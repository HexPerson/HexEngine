

#include "ShaderSystem.hpp"
#include "MaterialGraphCompiler.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	// Maps the active graphics backend to the shader-blob dialect we expect to
	// load. Used when reading v2 .hcs files (which carry multiple blobs per
	// stage tagged with their backend). v1 files implicitly hold DXBC.
	static ShaderBlobBackend ExpectedShaderBlobForDevice(IGraphicsDevice* device)
	{
		if (device == nullptr)
			return ShaderBlobBackend::DXBC_SM5;
		switch (device->GetBackend())
		{
		case GraphicsBackend::D3D12: return ShaderBlobBackend::DXIL_SM6;
		case GraphicsBackend::D3D11:
		default:                     return ShaderBlobBackend::DXBC_SM5;
		}
	}

	bool ShaderSystem::IsCachedShaderUsable(const fs::path& absolutePath)
	{
		if (!fs::exists(absolutePath))
			return false;

		DiskFile file(absolutePath, std::ios::in | std::ios::binary);
		if (!file.Open())
			return false;

		ShaderFileFormat header = {};
		if (file.GetSize() < sizeof(header))
			return false;
		file.Read(&header, sizeof(header));

		const bool isV1 = header._version == ShaderFileFormat::SHADER_FILE_VERSION;
		const bool isV2 = header._version == ShaderFileFormat::SHADER_FILE_VERSION_V2;
		if (!isV1 && !isV2)
			return false;

		const ShaderBlobBackend wantBackend = ExpectedShaderBlobForDevice(g_pEnv ? g_pEnv->_graphicsDevice : nullptr);

		// v1 implicitly holds DXBC; it can ONLY be used under D3D11. Anything
		// else needs a rebake. This is the case that fires today when an
		// editor session previously D3D11-baked a material-graph cache and
		// the engine now boots under D3D12.
		if (isV1)
			return wantBackend == ShaderBlobBackend::DXBC_SM5;

		// v2 carries a per-stage backend bitmap. The file is usable iff every
		// present stage advertises the wanted backend. Reading the tail is
		// cheap (sizeof = 4 * NumShaderStages = 24 bytes).
		ShaderFileFormatV2Tail tail = {};
		if (file.GetSize() < sizeof(header) + sizeof(tail))
			return false;
		file.Read(&tail, sizeof(tail));

		const uint32_t wantBit = 1u << (uint32_t)wantBackend;
		for (uint32_t stage = 0; stage < (uint32_t)ShaderStage::NumShaderStages; ++stage)
		{
			const bool stagePresent = HEX_HASFLAG(header._flags, (ShaderFileFlags)HEX_BITSET((uint8_t)stage));
			if (!stagePresent)
				continue;
			if ((tail._backendBitmap[stage] & wantBit) == 0)
				return false;
		}
		return true;
	}

	ShaderSystem::ShaderSystem()
	{
		g_pEnv->GetResourceSystem().RegisterResourceLoader(this);
	}

	ShaderSystem::~ShaderSystem()
	{
		g_pEnv->GetResourceSystem().UnregisterResourceLoader(this);
	}

	std::shared_ptr<IResource> ShaderSystem::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		// if its a .shader (not compiled shader) then just create a dummy and return it, there's nothing to actually load at this point
		if (absolutePath.extension() == ".shader")
		{
			std::shared_ptr<IShader> shader = std::shared_ptr<IShader>(new IShader, ResourceDeleter());
			_hotReloadShaders[absolutePath] = shader;
			return shader;
		}
		auto shader = ParseShaderInternal(absolutePath);

		if (!shader)
		{
			// Non-fatal: ParseShaderInternal already logged the specific reason
			// (version mismatch, corrupt header, stage size = 0, etc.). Callers
			// like MaterialLoader::ParseJson treat a null IShader as "rebake
			// needed" and drive the recompile path themselves; popping a
			// LOG_CRIT dialog here would block that flow and looks like a
			// fatal crash to the user even when the runtime is about to
			// recover.
			LOG_WARN("ShaderSystem: failed to parse shader '%s' - returning null so caller can recover.",
				absolutePath.filename().generic_u8string().c_str());
			return nullptr;
		}

		_loadedShaders[absolutePath] = shader;

		return shader;
	}

	std::shared_ptr<IResource> ShaderSystem::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
		auto shader = ParseShaderInternal(data);

		if (!shader)
		{
			LOG_CRIT("Failed to parse shader!");
			return nullptr;
		}

		_loadedShaders[relativePath] = shader;

		// Bytecode-pointer log keyed on the source's relative path - the
		// in-memory ParseShaderInternal overload has no path context, so we
		// emit the map here at the LoadResourceFromMemory boundary where
		// the path is available.
		{
			auto* vs = shader->_stages[(uint32_t)ShaderStage::VertexShader];
			auto* ps = shader->_stages[(uint32_t)ShaderStage::PixelShader];
			auto* gs = shader->_stages[(uint32_t)ShaderStage::GeometryShader];
			auto* cs = shader->_stages[(uint32_t)ShaderStage::ComputeShader];
			LOG_INFO("Shader bytecode map: '%s' vs=%p ps=%p gs=%p cs=%p",
				relativePath.filename().generic_u8string().c_str(),
				vs ? vs->GetNativePtr() : nullptr,
				ps ? ps->GetNativePtr() : nullptr,
				gs ? gs->GetNativePtr() : nullptr,
				cs ? cs->GetNativePtr() : nullptr);
		}

		return shader;
	}

	void ShaderSystem::UnloadResource(IResource* resource)
	{
		if (auto it = _loadedShaders.find(resource->GetAbsolutePath()); it != _loadedShaders.end())
		{
			_loadedShaders.erase(it);
		}

		SAFE_DELETE(resource);
	}

	std::vector<std::string> ShaderSystem::GetSupportedResourceExtensions()
	{
		return { ".hcs", ".shader" };
	}

	std::wstring ShaderSystem::GetResourceDirectory() const
	{
		return L"Shaders";
	}

	void ShaderSystem::OnResourceChanged(std::shared_ptr<IResource> resource)
	{
		auto it = _hotReloadShaders.find(resource->GetAbsolutePath());

		if (it == _hotReloadShaders.end())
			return;

		// find a hot reloadable dummy shader and link them together, if possible
		auto stem = resource->GetAbsolutePath().stem();
		std::shared_ptr<IShader> shaderToReload;
		for (auto& it : _loadedShaders)
		{
			if (it.first.stem() == stem)
			{
				shaderToReload = it.second.lock();
				break;
			}
		}

		if (!shaderToReload)
			return;

		auto shader = it->second;

		std::wstring compilerPath = (g_pEnv->GetFileSystem().GetBaseDirectory() / L"HexEngine.ShaderCompiler.exe");

		compilerPath += L" -I " + resource->GetAbsolutePath().wstring() + L" -T hlsl";
		compilerPath += L" -O " + shaderToReload->GetAbsolutePath().wstring();

		STARTUPINFOW si;
		PROCESS_INFORMATION pi;

		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		if (CreateProcessW(
			nullptr,
			(wchar_t*)compilerPath.c_str(),
			nullptr,
			nullptr,
			FALSE,
			0,
			nullptr,
			nullptr,
			&si,
			&pi) == FALSE)
		{
			LOG_CRIT("Failed to create an instance of HexEngine.ShaderCompiler.exe for hot reload. Error: %d", GetLastError());
			return;
		}

		WaitForSingleObject(pi.hProcess, INFINITE);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		

		

		auto reloadedShader = ParseShaderInternal(shaderToReload->GetAbsolutePath());

		if (!reloadedShader)
		{
			LOG_CRIT("Could not reload shader '%S'", shaderToReload->GetAbsolutePath());
			return;
		}

		shaderToReload->Destroy();

		for (auto i = 0; i < (int)ShaderStage::NumShaderStages; ++i)
		{
			if (reloadedShader->_stages[i] != nullptr)
			{
				// Transfer ownership of the newly created shader stage to the hot-reloaded shader.
				shaderToReload->_stages[i] = reloadedShader->_stages[i];
				reloadedShader->_stages[i] = nullptr;
			}
		}

	}

	void ShaderSystem::ReloadAllShaders()
	{
		for (auto& it : _loadedShaders)
		{
			/*for (auto i = 0; i < (int)ShaderStage::NumShaderStages; ++i)
			{
				if (it.second->_stages[i] != nullptr)
				{
					it.second->_stages[i]->Destroy();
				}
			}*/

			auto reloadedShader = ParseShaderInternal(it.first);

			if (!reloadedShader)
			{
				LOG_CRIT("Could not reload shader '%S'", it.first.c_str());
				continue;
			}

			auto sp = it.second.lock();
			sp->Destroy();

			for (auto i = 0; i < (int)ShaderStage::NumShaderStages; ++i)
			{
				if (reloadedShader->_stages[i] != nullptr)
				{
					// Transfer ownership of the newly created shader stage to the live shader.
					sp->_stages[i] = reloadedShader->_stages[i];
					reloadedShader->_stages[i] = nullptr;
				}
			}			
		}
	}

	std::shared_ptr<IShader> ShaderSystem::ParseShaderInternal(const fs::path& absolutePath)
	{
		DiskFile file(absolutePath, std::ios::in | std::ios::binary);

		if (file.Open() == false)
		{
			LOG_CRIT("Failed to load shader: %S", absolutePath.c_str());
			return nullptr;
		}

		// Read the header first
		//
		ShaderFileFormat shaderFile;
		file.Read(&shaderFile, sizeof(shaderFile));

		// V1 and V2 are both acceptable. V2 carries a per-blob backend tag
		// so the same .hcs file can ship DXBC + DXIL side by side; V1 files
		// are implicit-DXBC and only load under D3D11.
		const bool isV1 = shaderFile._version == ShaderFileFormat::SHADER_FILE_VERSION;
		const bool isV2 = shaderFile._version == ShaderFileFormat::SHADER_FILE_VERSION_V2;

		if (!isV1 && !isV2)
		{
			LOG_CRIT("Shader '%s' was built using an unsupported version. Loader supports v%d/v%d, file is v%d",
				absolutePath.filename().generic_u8string().c_str(),
				ShaderFileFormat::SHADER_FILE_VERSION,
				ShaderFileFormat::SHADER_FILE_VERSION_V2,
				shaderFile._version);
			return nullptr;
		}

		// V2 extends the header with a per-stage backend bitmap that we need to
		// read before the body.
		ShaderFileFormatV2Tail v2Tail = {};
		if (isV2)
			file.Read(&v2Tail, sizeof(v2Tail));

		const ShaderBlobBackend wantBackend = ExpectedShaderBlobForDevice(g_pEnv->_graphicsDevice);

		// V1 implicit-DXBC blob under a non-D3D11 backend. The MaterialLoader's
		// auto-recompile path only fires for materials that still carry their
		// graph data inline - many in-tree projects don't, so we can't rely on
		// it. Instead, attempt to rebake right here from the sibling .shader
		// source that MaterialGraphCompiler persists next to every generated
		// .hcs. If that succeeds we reopen and reparse the file under the
		// new dialect; if not, we fall through and return null.
		if (isV1 && wantBackend != ShaderBlobBackend::DXBC_SM5)
		{
			LOG_INFO("Shader '%s' is a v1 cache; rebaking from sibling source for backendId=%u.",
				absolutePath.filename().generic_u8string().c_str(),
				(uint32_t)wantBackend);
			file.Close();
			if (MaterialGraphCompiler::TryRebakeCachedShader(absolutePath))
			{
				// Recurse once - the .hcs on disk is now v2 with the wanted
				// dialect. Bounded recursion: the rebake either produces a
				// matching v2 or fails (we only recurse on success).
				return ParseShaderInternal(absolutePath);
			}
			LOG_WARN("Shader '%s' is a v1 (DXBC-only) blob and could not be rebaked. Returning null.",
				absolutePath.filename().generic_u8string().c_str());
			return nullptr;
		}

		std::shared_ptr<IShader> shader = std::shared_ptr<IShader>(new IShader, ResourceDeleter());

		std::vector<uint8_t> shaderBlobs[(uint32_t)ShaderStage::NumShaderStages];

		for (auto stage = 0U; stage < (uint32_t)ShaderStage::NumShaderStages; ++stage)
		{
			if (HEX_HASFLAG(shaderFile._flags, (ShaderFileFlags)HEX_BITSET((uint8_t)stage)))
			{
				if (shaderFile._shaderSizes[stage] <= 0)
				{
					LOG_CRIT("Shader file indicates that a shader stage is present (%d), but its size is 0!", stage);
					return nullptr;
				}

				if (isV2)
				{
					// V2 stage body: a sequence of ShaderBlobHeader + payload entries,
					// totalling _shaderSizes[stage] bytes. We scan for the entry whose
					// backendId matches the active backend.
					std::vector<uint8_t> stageBody(shaderFile._shaderSizes[stage]);
					file.Read(stageBody.data(), shaderFile._shaderSizes[stage]);

					uint32_t cursor = 0;
					bool found = false;
					while (cursor + sizeof(ShaderBlobHeader) <= stageBody.size())
					{
						const ShaderBlobHeader* hdr = reinterpret_cast<const ShaderBlobHeader*>(stageBody.data() + cursor);
						cursor += sizeof(ShaderBlobHeader);
						if (cursor + hdr->_blobBytes > stageBody.size())
						{
							LOG_CRIT("Shader '%s' stage %d v2 blob header overruns stage body", absolutePath.filename().generic_u8string().c_str(), stage);
							return nullptr;
						}
						if (hdr->_backendId == (uint32_t)wantBackend)
						{
							shaderBlobs[stage].assign(stageBody.begin() + cursor, stageBody.begin() + cursor + hdr->_blobBytes);
							found = true;
							break;
						}
						cursor += hdr->_blobBytes;
					}

					if (!found)
					{
						LOG_CRIT("Shader '%s' stage %d has no v2 blob for backendId=%u (bitmap=0x%x)",
							absolutePath.filename().generic_u8string().c_str(), stage, (uint32_t)wantBackend, v2Tail._backendBitmap[stage]);
						return nullptr;
					}
				}
				else
				{
					// V1: read the stage blob raw.
					shaderBlobs[stage].resize(shaderFile._shaderSizes[stage]);
					file.Read(shaderBlobs[stage].data(), shaderFile._shaderSizes[stage]);
				}

				switch (static_cast<ShaderStage>(stage))
				{
				case ShaderStage::VertexShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreateVertexShader(shaderBlobs[stage]);
					break;

				case ShaderStage::PixelShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreatePixelShader(shaderBlobs[stage]);
					break;

				case ShaderStage::GeometryShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreateGeometryShader(shaderBlobs[stage]);
					break;

				case ShaderStage::ComputeShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreateComputeShader(shaderBlobs[stage]);
					break;
				}
			}
		}

		if (shader->_stages[(uint32_t)ShaderStage::VertexShader] != nullptr)
		{
			shader->_inputLayout = IInputLayout::GetInputLayoutById(shaderFile._inputLayout, shader->_stages[(uint32_t)ShaderStage::VertexShader]);
		}
		else
		{
			shader->_inputLayout = nullptr;
		}
		shader->_requirements = shaderFile._requirements;

		// Log the native bytecode pointers alongside the shader's source
		// path. The D3D12 plugin's draw-trace ring records vsBytecode /
		// psBytecode pointers on every Draw; pairing those raw addresses
		// with the human-readable shader name in the log lets us identify
		// the failing shader when DRED dumps the trace on a GPU hang.
		{
			auto* vs = shader->_stages[(uint32_t)ShaderStage::VertexShader];
			auto* ps = shader->_stages[(uint32_t)ShaderStage::PixelShader];
			auto* gs = shader->_stages[(uint32_t)ShaderStage::GeometryShader];
			auto* cs = shader->_stages[(uint32_t)ShaderStage::ComputeShader];
			LOG_INFO("Shader bytecode map: '%s' vs=%p ps=%p gs=%p cs=%p",
				absolutePath.filename().generic_u8string().c_str(),
				vs ? vs->GetNativePtr() : nullptr,
				ps ? ps->GetNativePtr() : nullptr,
				gs ? gs->GetNativePtr() : nullptr,
				cs ? cs->GetNativePtr() : nullptr);
		}

		return shader;
	}

	std::shared_ptr<IShader> ShaderSystem::ParseShaderInternal(const std::vector<uint8_t>& data)
	{
		const uint8_t* base = data.data();
		const uint8_t* end  = data.data() + data.size();
		const uint8_t* p    = base;

		if (p + sizeof(ShaderFileFormat) > end)
		{
			LOG_CRIT("Shader memory blob (%zu bytes) is too small for header", data.size());
			return nullptr;
		}

		const ShaderFileFormat* shaderFile = reinterpret_cast<const ShaderFileFormat*>(p);
		p += sizeof(ShaderFileFormat);

		// Mirror the file-path variant: accept both v1 (legacy single-DXBC) and
		// v2 (multi-backend per-stage). v2 carries the per-stage
		// backend bitmap in a tail struct, followed by a stage body which is a
		// sequence of (ShaderBlobHeader + payload) entries. v1's stage body is
		// the raw bytecode for the stage.
		const bool isV1 = shaderFile->_version == ShaderFileFormat::SHADER_FILE_VERSION;
		const bool isV2 = shaderFile->_version == ShaderFileFormat::SHADER_FILE_VERSION_V2;

		if (!isV1 && !isV2)
		{
			LOG_CRIT("Shader memory blob is an unsupported version. Loader supports v%d/v%d, blob is v%d",
				ShaderFileFormat::SHADER_FILE_VERSION,
				ShaderFileFormat::SHADER_FILE_VERSION_V2,
				shaderFile->_version);
			return nullptr;
		}

		ShaderFileFormatV2Tail v2Tail = {};
		if (isV2)
		{
			if (p + sizeof(ShaderFileFormatV2Tail) > end)
			{
				LOG_CRIT("Shader memory blob is v2 but too small for V2 tail");
				return nullptr;
			}
			std::memcpy(&v2Tail, p, sizeof(v2Tail));
			p += sizeof(v2Tail);
		}

		const ShaderBlobBackend wantBackend = ExpectedShaderBlobForDevice(g_pEnv->_graphicsDevice);

		// v1 blob under a non-D3D11 backend: the rebake path is path-based
		// (sibling .shader source), which we don't have for an in-memory load.
		// Refuse cleanly so the caller can decide what to do.
		if (isV1 && wantBackend != ShaderBlobBackend::DXBC_SM5)
		{
			LOG_WARN("Shader memory blob is v1 (DXBC-only) under non-D3D11 backend (backendId=%u); cannot rebake from memory. Returning null.",
				(uint32_t)wantBackend);
			return nullptr;
		}

		std::shared_ptr<IShader> shader = std::shared_ptr<IShader>(new IShader, ResourceDeleter());

		std::vector<uint8_t> shaderBlobs[(uint32_t)ShaderStage::NumShaderStages];

		for (auto stage = 0U; stage < (uint32_t)ShaderStage::NumShaderStages; ++stage)
		{
			if (HEX_HASFLAG(shaderFile->_flags, (ShaderFileFlags)HEX_BITSET((uint8_t)stage)))
			{
				if (shaderFile->_shaderSizes[stage] <= 0)
				{
					LOG_CRIT("Shader file indicates that a shader stage is present (%d), but its size is 0!", stage);
					return nullptr;
				}

				if (p + shaderFile->_shaderSizes[stage] > end)
				{
					LOG_CRIT("Shader memory blob stage %d body (%d bytes) overruns blob end", stage, shaderFile->_shaderSizes[stage]);
					return nullptr;
				}

				if (isV2)
				{
					// Scan the stage body for an entry whose backendId matches.
					const uint8_t* stageBody    = p;
					const uint32_t stageBodyLen = shaderFile->_shaderSizes[stage];
					uint32_t cursor = 0;
					bool found = false;
					while (cursor + sizeof(ShaderBlobHeader) <= stageBodyLen)
					{
						const ShaderBlobHeader* hdr = reinterpret_cast<const ShaderBlobHeader*>(stageBody + cursor);
						cursor += sizeof(ShaderBlobHeader);
						if (cursor + hdr->_blobBytes > stageBodyLen)
						{
							LOG_CRIT("Shader memory blob stage %d v2 entry overruns stage body", stage);
							return nullptr;
						}
						if (hdr->_backendId == (uint32_t)wantBackend)
						{
							shaderBlobs[stage].assign(stageBody + cursor, stageBody + cursor + hdr->_blobBytes);
							found = true;
							break;
						}
						cursor += hdr->_blobBytes;
					}
					p += stageBodyLen;

					if (!found)
					{
						LOG_CRIT("Shader memory blob stage %d has no v2 blob for backendId=%u (bitmap=0x%x)",
							stage, (uint32_t)wantBackend, v2Tail._backendBitmap[stage]);
						return nullptr;
					}
				}
				else
				{
					// v1: raw stage bytecode.
					shaderBlobs[stage].resize(shaderFile->_shaderSizes[stage]);
					std::memcpy(shaderBlobs[stage].data(), p, shaderFile->_shaderSizes[stage]);
					p += shaderFile->_shaderSizes[stage];
				}

				switch (static_cast<ShaderStage>(stage))
				{
				case ShaderStage::VertexShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreateVertexShader(shaderBlobs[stage]);
					break;

				case ShaderStage::PixelShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreatePixelShader(shaderBlobs[stage]);
					break;

				case ShaderStage::GeometryShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreateGeometryShader(shaderBlobs[stage]);
					break;

				case ShaderStage::ComputeShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreateComputeShader(shaderBlobs[stage]);
					break;
				}
			}
		}

		if (shader->_stages[(uint32_t)ShaderStage::VertexShader] != nullptr)
		{
			shader->_inputLayout = IInputLayout::GetInputLayoutById(shaderFile->_inputLayout, shader->_stages[(uint32_t)ShaderStage::VertexShader]);
		}
		else
		{
			shader->_inputLayout = nullptr;
		}
		shader->_requirements = shaderFile->_requirements;

		// Bytecode-pointer log for hang diagnostics (matches the file-path
		// variant above). This overload is the in-memory load path used by
		// shaders coming from packaged data, hot reload, and runtime-built
		// material-graph rebakes. No source path is available here, so we
		// log "<memory:%zu bytes>" as the identifier.
		{
			auto* vs = shader->_stages[(uint32_t)ShaderStage::VertexShader];
			auto* ps = shader->_stages[(uint32_t)ShaderStage::PixelShader];
			auto* gs = shader->_stages[(uint32_t)ShaderStage::GeometryShader];
			auto* cs = shader->_stages[(uint32_t)ShaderStage::ComputeShader];
			LOG_INFO("Shader bytecode map: '<memory:%zu bytes>' vs=%p ps=%p gs=%p cs=%p",
				data.size(),
				vs ? vs->GetNativePtr() : nullptr,
				ps ? ps->GetNativePtr() : nullptr,
				gs ? gs->GetNativePtr() : nullptr,
				cs ? cs->GetNativePtr() : nullptr);
		}

		return shader;
	}
}
