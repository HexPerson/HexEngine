

#include "FileSystem.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Environment/LogFile.hpp"

namespace HexEngine
{
	bool FileSystem::DoesAbsolutePathExist(const fs::path& path) const
	{
		return fs::exists(path);
	}

	bool FileSystem::DoesRelativePathExist(const fs::path& path) const
	{
		fs::path fullPath = GetBaseDirectory();
		fullPath += path;

		return fs::exists(fullPath);
	}

	bool FileSystem::IsAsset() const
	{
		return false;
	}

	void FileSystem::SetBaseDirectory(const fs::path& baseDir)
	{
		_baseDirectory = baseDir;

		// Set the directory containing all our various dependencies
		_binaryDirectory = _baseDirectory;
		_binaryDirectory /= "Bin\\";

		if (!fs::exists(_binaryDirectory))
			fs::create_directory(_binaryDirectory);

		// Set the directory containing all our game data
		_dataDirectory = _baseDirectory;
		_dataDirectory /= "Data\\";

		if (!fs::exists(_dataDirectory))
			fs::create_directory(_dataDirectory);

		SetDllDirectoryW(_binaryDirectory.wstring().c_str());
	}

	const fs::path& FileSystem::GetBaseDirectory() const
	{
		return _baseDirectory;
	}

	const fs::path& FileSystem::GetDataDirectory() const
	{
		return _dataDirectory;
	}

	const fs::path& FileSystem::GetBinaryDirectory() const
	{
		return _binaryDirectory;
	}

	fs::path FileSystem::GetLocalAbsolutePath(const fs::path& localPath)
	{
		auto baseDir = GetBaseDirectory();

		baseDir /= localPath;

		return baseDir;
	}

	fs::path FileSystem::GetLocalAbsoluteDataPath(const fs::path& localPath)
	{
		auto localPathCopy = localPath.wstring();

		if (auto p = localPathCopy.find(GetName() + L"."); p != std::wstring::npos)
		{
			localPathCopy = localPathCopy.substr(p + 1 + GetName().length());
		}
		auto baseDir = GetDataDirectory();

		baseDir /= localPathCopy;

		return baseDir;
	}

	void FileSystem::CreateSubDirectories(const fs::path& absolutePath)
	{
		auto pathOnly = absolutePath;
		pathOnly.remove_filename();

		fs::create_directories(pathOnly);
	}

	void FileSystem::GetFileData(const fs::path& absolutePath, std::vector<uint8_t>& data)
	{
		DiskFile file(absolutePath, std::ios::in | std::ios::binary);

		if (!file.Open())
			return;

		file.ReadAll(data);
		file.Close();
	}

	bool FileSystem::CreateChangeNotifier(const fs::path& pathToWatch, std::function<void(const DirectoryWatchInfo&, const FileChangeActionMap&)> onFileChangeCB)
	{
		if (fs::is_directory(pathToWatch) == false)
		{
			LOG_WARN("Cannot place a file change watch on a path that is not a directory");
			return INVALID_HANDLE_VALUE;
		}

		auto createWatchInfo = [](const fs::path& path, DirectoryWatchInfo& info) -> bool
			{
				info.handle = CreateFileW(
					path.wstring().c_str(),
					FILE_LIST_DIRECTORY,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
					FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);

				if (info.handle == INVALID_HANDLE_VALUE)
				{
					LOG_CRIT("Could not create file change notifcation handle. Error: %d", GetLastError());
					return false;
				}

				info.event = CreateEvent(NULL, FALSE, 0, NULL);

				if (info.event == INVALID_HANDLE_VALUE)
				{
					LOG_CRIT("Could not create file change event handle. Error: %d", GetLastError());
					return false;
				}

				info.path = path;

				LOG_INFO("Created file change watch in directory: %s", path.string().c_str());

				return true;
			};

		DirectoryWatchInfo info;
		if (createWatchInfo(pathToWatch, info) == false)
		{
			LOG_CRIT("Change watch creation failed");
			return false;
		}		

		std::vector<DirectoryWatchInfo> pathsToWatch;
		pathsToWatch.push_back(info);
		
		std::thread notifyThread(std::bind(&FileSystem::FileChangeMonitorThread, this, pathsToWatch, onFileChangeCB));
		notifyThread.detach();

		return true;
	}

	void FileSystem::FileChangeMonitorThread(const std::vector<DirectoryWatchInfo>& pathsToWatch, std::function<void(const DirectoryWatchInfo&, const FileChangeActionMap&)> onFileChangeCB)
	{
		// https://github.com/tresorit/rdcfswatcherexample/blob/master/rdc_fs_watcher.cpp
		while (g_pEnv->IsRunning())
		{
			constexpr DWORD flags = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
				| FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION
				| FILE_NOTIFY_CHANGE_SECURITY;

			for (auto& info : pathsToWatch)
			{

				const uint32_t bufferSize = 0x10240;
				uint8_t* buffer = new uint8_t[bufferSize];

				OVERLAPPED overlapped;
				overlapped.hEvent = info.event;

				const BOOL res = ReadDirectoryChangesW(
					info.handle,
					buffer,
					bufferSize,
					true /* bWatchSubtree */,
					flags,
					nullptr /* lpBytesReturned */,
					&overlapped,
					nullptr /* lpCompletionRoutine */);

				DWORD result = WaitForSingleObjectEx(overlapped.hEvent, INFINITE, TRUE);

				if (result == WAIT_OBJECT_0) {
					DWORD bytes_transferred;
					GetOverlappedResult(info.handle, &overlapped, &bytes_transferred, FALSE);

					FileChangeActionMap events;

					FILE_NOTIFY_INFORMATION* event = (FILE_NOTIFY_INFORMATION*)buffer;

					for (;;) {
						DWORD name_len = event->FileNameLength / sizeof(wchar_t);						

						std::wstring fileName(event->FileName, event->FileName + name_len);
						fs::path filePath = info.path / fileName;

						FileChangeInfo changeInfo;
						changeInfo.path = filePath;

						if (auto it = events.find(event->Action); it != events.end())
						{
							it->second.push_back(changeInfo);
						}
						else
						{
							events[event->Action].push_back(changeInfo);
						}

						// Are there more events to handle?
						if (event->NextEntryOffset) {
							*((uint8_t**)&event) += event->NextEntryOffset;
						}
						else {
							break;
						}
					}

					OnFileChange(info, events);

					if (onFileChangeCB)
						onFileChangeCB(info, events);
				}

				SAFE_DELETE_ARRAY(buffer);
			}
			//std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}

		for (auto& info : pathsToWatch)
		{
			CloseHandle(info.event);
			CloseHandle(info.handle);
		}
	}

	void FileSystem::OnFileChange(const DirectoryWatchInfo& watchInfo, const FileChangeActionMap& actionMap)
	{
		for (auto& actions : actionMap)
		{
			if (actions.first == FILE_ACTION_MODIFIED)
			{
				for (auto& fileInfo : actions.second)
				{
					auto loader = g_pEnv->_resourceSystem->FindResourceLoaderForExtension(fileInfo.path.extension().string());
					auto resource = g_pEnv->_resourceSystem->FindResourceByFileName(fileInfo.path.filename(), true);

					if (loader && resource)
					{
						loader->OnResourceChanged(resource);
					}
				}
			}
		}
	}

	std::wstring FileSystem::GetRelativeResourcePath(const fs::path& path)
	{
		return GetName() + L"." + path.wstring();
	}
}