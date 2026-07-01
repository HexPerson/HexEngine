

#pragma once

#include "../Required.hpp"

#include <thread>
#include <memory>
#include <functional>
#include <vector>

namespace HexEngine
{
	struct DirectoryWatchInfo
	{
		fs::path path;
		HANDLE handle;
		HANDLE event;
	};

	struct FileChangeInfo
	{
		fs::path path;
	};

	using FileChangeActionMap = std::map<uint32_t, std::vector<FileChangeInfo>>;

	class HEX_API FileSystem
	{
	public:
		FileSystem(const std::wstring& name) :
			_name(name)
		{}

		// Owns its directory-watch threads + Win32 handles, so it can't be
		// copied. The destructor stops and joins every watcher (RAII) rather
		// than leaking detached threads.
		~FileSystem();
		FileSystem(const FileSystem&) = delete;
		FileSystem& operator=(const FileSystem&) = delete;

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

		bool CreateChangeNotifier(const fs::path& pathToWatch, std::function<void(const DirectoryWatchInfo&, const FileChangeActionMap&)> onFileChangeCB=nullptr);

		virtual void GetFileData(const fs::path& absolutePath, std::vector<uint8_t>& data);

		std::wstring GetRelativeResourcePath(const fs::path& path);

	private:
		// One owned watcher: a cancellable overlapped ReadDirectoryChangesW loop
		// on its own thread. The worker only READS these handles for its
		// lifetime; StopAllWatches (main thread, after join) is the sole owner
		// that closes them, so there's no cross-thread race on handle lifetime.
		// Defined here (not the .cpp) because the dllexported FileSystem embeds a
		// std::vector<std::unique_ptr<WatchContext>>, which every including TU
		// must be able to instantiate - so the type has to be complete.
		struct WatchContext
		{
			fs::path    path;
			HANDLE      dirHandle       = INVALID_HANDLE_VALUE;
			HANDLE      completionEvent = nullptr;   // OVERLAPPED completion (auto-reset)
			HANDLE      stopEvent       = nullptr;   // manual-reset; set to cancel the watch
			std::function<void(const DirectoryWatchInfo&, const FileChangeActionMap&)> callback;
			std::thread thread;
		};

		void FileChangeMonitorThread(WatchContext* watch);

		void OnFileChange(const DirectoryWatchInfo& watchInfo, const FileChangeActionMap& fileInfo);

		// Signal every watcher's stop event and join its thread. Called by the
		// destructor; idempotent.
		void StopAllWatches();

	private:
		std::wstring _name;
		fs::path _baseDirectory;
		fs::path _binaryDirectory;
		fs::path _dataDirectory;
		fs::path _projectDirectory;

		std::vector<std::unique_ptr<WatchContext>> _watches;
	};
}
