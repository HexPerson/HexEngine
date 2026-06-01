

#include "HLSL.hpp"

#include <sstream>
#include <ShaderConductor/ShaderConductor.hpp>

#pragma comment(lib,"d3dcompiler.lib")

// Per-stage HLSL profile strings.
// DXBC path uses FXC (D3DCompile) at SM 5.0 - the legacy D3D11 path.
// DXIL path uses ShaderConductor (DXC) at SM 6.0 - the D3D12 path. The same
// HLSL source compiles to both unless it uses SM 6.0+-only features.
static const std::string gShaderStageToSm5Version[(uint32_t)HexEngine::ShaderStage::NumShaderStages] = {
	"vs_5_0",
	"ps_5_0",
	"gs_5_0",
	"hs_5_0",
	"ds_5_0",
	"cs_5_0"
};

static ShaderConductor::ShaderStage HexStageToScStage(HexEngine::ShaderStage s)
{
	switch (s)
	{
	case HexEngine::ShaderStage::VertexShader:   return ShaderConductor::ShaderStage::VertexShader;
	case HexEngine::ShaderStage::PixelShader:    return ShaderConductor::ShaderStage::PixelShader;
	case HexEngine::ShaderStage::GeometryShader: return ShaderConductor::ShaderStage::GeometryShader;
	case HexEngine::ShaderStage::HullShader:     return ShaderConductor::ShaderStage::HullShader;
	case HexEngine::ShaderStage::DomainShader:   return ShaderConductor::ShaderStage::DomainShader;
	case HexEngine::ShaderStage::ComputeShader:  return ShaderConductor::ShaderStage::ComputeShader;
	default:                                     return ShaderConductor::ShaderStage::NumShaderStages;
	}
}

