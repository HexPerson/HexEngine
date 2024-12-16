

#pragma once

#include "../FileSystem/ResourceSystem.hpp"

namespace HexEngine
{
	class IShader;

	class ShaderSystem : public IResourceLoader
	{
	public:
		ShaderSystem();
		~ShaderSystem();

		// IResourceLoader
		//
		virtual IResource* LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;

		virtual IResource* LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;

		virtual void UnloadResource(IResource* resource) override;

		virtual std::vector<std::string> GetSupportedResourceExtensions() override;

		virtual std::wstring GetResourceDirectory() const override;

		virtual void SaveResource(IResource* resource, const fs::path& path) override {}

		void ReloadAllShaders();

	private:
		IShader* ParseShaderInternal(const fs::path& absolutePath);
		IShader* ParseShaderInternal(const std::vector<uint8_t>& data);

	private:
		std::map<fs::path, IShader*> _loadedShaders;
	};
}
