

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
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

		HANDLE CreateChangeNotifier(const fs::path& pathToWatch, std::function<void(PFILE_NOTIFY_INFORMATION)> onFileChangeCB);

		virtual void GetFileData(const fs::path& absolutePath, std::vector<uint8_t>& data);

		std::wstring GetRelativeResourcePath(const fs::path& path);

	private:
		void FileChangeMonitorThread(HANDLE handle, std::function<void(PFILE_NOTIFY_INFORMATION)> onFileChangeCB);

	private:
		std::wstring _name;
		fs::path _baseDirectory;
		fs::path _binaryDirectory;
		fs::path _dataDirectory;
		fs::path _projectDirectory;
	};
}
