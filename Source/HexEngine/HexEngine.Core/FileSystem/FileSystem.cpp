

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

	HANDLE FileSystem::CreateChangeNotifier(const fs::path& pathToWatch, std::function<void(PFILE_NOTIFY_INFORMATION)> onFileChangeCB)
	{
		HANDLE dwChangeHandles = CreateFileW(
			pathToWatch.wstring().c_str(),
			FILE_LIST_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);

		if (dwChangeHandles == INVALID_HANDLE_VALUE)
		{
			LOG_CRIT("Could not create file change notifcation handle. Error: %d", GetLastError());
			return INVALID_HANDLE_VALUE;
		}

		std::thread notifyThread(std::bind(&FileSystem::FileChangeMonitorThread, this, dwChangeHandles, onFileChangeCB));
		notifyThread.detach();

		return dwChangeHandles;
	}

	void FileSystem::FileChangeMonitorThread(HANDLE handle, std::function<void(PFILE_NOTIFY_INFORMATION)> onFileChangeCB)
	{
		// https://github.com/tresorit/rdcfswatcherexample/blob/master/rdc_fs_watcher.cpp
		//while (1)
		//{
		//	constexpr DWORD flags = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
		//		| FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION
		//		| FILE_NOTIFY_CHANGE_SECURITY;

		//	const BOOL res = ReadDirectoryChangesW(handle, &this->notifBuffer,
		//		static_cast<DWORD>(sizeof(this->notifBuffer)), true /* bWatchSubtree */, flags,
		//		nullptr /* lpBytesReturned */, this->overlapped.get(), nullptr /* lpCompletionRoutine */);

		//	switch (status)
		//	{
		//	case WAIT_OBJECT_0:
		//	{
		//		ReadDirectoryChangesW()
		//	}
		//}
	}

	std::wstring FileSystem::GetRelativeResourcePath(const fs::path& path)
	{
		return GetName() + L"." + path.wstring();
	}
}