bool HLSL::Compile(const fs::path& filePath, CompiledShader& out)
{
	std::string shaderData[(uint32_t)ShaderStage::NumShaderStages];
	int32_t lineOffsets[(uint32_t)ShaderStage::NumShaderStages];

	if (!ReadShader(filePath, shaderData, out.inputLayout, out.requirements, lineOffsets))
		return false;

	bool anyStageCompiled = false;

	for (uint32_t stageIdx = 0; stageIdx < (uint32_t)ShaderStage::NumShaderStages; ++stageIdx)
	{
		auto& data = shaderData[stageIdx];
		if (data.empty())
			continue;

		const HexEngine::ShaderStage stage = static_cast<HexEngine::ShaderStage>(stageIdx);

		bool stageEmittedSomething = false;

		// ----- DXBC (FXC / SM 5.0) -----
		// This is the canonical D3D11 path. Failure here is treated as a
		// hard error - the engine still ships the D3D11 backend as primary.
		{
			ID3DBlob* pCode = nullptr;
			ID3DBlob* pErrors = nullptr;

			auto targetVer = gShaderStageToSm5Version[stageIdx];

			HRESULT hr = D3DCompile(
				data.data(),
				data.size(),
				nullptr,
				nullptr,
				this,
				"ShaderMain",
				targetVer.c_str(),
#ifdef _DEBUG
				D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
#else
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
#endif
				0,
				&pCode,
				&pErrors);

			if (hr != S_OK)
			{
				if (pErrors != nullptr)
				{
					std::stringstream errorstr;
					errorstr << (const char*)pErrors->GetBufferPointer();

					std::string error;
					while (std::getline(errorstr, error))
					{
						int line = 0;
						int column = 0;

						if (auto p = error.find('('); p != error.npos)
						{
							auto p2 = error.find(',', p);
							auto sline = error.substr(p + 1, p2 - p - 1);
							line = std::stoi(sline);

							auto p3 = error.find('-', p);
							auto scolumn = error.substr(p2 + 1, p3 - p2 - 1);
							column = std::stoi(scolumn);
						}

						printf("%S:%d:%d: error (DXBC): %s\n", filePath.c_str(), line + lineOffsets[stageIdx], column, error.c_str());
					}
					pErrors->Release();
				}
				if (pCode) pCode->Release();
				return false;
			}

			auto size = pCode->GetBufferSize();
			printf("%S stage %u: DXBC compiled, %lld bytes\n", filePath.filename().c_str(), stageIdx, size);

			CompiledStage::Blob blob;
			blob.backend = HexEngine::ShaderBlobBackend::DXBC_SM5;
			blob.bytes.resize(size);
			memcpy(blob.bytes.data(), pCode->GetBufferPointer(), size);
			out.stages[stageIdx].blobs.push_back(std::move(blob));

			pCode->Release();
			if (pErrors) pErrors->Release();

			stageEmittedSomething = true;
		}

		// ----- DXIL (ShaderConductor / DXC / SM 6.0) -----
		// This path is best-effort. If a shader uses constructs DXC rejects
		// (rare for engine-written SM 5.0 HLSL but it happens with some
		// register() declarations or implicit conversions), we log the
		// failure and ship DXBC-only - the v2 .hcs loader will refuse to load
		// the shader under D3D12 with a clear "re-bake" message.
		{
			ShaderConductor::Compiler::SourceDesc src{};
			src.source     = data.c_str();
			src.fileName   = nullptr;
			src.entryPoint = "ShaderMain";
			src.stage      = HexStageToScStage(stage);
			src.defines    = nullptr;
			src.numDefines = 0;
			src.loadIncludeCallback = [this](const char* includeName) -> ShaderConductor::Blob
			{
				const void* data  = nullptr;
				UINT        bytes = 0;
				if (FAILED(this->Open(D3D_INCLUDE_LOCAL, includeName, nullptr, &data, &bytes)))
					return ShaderConductor::Blob();
				ShaderConductor::Blob out(data, bytes);
				this->Close(data);
				return out;
			};

			ShaderConductor::Compiler::Options opts{};
			opts.packMatricesInRowMajor = true;
			opts.enable16bitTypes       = false;
			opts.optimizationLevel      = 3;
			opts.shaderModel            = {6, 0};
#ifdef _DEBUG
			opts.enableDebugInfo     = true;
			opts.disableOptimizations = true;
#endif

			ShaderConductor::Compiler::TargetDesc tgt{};
			tgt.language = ShaderConductor::ShadingLanguage::Dxil;
			tgt.version  = nullptr;
			tgt.asModule = false;

			ShaderConductor::Compiler::ResultDesc result = ShaderConductor::Compiler::Compile(src, opts, tgt);

			if (result.hasError)
			{
				const char* msg = result.errorWarningMsg.Size() > 0
					? reinterpret_cast<const char*>(result.errorWarningMsg.Data())
					: "<no error message>";
				printf("%S stage %u: DXIL compile failed (D3D12 will reject this shader): %s\n",
					filePath.filename().c_str(), stageIdx, msg);
			}
			else if (result.target.Size() == 0)
			{
				printf("%S stage %u: DXIL compile produced no output (skipping D3D12)\n",
					filePath.filename().c_str(), stageIdx);
			}
			else
			{
				printf("%S stage %u: DXIL compiled, %u bytes\n",
					filePath.filename().c_str(), stageIdx, result.target.Size());

				CompiledStage::Blob blob;
				blob.backend = HexEngine::ShaderBlobBackend::DXIL_SM6;
				blob.bytes.resize(result.target.Size());
				memcpy(blob.bytes.data(), result.target.Data(), result.target.Size());
				out.stages[stageIdx].blobs.push_back(std::move(blob));
			}
		}

		if (stageEmittedSomething)
			anyStageCompiled = true;
	}

	return anyStageCompiled;
}

HRESULT HLSL::Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes)
{
	char* fileName = (char*)pFileName;

	if (fileName[0] == '\t')
		fileName++;

	while (fileName[0] == ' ')
		fileName++;

	auto shaderPath = _includePath.length() > 0 ? _includePath : gWorkingDirectory;
	shaderPath += fileName;

	HexEngine::DiskFile file(shaderPath, std::ios::in);

	if (!file.Open())
	{
		printf("ShaderCompiler :: Failed to open '%S' for compilation!\n", shaderPath.c_str());
		return S_FALSE;
	}

	HexEngine::KeyValues kv;

	if (!kv.Parse(&file))
	{
		printf("ShaderCompiler :: Failed to parse '%S'!\n", shaderPath.c_str());
		return S_FALSE;
	}

	auto keyValues = kv.GetKeyValues();

	if (keyValues.size() == 0)
	{
		printf("ShaderCompiler :: Shader does not have any data or is not formatted correctly\n");
		return S_FALSE;
	}

	std::string includeData;
	int lineOffset = 0;
	ProcessSection("Global", keyValues, includeData, lineOffset);

	if (includeData.length() > 0)
	{
		// allocate the new data
		uint8_t* data = new uint8_t[includeData.size()];
		memcpy(data, includeData.data(), includeData.size());

		*ppData = data;
		*pBytes = (UINT)includeData.size();
	}

	return S_OK;
}

HRESULT HLSL::Close(LPCVOID pData)
{
	SAFE_DELETE_ARRAY(pData);
	return S_OK;
}
