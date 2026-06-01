

#include "HLSL.hpp"

#include <sstream>
#include <dxc/dxcapi.h>
#include <wrl/client.h>

#pragma comment(lib,"d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

// Per-stage HLSL profile strings.
// DXBC path uses FXC (D3DCompile) at SM 5.0 - the legacy D3D11 path.
// DXIL path uses DXC directly via dxcompiler.dll's COM API at SM 6.0 - the
// D3D12 path. The same HLSL source compiles to both unless it uses SM 6.0+-
// only features.
//
// We bypass ShaderConductor's HLSL->DXIL helper deliberately - its reflection
// pass crashes inside dxcompiler.dll due to a CRT mismatch (std::map iterator
// across DLL boundaries between exe and dxcompiler.dll). We only need the raw
// bytecode, not reflection; talking to DXC over COM dodges the bug entirely.
static const std::string gShaderStageToSm5Version[(uint32_t)HexEngine::ShaderStage::NumShaderStages] = {
	"vs_5_0",
	"ps_5_0",
	"gs_5_0",
	"hs_5_0",
	"ds_5_0",
	"cs_5_0"
};

static const wchar_t* gShaderStageToSm6Target[(uint32_t)HexEngine::ShaderStage::NumShaderStages] = {
	L"vs_6_0",
	L"ps_6_0",
	L"gs_6_0",
	L"hs_6_0",
	L"ds_6_0",
	L"cs_6_0"
};

namespace
{
	// Lazily-initialised process-wide DXC instances. The COM objects are
	// expensive to spin up (~50 ms each) so we share them across stages and
	// shaders within a single compiler invocation.
	struct DxcGlobals
	{
		ComPtr<IDxcUtils>         utils;
		ComPtr<IDxcCompiler3>     compiler;
		ComPtr<IDxcIncludeHandler> defaultIncludeHandler;
		bool                       initialised = false;
		bool                       initFailed  = false;
	};

