

#pragma once

#include "IResource.hpp"
#include "IResourceLoader.hpp"

namespace HexEngine
{
	using ResourceLoadedFn = std::function<void(std::shared_ptr<IResource>)>;
	const int32_t MaxAsyncResourceLoaders = 4;

	class FileSystem;
	class Dialog;
	

	struct ModelLoadOptions : ResourceLoadOptions
	{
		bool loadLights = false;
	};

	struct HEX_API ResourceDeleter
	{
		void operator()(IResource* p) const;
	};	

	class HEX_API ResourceSystem
	{
	public:
		void Create();
		void Destroy();
		void Update();

		void							AddFileSystem(FileSystem* fileSystem);
		void							RemoveFileSystem(FileSystem* fileSystem);
		const std::vector<FileSystem*>& GetFileSystems() const { return _fileSystems; }
		FileSystem*						FindFileSystemByPath(const fs::path& path);
		FileSystem*						FindFileSystemByName(const std::wstring& name);

		// Resource loader methods
		//
		void							RegisterResourceLoader(IResourceLoader* loader);
		void							UnregisterResourceLoader(IResourceLoader* loader);
		IResourceLoader*				FindResourceLoaderForExtension(const std::string& extension);

		// Methods responsible for loading resources
		//
		std::shared_ptr<IResource>		LoadResource(const fs::path& path, const ResourceLoadOptions* options = nullptr);
		void							UnloadResource(IResource* resource);
		std::shared_ptr<IResource>		LoadResourceAsync(const fs::path& path, ResourceLoadedFn callback);
		std::shared_ptr<IResource>		FindResourceById(ResourceId id) const;
		std::shared_ptr<IResource>		FindResourceByFileName(const fs::path& fileName, bool matchFileNameOnly = false) const;

		std::vector<std::string>		GetSupportedFileExtensions() const;

		bool							DoesResourceExistAsAsset(const fs::path& path);
		FileSystem*						FindAssetFileSystemForAsset(const fs::path& path);

	private:
		void JobLoader();

	private:
		std::vector<FileSystem*> _fileSystems;
		std::vector<IResourceLoader*> _resourceLoaders;

		std::map<fs::path, std::weak_ptr<IResource>> _loadedResources;
		std::map<ResourceId, std::weak_ptr<IResource>> _idToResourceMap;
		std::list<std::pair<fs::path, ResourceLoadedFn>> _queuedResources;
		std::list<std::pair<std::shared_ptr<IResource>, ResourceLoadedFn>> _asyncLoadedForCallback;
		std::thread _jobThread[MaxAsyncResourceLoaders];
		mutable std::recursive_mutex _lock;
		std::mutex _resourceLoadedCallbackLock;
		bool _running = true;
		ResourceId _currentResourceId = 1;
	};
}
