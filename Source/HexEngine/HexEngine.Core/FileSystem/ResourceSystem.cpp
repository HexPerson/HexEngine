

#include "ResourceSystem.hpp"
#include "../HexEngine.hpp"
#include <algorithm>
#include <cwctype>

namespace
{
	fs::path NormalizeAbsoluteResourcePath(const fs::path& input)
	{
		if (input.empty())
			return {};

		fs::path normalized = input.lexically_normal().make_preferred();
#ifdef _WIN32
		std::wstring lower = normalized.wstring();
		std::transform(lower.begin(), lower.end(), lower.begin(),
			[](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
		return fs::path(lower);
#else
		return normalized;
#endif
	}
}

namespace HexEngine
{
	void ResourceDeleter::operator()(IResource* p) const
	{
		g_pEnv->GetResourceSystem().UnloadResource(p);
	}

	void ResourceSystem::Create()
	{
		for (auto i = 0; i < MaxAsyncResourceLoaders; ++i)
			_jobThread[i] = std::thread(std::bind(&ResourceSystem::JobLoader, this));
	}

	void ResourceSystem::Destroy()
	{
		_running = false;

		for(auto i = 0; i < MaxAsyncResourceLoaders; ++i)
			_jobThread[i].join();

		for (auto&& resource : _loadedResources)
		{
			LOG_DEBUG("** WARNING ** Unloaded resource: '%S' RefCount = %d", resource.first.c_str(), resource.second.use_count());
		}

		_loadedResourcesByAbsolute.clear();
	}

	void ResourceSystem::AddFileSystem(FileSystem* fileSystem)
	{
		_fileSystems.push_back(fileSystem);
	}

	void ResourceSystem::RemoveFileSystem(FileSystem* fileSystem)
	{
		_fileSystems.erase(std::remove(_fileSystems.begin(), _fileSystems.end(), fileSystem));
	}

	void ResourceSystem::RegisterResourceLoader(IResourceLoader* loader)
	{
		_resourceLoaders.push_back(loader);
	}

	void ResourceSystem::UnregisterResourceLoader(IResourceLoader* loader)
	{
		_resourceLoaders.erase(std::remove(_resourceLoaders.begin(), _resourceLoaders.end(), loader), _resourceLoaders.end());
	}

	IResourceLoader* ResourceSystem::FindResourceLoaderForExtension(const std::string& extension)
	{
		std::string lowerExtension = extension;
		std::transform(lowerExtension.begin(), lowerExtension.end(), lowerExtension.begin(), ::tolower);

		for (auto&& loader : _resourceLoaders)
		{
			auto supportedExtensions = loader->GetSupportedResourceExtensions();

			if (std::find(supportedExtensions.begin(), supportedExtensions.end(), lowerExtension) != supportedExtensions.end())
				return loader;
		}

		return nullptr;
	}

	void ResourceSystem::Update()
	{
		_resourceLoadedCallbackLock.lock();

		while (_asyncLoadedForCallback.size() > 0)
		{		
			auto& resourceToLoad = _asyncLoadedForCallback.front();

			resourceToLoad.second(resourceToLoad.first);

			_asyncLoadedForCallback.pop_front();
		}

		_resourceLoadedCallbackLock.unlock();
	}

	void ResourceSystem::JobLoader()
	{
		while (_running)
		{
			_lock.lock();
			if (_queuedResources.size() > 0)
			{
				auto resourceToLoad = _queuedResources.front();
				_queuedResources.pop_front();
				_lock.unlock();

				auto resource = LoadResource(resourceToLoad.first);

				_resourceLoadedCallbackLock.lock();
				_asyncLoadedForCallback.push_back({ resource, resourceToLoad.second });
				_resourceLoadedCallbackLock.unlock();
			}
			else
				_lock.unlock();

			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}

	bool ResourceSystem::DoesResourceExistAsAsset(const fs::path& path)
	{
		for (auto& fs : _fileSystems)
		{
			if (fs->IsAsset() == false)
				continue;

			if (fs->DoesAbsolutePathExist(path) == true)
			{
				return true;
			}
		}
		return false;
	}

	FileSystem* ResourceSystem::FindAssetFileSystemForAsset(const fs::path& path)
	{
		for (auto& fs : _fileSystems)
		{
			if (fs->IsAsset() == false)
				continue;

			if (fs->DoesAbsolutePathExist(path) == true)
			{
				return fs;
			}
		}
		return nullptr;
	}

	FileSystem* ResourceSystem::FindFileSystemByPath(const fs::path& path)
	{
		for (auto& fs : _fileSystems)
		{
			if (auto p = path.wstring().find('.'); p != std::string::npos)
			{
				if (fs->GetName() == path.wstring().substr(0, p))
					return fs;
			}
			else
			{
				auto rel = fs::relative(path, fs->GetBaseDirectory());

				if (!rel.empty() && rel.native()[0] != '.')
					return fs;

				rel = fs::relative(fs->GetName(), path);

				if (!rel.empty() && rel.native()[0] != '.')
					return fs;
			}
		}
		return nullptr;
	}

	FileSystem* ResourceSystem::FindFileSystemByName(const std::wstring& name)
	{
		for (auto& fs : _fileSystems)
		{
			if(fs->GetName() == name)
				return fs;
		}
		return nullptr;
	}

	std::shared_ptr<IResource> ResourceSystem::LoadResource(const fs::path& localPath, const ResourceLoadOptions* options /*= nullptr*/)
	{
		std::unique_lock lock(_lock);

		auto tryGetLoadedByKey = [this](const fs::path& key) -> std::shared_ptr<IResource>
		{
			if (key.empty())
				return nullptr;

			auto it = _loadedResources.find(key);
			if (it == _loadedResources.end())
				return nullptr;

			auto loaded = it->second.lock();
			if (!loaded)
			{
				_loadedResources.erase(it);
				return nullptr;
			}

			return loaded;
		};

		auto tryGetLoadedByAbsolute = [this](const fs::path& absolutePath) -> std::shared_ptr<IResource>
		{
			if (!absolutePath.is_absolute())
				return nullptr;

			const fs::path absoluteKey = NormalizeAbsoluteResourcePath(absolutePath);
			auto it = _loadedResourcesByAbsolute.find(absoluteKey);
			if (it == _loadedResourcesByAbsolute.end())
				return nullptr;

			auto loaded = it->second.lock();
			if (!loaded)
			{
				_loadedResourcesByAbsolute.erase(it);
				return nullptr;
			}

			return loaded;
		};

		if (auto loaded = tryGetLoadedByKey(localPath); loaded != nullptr)
			return loaded;

		if (localPath.is_absolute())
		{
			if (auto loaded = tryGetLoadedByAbsolute(localPath); loaded != nullptr)
				return loaded;
		}

		std::shared_ptr<IResource> resource = nullptr;

		for (auto& fs : _fileSystems)
		{
			fs::path trueLocalPath = localPath;

			
			// Deal with absolute file paths first
			if (trueLocalPath.is_absolute())
			{
				std::error_code ec;
				trueLocalPath = fs::relative(trueLocalPath, fs->GetDataDirectory(), ec);

				if (ec || trueLocalPath.empty())
					continue;

				const auto relStr = trueLocalPath.generic_string();
				if (relStr.size() >= 2 && relStr[0] == '.' && relStr[1] == '.')
					continue;

				trueLocalPath = trueLocalPath.lexically_normal();

				// do a second check for the loaded resource, as the local path might be correct now
				const fs::path fsPath = fs->GetRelativeResourcePath(trueLocalPath.wstring());
				if (auto loaded = tryGetLoadedByKey(fsPath); loaded != nullptr)
					return loaded;

				if (auto loaded = tryGetLoadedByKey(trueLocalPath); loaded != nullptr)
					return loaded;
			}
			else
			{
				// else the file path is not relative, but might use the virtual filesystem path convention
				if (auto p = trueLocalPath.wstring().find_first_of('.'); p != std::wstring::npos)
				{
					auto virtualFsName = trueLocalPath.wstring().substr(0, p);

					if (virtualFsName == fs->GetName())
					{
						trueLocalPath = trueLocalPath.wstring().substr(p + 1);
					}
					else
						continue;
				}

				// non-prefixed relative path may still map to an already-loaded resource via fs-relative key
				const fs::path fsPath = fs->GetRelativeResourcePath(trueLocalPath.wstring());
				if (auto loaded = tryGetLoadedByKey(fsPath); loaded != nullptr)
					return loaded;

				if (auto loaded = tryGetLoadedByKey(trueLocalPath); loaded != nullptr)
					return loaded;
			}

			LOG_INFO("Loading resource '%S' at file system data directory '%s'", trueLocalPath.c_str(), fs->GetDataDirectory().string().c_str());

			// else it has not been loaded, so load it!
			auto fullPath = fs->GetLocalAbsoluteDataPath(trueLocalPath);

			if (auto loaded = tryGetLoadedByAbsolute(fullPath); loaded != nullptr)
				return loaded;

			if (fs->DoesAbsolutePathExist(fullPath) == false)
			{
				LOG_WARN("Cannot load resource '%s' because the path does not exist", fullPath.string().c_str());
				continue;
			}

			std::string fileExtension = fullPath.extension().string();

			auto loader = FindResourceLoaderForExtension(fileExtension);

			if (!loader)
			{
				LOG_CRIT("No IResourceLoader is associated with the file extension '%s'!", fileExtension.c_str());
				return nullptr;
			}			



			if (fs->IsAsset())
			{
				std::vector<uint8_t> fileData;
				fs->GetFileData(fullPath, fileData);

				resource = loader->LoadResourceFromMemory(fileData, fullPath, fs, options);
			}
			else
				resource = loader->LoadResourceFromFile(fullPath, fs, options);
			
			if (!resource)
			{
				//if (options && !options->silenceErrors)
				{
					LOG_CRIT("Failed to load resource '%S'", trueLocalPath.c_str());
					CON_ECHO("^rFailed to load resource '%s'", trueLocalPath.string().c_str());
				}
				return nullptr;
			}

			resource->_loader = loader;
			resource->_fs = fs;

			// set up the paths
			resource->_fsRelativePath = fs->GetRelativeResourcePath(trueLocalPath.wstring());
			resource->_absolutePath = fullPath;
			resource->_relativePath = trueLocalPath;		
			resource->_id = _currentResourceId++;

			_loadedResources[resource->GetFileSystemPath()] = resource;
			_loadedResourcesByAbsolute[NormalizeAbsoluteResourcePath(resource->_absolutePath)] = resource;
			_idToResourceMap[resource->_id] = resource;

			return resource;
		}

		
		return nullptr;
	}

	std::shared_ptr<IResource> ResourceSystem::LoadResourceAsync(const fs::path& path, ResourceLoadedFn callback)
	{
		std::unique_lock lock(_lock);

		_queuedResources.push_back({ path, callback });

		return nullptr;
	}

	void ResourceSystem::UnloadResource(IResource* resource)
	{
		std::unique_lock lock(_lock);

		_loadedResources.erase(resource->GetFileSystemPath());
		_loadedResourcesByAbsolute.erase(NormalizeAbsoluteResourcePath(resource->GetAbsolutePath()));
		_idToResourceMap.erase(resource->GetId());

		LOG_INFO("Unloading resource '%S'", resource->GetFileSystemPath().wstring().c_str());

		if (resource->_loader)
			resource->_loader->UnloadResource(resource);
		else
		{
			LOG_WARN("Trying to unload resource '%s' but it has no associated resource loader. This will cause a memory leak.", resource->GetFileSystemPath().filename().string().c_str());
		}
	}

	std::vector<std::string> ResourceSystem::GetSupportedFileExtensions() const
	{
		std::unique_lock lock(_lock);

		std::vector<std::string> res;

		for (auto&& loader : _resourceLoaders)
		{
			auto supportedResources = loader->GetSupportedResourceExtensions();

			res.insert(res.end(), supportedResources.begin(), supportedResources.end());
		}

		return res;
	}

	std::shared_ptr<IResource> ResourceSystem::FindResourceById(ResourceId id) const
	{
		std::unique_lock lock(_lock);

		auto it = _idToResourceMap.find(id);

		if (it == _idToResourceMap.end())
			return nullptr;

		return it->second.lock();
	}

	std::shared_ptr<IResource> ResourceSystem::FindResourceByFileName(const fs::path& fileName, bool matchFileNameOnly) const
	{
		std::unique_lock lock(_lock);

		if (matchFileNameOnly)
		{
			for (auto& it : _loadedResources)
			{
				if (it.first.filename() == fileName)
				{
					return it.second.lock();
				}
			}
		}
		else
		{
			auto it = _loadedResources.find(fileName);

			if (it != _loadedResources.end())
				return it->second.lock();
		}

		return nullptr;
	}
}
