

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

		/**
		 * @brief Returns false if a cached .hcs at `absolutePath` cannot
		 *        satisfy the active backend - either because it doesn't
		 *        exist, fails to open, has a corrupt header, or holds blob
		 *        data in a dialect the backend can't consume.
		 *
		 * Used by callers that persist their compiled shaders to disk and
		 * want to know whether to recompile or whether the on-disk file
		 * is still usable (e.g. the material-graph compiler skipping
		 * recompilation when the cache is fresh). v1 (DXBC-only) caches
		 * become unusable when the engine boots under D3D12 - this lets
		 * the cache be invalidated and rebaked transparently.
		 */
		static bool IsCachedShaderUsable(const fs::path& absolutePath);

	private:
		std::shared_ptr<IShader> ParseShaderInternal(const fs::path& absolutePath);
		std::shared_ptr<IShader> ParseShaderInternal(const std::vector<uint8_t>& data);

	private:
		std::map<fs::path, std::weak_ptr<IShader>> _loadedShaders;
		std::map<fs::path, std::weak_ptr<IShader>> _hotReloadShaders;
		//std::map<fs::path, std::pair<std::weak_ptr<IShader>, std::weak_ptr<IShader>>> _hotReloadMap;
	};
}
