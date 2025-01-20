

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	struct DirectoryWatchInfo
	{
		fs::path path;
		HANDLE handle;
		HANDLE event;
	};

	class FileSystem
	{
	public:
		FileSystem(const std::wstring& name) :
			_name(name)
		{}

		const std::wstring& GetName() const { return _name; }

		virtual bool DoesAbsolutePathExist(const fs::path& path) const;

		virtual bool DoesRelativePathExist(const fs::path& path) const;

		virtual bool IsAsset() const;

		const fs::path& GetBaseDirectory() const;

		const fs::path& GetDataDirectory() const;

		const fs::path& GetBinaryDirectory() const;

		void SetBaseDirectory(const fs::path& baseDirectory);

		fs::path GetLocalAbsolutePath(const fs::path& localPath);

		virtual fs::path GetLocalAbsoluteDataPath(const fs::path& localPath);

		void CreateSubDirectories(const fs::path& absolutePath);

		bool CreateChangeNotifier(const fs::path& pathToWatch, std::function<void(PFILE_NOTIFY_INFORMATION)> onFileChangeCB=nullptr);

		virtual void GetFileData(const fs::path& absolutePath, std::vector<uint8_t>& data);

		std::wstring GetRelativeResourcePath(const fs::path& path);

	private:
		void FileChangeMonitorThread(const std::vector<DirectoryWatchInfo>& watchInfo, std::function<void(PFILE_NOTIFY_INFORMATION)> onFileChangeCB);

		void OnFileChange(PFILE_NOTIFY_INFORMATION info);

	private:
		std::wstring _name;
		fs::path _baseDirectory;
		fs::path _binaryDirectory;
		fs::path _dataDirectory;
		fs::path _projectDirectory;
	};
}
