
#pragma once

#include "../HexEngine.hpp"
#include <deque>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace HexEngine
{
	struct IconDiskCacheEntry
	{
		std::string cacheFileName;
		int64_t sourceWriteTime = 0;
		uintmax_t sourceFileSize = 0;
	};

	struct IconPending
	{
		enum class State
		{
			Queued,
			LoadingMeshAsync,
			ReadyToRender,
			Failed
		};

		bool isAsset = false;
		fs::path path;
		std::wstring assetPackage;
		std::string extensionLower;
		State state = State::Queued;
		std::shared_ptr<Mesh> loadedMesh;
		uint8_t asyncLoadAttempts = 0;
	};
	class HEX_API IconService
	{
	public:
		void Create(const std::wstring& sceneName);

		void Destroy();

		void PushFilePathForIconGeneration(const fs::path& path);
		void PushAssetPathForIconGeneration(const std::wstring& assetPackage, const fs::path& assetPath);

		ITexture2D* GetIcon(const fs::path& path);
		void RemoveIcon(const fs::path& path);

		void Render();
		void CompletedFrame();

	private:
		void ClearPreviewEntities();
		void BeginAsyncMeshLoad(IconPending& pending);
		void PumpAsyncMeshLoads();
		IconPending* FindPendingByPath(const fs::path& path);
		void RemovePendingByPath(const fs::path& path);
		bool PopNextRenderablePending(IconPending& outPending);
		void TouchIconLru(const fs::path& path);
		void EnforceIconMemoryBudget();
		void SaveIconToDiskCache(const fs::path& sourcePath, ITexture2D* texture);
		bool TryLoadIconFromDiskCache(const fs::path& sourcePath);
		void RemoveIconFromDiskCache(const fs::path& sourcePath);
		void LoadDiskCacheIndex();
		void SaveDiskCacheIndex();
		static int64_t GetFileWriteTimeTicks(const fs::path& path);
		static uintmax_t GetFileSizeSafe(const fs::path& path);
		static std::string BuildCacheFileName(const fs::path& normalizedPath);

		std::deque<IconPending> _pendingPaths;
		std::unordered_set<fs::path> _queuedPaths;
		std::vector<fs::path> _generatedPaths;
		std::map<fs::path, ITexture2D*> _icons;
		std::map<fs::path, std::shared_ptr<ITexture2D>> _resourceIcons;
		std::list<fs::path> _iconLru;
		std::unordered_map<fs::path, std::list<fs::path>::iterator> _iconLruIndex;
		std::unordered_map<fs::path, IconDiskCacheEntry> _diskCacheIndex;
		fs::path _diskCacheRoot;
		fs::path _diskCacheIndexFile;
		bool _diskCacheDirty = false;
		

		std::shared_ptr<Scene> _iconScene;
		Camera* _camera = nullptr;
		std::vector<Entity*> _previewRootEntities;
	};
}
