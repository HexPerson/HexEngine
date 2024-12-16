

#include "HLSL.hpp"

#include <sstream>

#pragma comment(lib,"d3dcompiler.lib")

const std::string gShaderStageToVersion[(uint32_t)HexEngine::ShaderStage::NumShaderStages] = {
	"vs_5_0",
	"ps_5_0",
	"gs_5_0",
	"hs_5_0",
	"ds_5_0",
	"cs_5_0"
};

bool HLSL::Compile(const fs::path& filePath, std::vector<uint8_t>& dataOut, HexEngine::ShaderFileFormat& shader)
{
	std::string shaderData[(uint32_t)ShaderStage::NumShaderStages];
	int32_t lineOffsets[(uint32_t)ShaderStage::NumShaderStages];

	ReadShader(filePath, shaderData, shader._inputLayout, shader._requirements, lineOffsets);

	int32_t stageIdx = 0;

	for (auto& data : shaderData)
	{
		if (data.length() > 0)
		{
			ID3DBlob* pCode, * pErrors;

			const HexEngine::ShaderStage stage = static_cast<HexEngine::ShaderStage>(stageIdx);

			printf("Stage %d line is %d\n", stage, lineOffsets[stageIdx]);

			auto targetVer = gShaderStageToVersion[(&data - shaderData)];

			if (HRESULT hr = D3DCompile(
				data.data(),
				data.size(),
				nullptr,
				nullptr,
				this,
				"ShaderMain",
				targetVer.c_str(),
#ifdef _DEBUG
				D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_PREFER_FLOW_CONTROL,
#else
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
#endif
				0,
				&pCode,
				&pErrors); hr != S_OK)
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

					auto p = error.find_last_of(':');

					auto errorstr = error.substr(p + 2);

					printf("%S:%d:%d: error: %s\n", filePath.c_str(), line + lineOffsets[stageIdx], column, errorstr.c_str());
				}
				return false;
			}

			auto size = pCode->GetBufferSize();

			//size += 800;

			printf("%S %d compiled successfully! Size is %lld\n", filePath.filename().c_str(), stageIdx, size);

			dataOut.insert(dataOut.end(), (uint8_t*)pCode->GetBufferPointer(), (uint8_t*)pCode->GetBufferPointer() + size);

			shader._shaderSizes[stageIdx] = (uint32_t)size;
			shader._flags |= (HexEngine::ShaderFileFlags)HEX_BITSET((uint8_t)stageIdx);
		}

		++stageIdx;
	}

	return true;
}

HRESULT HLSL::Open(D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes)
{
	char* fileName = (char*)pFileName;

	if (fileName[0] == '\t')
		fileName++;

	while (fileName[0] == ' ')
		fileName++;

	auto shaderPath = gWorkingDirectory;
	shaderPath += fileName;

	HexEngine::DiskFile file(shaderPath, std::ios::in);

	//printf("Opening '%s' as a shader include\n", pFileName);

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