	DxcGlobals& Dxc()
	{
		static DxcGlobals g;
		if (!g.initialised && !g.initFailed)
		{
			// dxcompiler.dll is expected to sit next to ShaderCompiler.exe.
			// Note: NO LoadLibrary call - linking against dxcompiler.lib (via
			// the implicit reference from #pragma comment(lib, ...) below, OR
			// via direct DxcCreateInstance symbol) pulls it in implicitly.
			HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&g.utils));
			if (FAILED(hr))
			{
				printf("DXC :: DxcCreateInstance(CLSID_DxcUtils) failed (0x%08X)\n", hr);
				g.initFailed = true;
				return g;
			}
			hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&g.compiler));
			if (FAILED(hr))
			{
				printf("DXC :: DxcCreateInstance(CLSID_DxcCompiler) failed (0x%08X)\n", hr);
				g.initFailed = true;
				return g;
			}
			hr = g.utils->CreateDefaultIncludeHandler(&g.defaultIncludeHandler);
			if (FAILED(hr))
			{
				printf("DXC :: CreateDefaultIncludeHandler failed (0x%08X)\n", hr);
				g.initFailed = true;
				return g;
			}
			g.initialised = true;
		}
		return g;
	}

	// IDxcIncludeHandler shim that forwards #include resolution to HLSL's
	// existing ID3DInclude implementation. DXC needs wide-char paths and uses
	// IDxcBlob to hand the source back; we adapt across both.
	class HlslIncludeHandlerForDxc : public IDxcIncludeHandler
	{
	public:
		HlslIncludeHandlerForDxc(HLSL* owner, IDxcUtils* utils, IDxcIncludeHandler* defaultHandler)
			: _owner(owner), _utils(utils), _defaultHandler(defaultHandler) {}

		ULONG STDMETHODCALLTYPE AddRef() override  { return ++_refCount; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG c = --_refCount;
			if (c == 0) delete this;
			return c;
		}
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override
		{
			if (iid == __uuidof(IDxcIncludeHandler) || iid == IID_IUnknown)
			{
				*ppv = static_cast<IDxcIncludeHandler*>(this);
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}

		HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource) override
		{
			if (ppIncludeSource == nullptr)
				return E_POINTER;
			*ppIncludeSource = nullptr;

			// Convert to UTF-8 + strip any "./" prefix DXC tends to prepend, then
			// delegate to HLSL::Open which already understands the engine's
			// .shader include format.
			char narrow[MAX_PATH] = {};
			WideCharToMultiByte(CP_UTF8, 0, pFilename, -1, narrow, sizeof(narrow), nullptr, nullptr);

			const char* clean = narrow;
			if (clean[0] == '.' && (clean[1] == '/' || clean[1] == '\\'))
				clean += 2;

			const void* data  = nullptr;
			UINT        bytes = 0;
			if (FAILED(_owner->Open(D3D_INCLUDE_LOCAL, clean, nullptr, &data, &bytes)))
			{
				// Fall back to the default handler so files outside our .shader
				// include space (e.g. compiler intrinsics) still resolve.
				return _defaultHandler->LoadSource(pFilename, ppIncludeSource);
			}

			// IDxcBlobEncoding needs the source as UTF-8 with an encoding hint.
			ComPtr<IDxcBlobEncoding> blob;
			HRESULT hr = _utils->CreateBlobFromPinned(data, bytes, DXC_CP_UTF8, &blob);
			if (FAILED(hr))
			{
				_owner->Close(data);
				return hr;
			}

			// Copy into a blob DXC can own past our Close() of the source page.
			ComPtr<IDxcBlobEncoding> ownedBlob;
			hr = _utils->CreateBlob(data, bytes, DXC_CP_UTF8, &ownedBlob);
			_owner->Close(data);
			if (FAILED(hr))
				return hr;

			*ppIncludeSource = ownedBlob.Detach();
			return S_OK;
		}

	private:
		HLSL*                _owner          = nullptr;
		IDxcUtils*           _utils          = nullptr;
		IDxcIncludeHandler*  _defaultHandler = nullptr;
		std::atomic<ULONG>   _refCount{ 1 };
	};

	// Format a DXC error blob to the same `path:line:col: error: msg` form FXC
	// uses, with the engine's per-stage line offset folded in so VS double-
	// click navigation still lands on the right line of the source .shader.
	void PrintDxcErrors(const fs::path& filePath, uint32_t stageIdx, int lineOffset, IDxcBlobUtf8* errors)
	{
		if (errors == nullptr || errors->GetStringLength() == 0)
			return;

		const char* msg = errors->GetStringPointer();
		std::stringstream ss(msg);
		std::string line;
		while (std::getline(ss, line))
		{
			if (line.empty())
				continue;
			printf("%S stage %u DXIL: %s\n", filePath.filename().c_str(), stageIdx, line.c_str());
		}
	}
}

