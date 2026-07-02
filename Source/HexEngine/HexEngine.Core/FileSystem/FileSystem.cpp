

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

		// Modern, additive DLL search policy - replaces the blunt process-wide
		// SetDllDirectoryW, which owned a single legacy "DllDirectory" slot (so a
		// later caller silently overrode ours) and dropped the current directory
		// unpredictably. We opt the process into the secure search set ONCE
		// (application dir + System32 + explicitly-registered user dirs; this
		// also mitigates CWD/%PATH% DLL-planting) and then register the binary
		// directory ADDITIVELY. AddDllDirectory entries stack and are honoured by
		// every LoadLibrary(Ex) in the process, so multiple mounted file systems
		// each contribute their Bin dir without stomping one another.
		static const bool s_secureDllPolicy = []()
		{
			SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			return true;
		}();
		(void)s_secureDllPolicy;

		if (!_binaryDirectory.empty())
			AddDllDirectory(_binaryDirectory.wstring().c_str());
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

	FileSystem::~FileSystem()
	{
		StopAllWatches();
	}

	void FileSystem::StopAllWatches()
	{
		for (auto& watch : _watches)
		{
			if (!watch)
				continue;

			// Wake the monitor thread out of its wait, then join it (RAII - no
			// detached threads outliving the FileSystem). Handles are closed
			// here, only after the thread has fully stopped touching them.
			if (watch->stopEvent)
				SetEvent(watch->stopEvent);

			if (watch->thread.joinable())
				watch->thread.join();

			if (watch->completionEvent)
				CloseHandle(watch->completionEvent);
			if (watch->stopEvent)
				CloseHandle(watch->stopEvent);
			if (watch->dirHandle != INVALID_HANDLE_VALUE)
				CloseHandle(watch->dirHandle);
		}
		_watches.clear();
	}

	bool FileSystem::CreateChangeNotifier(const fs::path& pathToWatch, std::function<void(const DirectoryWatchInfo&, const FileChangeActionMap&)> onFileChangeCB)
	{
		if (fs::is_directory(pathToWatch) == false)
		{
			LOG_WARN("Cannot place a file change watch on a path that is not a directory");
			return false;
		}

		auto watch = std::make_unique<WatchContext>();
		watch->path = pathToWatch;
		watch->callback = std::move(onFileChangeCB);

		// FILE_FLAG_OVERLAPPED so the read is asynchronous and can be cancelled
		// (stop event + CancelIo). A synchronous ReadDirectoryChangesW would
		// block the thread indefinitely and make join() at shutdown hang.
		watch->dirHandle = CreateFileW(
			pathToWatch.wstring().c_str(),
			FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);

		if (watch->dirHandle == INVALID_HANDLE_VALUE)
		{
			LOG_CRIT("Could not create file change notification handle. Error: %d", GetLastError());
			return false;
		}

		watch->completionEvent = CreateEventW(NULL, FALSE, FALSE, NULL); // auto-reset
		watch->stopEvent       = CreateEventW(NULL, TRUE,  FALSE, NULL); // manual-reset

		if (!watch->completionEvent || !watch->stopEvent)
		{
			LOG_CRIT("Could not create file change event handles. Error: %d", GetLastError());
			if (watch->completionEvent)
				CloseHandle(watch->completionEvent);
			if (watch->stopEvent)
				CloseHandle(watch->stopEvent);
			CloseHandle(watch->dirHandle);
			return false;
		}

		LOG_INFO("Created file change watch in directory: %s", pathToWatch.string().c_str());

		WatchContext* watchPtr = watch.get();
		_watches.push_back(std::move(watch));

		// Owned, NOT detached: the destructor signals stopEvent and joins.
		watchPtr->thread = std::thread(&FileSystem::FileChangeMonitorThread, this, watchPtr);

		return true;
	}

	void FileSystem::FileChangeMonitorThread(WatchContext* watch)
	{
		// https://github.com/tresorit/rdcfswatcherexample/blob/master/rdc_fs_watcher.cpp
		constexpr DWORD flags = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
			| FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION
			| FILE_NOTIFY_CHANGE_SECURITY;

		// RAII buffer - can't leak on an early continue/break. The original
		// new[]/delete[] leaked this buffer on every ReadDirectoryChangesW error
		// (the failure path `continue`d past the delete).
		std::vector<uint8_t> buffer(0x102400);

		while (true)
		{
			OVERLAPPED overlapped = {};
			overlapped.hEvent = watch->completionEvent;

			const BOOL res = ReadDirectoryChangesW(
				watch->dirHandle,
				buffer.data(),
				(DWORD)buffer.size(),
				TRUE /* bWatchSubtree */,
				flags,
				nullptr /* lpBytesReturned - ignored for async reads */,
				&overlapped,
				nullptr);

			if (!res)
			{
				LOG_CRIT("ReadDirectoryChangesW error %d", GetLastError());
				break;
			}

			// Block until the read completes OR we're asked to stop.
			const HANDLE waitHandles[2] = { watch->completionEvent, watch->stopEvent };
			const DWORD  waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

			if (waitResult != WAIT_OBJECT_0)
			{
				// stopEvent (WAIT_OBJECT_0 + 1) or a wait failure: cancel the
				// pending read and exit the loop.
				CancelIo(watch->dirHandle);
				break;
			}

			DWORD bytesTransferred = 0;
			if (!GetOverlappedResult(watch->dirHandle, &overlapped, &bytesTransferred, FALSE) || bytesTransferred == 0)
			{
				// 0 bytes => the change set overflowed the buffer; nothing safely
				// parseable this round, just re-arm the watch.
				continue;
			}

			FileChangeActionMap events;

			FILE_NOTIFY_INFORMATION* event = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());

			for (;;)
			{
				DWORD name_len = event->FileNameLength / sizeof(wchar_t);

				std::wstring fileName(event->FileName, event->FileName + name_len);
				fs::path filePath = watch->path / fileName;

				FileChangeInfo changeInfo;
				changeInfo.path = filePath;

				events[event->Action].push_back(changeInfo);

				// Are there more events to handle?
				if (event->NextEntryOffset != 0)
					event = reinterpret_cast<PFILE_NOTIFY_INFORMATION>(reinterpret_cast<uintptr_t>(event) + event->NextEntryOffset);
				else
					break;
			}

			DirectoryWatchInfo info;
			info.path   = watch->path;
			info.handle = watch->dirHandle;
			info.event  = watch->completionEvent;

			OnFileChange(info, events);

			if (watch->callback)
				watch->callback(info, events);
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
					auto loader = g_pEnv->GetResourceSystem().FindResourceLoaderForExtension(fileInfo.path.extension().string());
					auto resource = g_pEnv->GetResourceSystem().FindResourceByFileName(fileInfo.path.filename(), true);

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
