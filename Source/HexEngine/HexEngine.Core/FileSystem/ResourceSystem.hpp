

#pragma once

#include "IResource.hpp"

namespace HexEngine
{
	using ResourceLoadedFn = std::function<void(IResource*)>;
	const int32_t MaxAsyncResourceLoaders = 4;

	class FileSystem;
	class Dialog;
	

	struct ModelLoadOptions : ResourceLoadOptions
	{
		bool loadLights = false;
	};

	class IResourceLoader
	{
	public:
		virtual IResource* LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) = 0;

		virtual IResource* LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) = 0;

		virtual void UnloadResource(IResource* resource) = 0;

		virtual std::vector<std::string> GetSupportedResourceExtensions() = 0;

		virtual std::wstring GetResourceDirectory() const = 0;

		virtual Dialog* CreateEditorDialog(const fs::path& path, FileSystem* fileSystem) {
			return nullptr;
		}

		virtual void SaveResource(IResource* resource, const fs::path& path) = 0;
	};

	

	class ResourceSystem
	{
	public:
		void Create();
		void Destroy();
		void Update();

		void							AddFileSystem(FileSystem* fileSystem);
		void							RemoveFileSystem(FileSystem* fileSystem);
		const std::vector<FileSystem*>& GetFileSystems() const { return _fileSystems; }

		// Resource loader methods
		//
		void				RegisterResourceLoader(IResourceLoader* loader);
		void				UnregisterResourceLoader(IResourceLoader* loader);
		IResourceLoader*	FindResourceLoaderForExtension(const std::string& extension);

		// Methods responsible for loading resources
		//
		IResource*	LoadResource(const fs::path& path, const ResourceLoadOptions* options = nullptr);
		void		UnloadResource(IResource* resource);
		IResource*	LoadResourceAsync(const fs::path& path, ResourceLoadedFn callback);

		std::vector<std::string> GetSupportedFileExtensions();

		bool		DoesResourceExistAsAsset(const fs::path& path);
		FileSystem* FindAssetFileSystemForAsset(const fs::path& path);

	private:
		void JobLoader();

	private:
		std::vector<FileSystem*> _fileSystems;
		std::vector<IResourceLoader*> _resourceLoaders;

		std::map<fs::path, IResource*> _loadedResources;
		std::list<std::pair<fs::path, ResourceLoadedFn>> _queuedResources;
		std::list<std::pair<IResource*, ResourceLoadedFn>> _asyncLoadedForCallback;
		std::thread _jobThread[MaxAsyncResourceLoaders];
		std::recursive_mutex _lock;
		std::mutex _resourceLoadedCallbackLock;
		bool _running = true;
	};
}