bool HLSL::Compile(const fs::path& filePath, CompiledShader& out)
{
	std::string shaderData[(uint32_t)ShaderStage::NumShaderStages];
	int32_t lineOffsets[(uint32_t)ShaderStage::NumShaderStages];

	if (!ReadShader(filePath, shaderData, out.inputLayout, out.requirements, lineOffsets))
		return false;

	auto& dxc = Dxc();

	bool anyStageCompiled = false;

	for (uint32_t stageIdx = 0; stageIdx < (uint32_t)ShaderStage::NumShaderStages; ++stageIdx)
	{
		auto& data = shaderData[stageIdx];
		if (data.empty())
			continue;

		const HexEngine::ShaderStage stage = static_cast<HexEngine::ShaderStage>(stageIdx);
		bool stageEmittedSomething = false;

		// ----- DXBC (FXC / SM 5.0) -----
		// This is the canonical D3D11 path. Failure here is treated as a hard
		// error - the engine ships the D3D11 backend as primary.
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

		// ----- DXIL (DXC via IDxcCompiler3 / SM 6.0) -----
		// Best-effort: failures are logged but don't fail the build. Shaders
		// that DXC rejects ship DXBC-only; the v2 .hcs loader refuses them
		// under D3D12 with a "re-bake into v2" message so the user knows.
		if (!dxc.initFailed)
		{
			// Wrap the HLSL source as a DXC-owned blob.
			ComPtr<IDxcBlobEncoding> srcBlob;
			HRESULT hr = dxc.utils->CreateBlob(data.data(), (uint32_t)data.size(), DXC_CP_UTF8, &srcBlob);
			if (FAILED(hr))
			{
				printf("%S stage %u: DXC CreateBlob failed (0x%08X) - shipping DXBC-only\n", filePath.filename().c_str(), stageIdx, hr);
				if (stageEmittedSomething) anyStageCompiled = true;
				continue;
			}

			// Build the DXC argv. Mirrors the FXC defines: matrix major matches
			// HexEngine convention (row major in the C++ side, packed via
			// /Zpr); HLSL 2021 stays opt-in so SM 5.0 source stays valid.
			std::vector<LPCWSTR> args;
			args.push_back(L"-T");
			args.push_back(gShaderStageToSm6Target[stageIdx]);
			args.push_back(L"-E");
			args.push_back(L"ShaderMain");
			args.push_back(L"-Zpr"); // pack matrices in row-major
#ifdef _DEBUG
			args.push_back(L"-Zi"); // embed debug info
			args.push_back(L"-Od"); // disable optimisations
#else
			args.push_back(L"-O3");
#endif

			DxcBuffer srcBuf = {};
			srcBuf.Ptr      = srcBlob->GetBufferPointer();
			srcBuf.Size     = srcBlob->GetBufferSize();
			srcBuf.Encoding = DXC_CP_UTF8;

			// Per-compile include handler that delegates to HLSL::Open. Created
			// fresh each compile - it holds a back-pointer to `this`, so we
			// can't safely share it across HLSL instances.
			ComPtr<IDxcIncludeHandler> includeHandler(new HlslIncludeHandlerForDxc(this, dxc.utils.Get(), dxc.defaultIncludeHandler.Get()));

			ComPtr<IDxcResult> result;
			hr = dxc.compiler->Compile(
				&srcBuf,
				args.data(),
				(uint32_t)args.size(),
				includeHandler.Get(),
				IID_PPV_ARGS(&result));
			if (FAILED(hr))
			{
				printf("%S stage %u: DXC Compile failed (0x%08X) - shipping DXBC-only\n", filePath.filename().c_str(), stageIdx, hr);
				if (stageEmittedSomething) anyStageCompiled = true;
				continue;
			}

			// Error / warning text first, regardless of success - DXC produces
			// useful warnings even on a successful compile.
			ComPtr<IDxcBlobUtf8> errors;
			result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
			if (errors && errors->GetStringLength() > 0)
				PrintDxcErrors(filePath, stageIdx, lineOffsets[stageIdx], errors.Get());

			HRESULT status = E_FAIL;
			result->GetStatus(&status);
			if (FAILED(status))
			{
				printf("%S stage %u: DXIL compile failed (status 0x%08X) - shipping DXBC-only\n",
					filePath.filename().c_str(), stageIdx, status);
				if (stageEmittedSomething) anyStageCompiled = true;
				continue;
			}

			ComPtr<IDxcBlob> dxilBlob;
			result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxilBlob), nullptr);
			if (!dxilBlob || dxilBlob->GetBufferSize() == 0)
			{
				printf("%S stage %u: DXIL compile produced no output (shipping DXBC-only)\n",
					filePath.filename().c_str(), stageIdx);
				if (stageEmittedSomething) anyStageCompiled = true;
				continue;
			}

			printf("%S stage %u: DXIL compiled, %llu bytes\n",
				filePath.filename().c_str(), stageIdx, (uint64_t)dxilBlob->GetBufferSize());

			CompiledStage::Blob blob;
			blob.backend = HexEngine::ShaderBlobBackend::DXIL_SM6;
			blob.bytes.resize(dxilBlob->GetBufferSize());
			memcpy(blob.bytes.data(), dxilBlob->GetBufferPointer(), dxilBlob->GetBufferSize());
			out.stages[stageIdx].blobs.push_back(std::move(blob));
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
