

#include "ShaderSystem.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	ShaderSystem::ShaderSystem()
	{
		g_pEnv->_resourceSystem->RegisterResourceLoader(this);
	}

	ShaderSystem::~ShaderSystem()
	{
		g_pEnv->_resourceSystem->UnregisterResourceLoader(this);
	}

	std::shared_ptr<IResource> ShaderSystem::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
	{
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
		return { ".hcs" };
	}

	std::wstring ShaderSystem::GetResourceDirectory() const
	{
		return L"Shaders";
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
					sp->_stages[i] = reloadedShader->_stages[i];
					sp->_stages[i]->CopyFrom(reloadedShader->_stages[i]);
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
				}
			}
		}

		shader->_inputLayout = IInputLayout::GetInputLayoutById(shaderFile._inputLayout, shader->_stages[(uint32_t)ShaderStage::VertexShader]);
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
				}
			}
		}

		shader->_inputLayout = IInputLayout::GetInputLayoutById(shaderFile->_inputLayout, shader->_stages[(uint32_t)ShaderStage::VertexShader]);
		shader->_requirements = shaderFile->_requirements;

		return shader;
	}
}