
#pragma once

#include <HexEngine.Core\HexEngine.hpp>

namespace HexEditor
{
	

	enum Overlay
	{
		Overlay_Light,
		Overlay_Count
	};

	class EditorExtension : public IGameExtension
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

		

	public:
		FileSystem* _projectFS = nullptr;
		std::shared_ptr<ITexture2D> _overlayIcons[Overlay_Count] = { nullptr };
	};

	inline EditorExtension* g_pEditor = nullptr;
}
