

#pragma once

#include "../Required.hpp"
#include "../Environment/Profiler.hpp"

namespace HexEngine
{
	class HEX_API IDebugGUICallback
	{
	public:
		virtual void OnDebugGUI() = 0;
	};

	class Entity;

	class HEX_API DebugGUI
	{
	public:
		DebugGUI();

		~DebugGUI();

		void Destroy();

		void Resize(HWND wnd);

		void Render();

		void ReportProfile(const Profiler& profile);

		void AddCallback(IDebugGUICallback* callback);

		void RemoveCallback(IDebugGUICallback* callback);

		void ShowGizmo(Entity* entity);

	private:
		void ShowOverlay();

		void ShowRenderOptions();

	private:
		std::vector<Profiler*> _profiles;
		std::vector<IDebugGUICallback*> _callbacks;
		bool _uiActive = false;

		std::recursive_mutex _lock;

		Entity* _gizmoTarget = nullptr;
	};
}
