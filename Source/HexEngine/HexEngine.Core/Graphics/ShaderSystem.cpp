

#include "ShaderSystem.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
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
			LOG_CRIT("Failed to parse shader!");
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

		// check the shader version, it must match our current version
		if (shaderFile._version != ShaderFileFormat::SHADER_FILE_VERSION)
		{
			LOG_CRIT("Shader '%s' was built using an outdated version. Current = %d, Shader = %d",
				absolutePath.filename().generic_u8string().c_str(), ShaderFileFormat::SHADER_FILE_VERSION, shaderFile._version);

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

				// Read the vertex shader data
				//
				shaderBlobs[stage].resize(shaderFile._shaderSizes[stage]);

				file.Read(shaderBlobs[stage].data(), shaderFile._shaderSizes[stage]);

				switch (static_cast<ShaderStage>(stage))
				{
				case ShaderStage::VertexShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreateVertexShader(shaderBlobs[stage]);
					break;

				case ShaderStage::PixelShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreatePixelShader(shaderBlobs[stage]);
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

		return shader;
	}

	std::shared_ptr<IShader> ShaderSystem::ParseShaderInternal(const std::vector<uint8_t>& data)
	{
		uint8_t* p = (uint8_t*)data.data();

		// Read the header first
		//
		ShaderFileFormat* shaderFile = (ShaderFileFormat*)p;
		p += sizeof(ShaderFileFormat);

		// check the shader version, it must match our current version
		if (shaderFile->_version != ShaderFileFormat::SHADER_FILE_VERSION)
		{
			LOG_CRIT("Shader was built using an outdated version. Current = %d, Shader = %d",
				ShaderFileFormat::SHADER_FILE_VERSION, shaderFile->_version);

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

				// Read the vertex shader data
				//
				shaderBlobs[stage].resize(shaderFile->_shaderSizes[stage]);

				memcpy(shaderBlobs[stage].data(), p, shaderFile->_shaderSizes[stage]);
				p += shaderFile->_shaderSizes[stage];

				switch (static_cast<ShaderStage>(stage))
				{
				case ShaderStage::VertexShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreateVertexShader(shaderBlobs[stage]);
					break;

				case ShaderStage::PixelShader:
					shader->_stages[stage] = g_pEnv->_graphicsDevice->CreatePixelShader(shaderBlobs[stage]);
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

		return shader;
	}
}
