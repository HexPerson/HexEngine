
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace HexEditor
{
	

	enum Overlay
	{
		Overlay_Light,
		Overlay_Count
	};

	class EditorExtension : public HexEngine::IGameExtension
	{
	public:
		EditorExtension();

		~EditorExtension();

		virtual void OnCreateGame();

		virtual void OnStopGame() {}

		virtual void OnLoadGameWorld() {}

		virtual void OnRegisterClasses() {}

		virtual void OnUpdate(float frameTime) {}

		virtual void OnFixedUpdate(float frameTime) {}

		virtual void OnShutdown() {}

		virtual void OnDebugGUI() {}

		virtual void OnGUI();

		virtual void OnDebugRender() {}

		virtual void OnResize(int32_t width, int32_t height) override;

		virtual std::string GetGameName() const { return "HexEngine Editor"; }

		void CreateFileSystem(const fs::path& path);

		void OnFileChangeEvent(const HexEngine::DirectoryWatchInfo& info, const HexEngine::FileChangeActionMap& fileData);
		void ConsumePendingPrefabReloads(std::vector<fs::path>& outPaths);

	public:
		HexEngine::FileSystem* _projectFS = nullptr;
		std::shared_ptr<HexEngine::ITexture2D> _overlayIcons[Overlay_Count] = { nullptr };

	private:
		std::mutex _prefabReloadMutex;
		std::vector<fs::path> _pendingPrefabReloads;
		std::unordered_set<std::wstring> _pendingPrefabReloadDedup;
	};

	inline EditorExtension* g_pEditor = nullptr;
}
