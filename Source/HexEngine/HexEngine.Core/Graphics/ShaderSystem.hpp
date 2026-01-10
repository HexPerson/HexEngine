

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
		virtual std::shared_ptr<IResource>	LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual std::shared_ptr<IResource>	LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
		virtual void						OnResourceChanged(std::shared_ptr<IResource> resource) override;
		virtual void						UnloadResource(IResource* resource) override;
		virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
		virtual std::wstring				GetResourceDirectory() const override;
		virtual void						SaveResource(IResource* resource, const fs::path& path) override {}
		virtual bool						DoesSupportHotLoading() override { return true; }

		void ReloadAllShaders();

	private:
		std::shared_ptr<IShader> ParseShaderInternal(const fs::path& absolutePath);
		std::shared_ptr<IShader> ParseShaderInternal(const std::vector<uint8_t>& data);

	private:
		std::map<fs::path, std::weak_ptr<IShader>> _loadedShaders;
		std::map<fs::path, std::weak_ptr<IShader>> _hotReloadShaders;
		//std::map<fs::path, std::pair<std::weak_ptr<IShader>, std::weak_ptr<IShader>>> _hotReloadMap;
	};
}
