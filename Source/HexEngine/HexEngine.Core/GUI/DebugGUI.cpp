

#include "DebugGUI.hpp"
#include "../HexEngine.hpp"

namespace HexEngine
{
	DebugGUI::DebugGUI()
	{
	}

	DebugGUI::~DebugGUI()
	{
		Destroy();
	}

	void DebugGUI::Destroy()
	{
		_lock.lock();
		for (auto&& prof : _profiles)
		{
			delete prof;
		}
		_profiles.clear();
		_lock.unlock();
	}

	void DebugGUI::Resize(HWND wnd)
	{
		Destroy();
	}

	void DebugGUI::Render()
	{
		ShowOverlay();

		//ShowRenderOptions();

		for (auto&& callback : _callbacks)
		{
			callback->OnDebugGUI();
		}

		for (auto& extension : g_pEnv->GetGameExtensions())
		{
			extension->OnDebugGUI();
		}
	}

	void DebugGUI::ShowOverlay()
	{
		
		//{
		//	ImGui::Text("FPS: %d", g_pEnv->_timeManager->_fps);
		//	ImGui::Text("Time: %.3f", g_pEnv->_timeManager->_currentTime);
		//	ImGui::Text("Frame time (ms): %f", g_pEnv->_timeManager->_frameTimeMS);			
		//	ImGui::Text("Average frame time (ms): %f", g_pEnv->_timeManager->_averageFrameTimeMS);
		//	ImGui::Text("Sim time (ms): %f", g_pEnv->_timeManager->_accumulatedSimulationTime);

		//	int32_t mx, my;
		//	g_pEnv->_inputSystem->GetMousePosition(mx, my);
		//	ImGui::Text("Mouse %dx%d", mx, my);
		//	//ImGui::Text("Mouse %dx%d %s", ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
		//	//ImGui::Text("MouseLast %.0fx%.0f", ImGui::GetIO().MousePosPrev.x, ImGui::GetIO().MousePosPrev.y);
		//	//ImGui::Text("MouseLastValid %.0fx%.0f", _ctx->MouseLastValidPos.x, _ctx->MouseLastValidPos.y);
		//	

		

		//	_lock.lock();
		//	std::sort(_profiles.begin(), _profiles.end(), [](const Profiler* left, const Profiler* right) {
		//		return left->_average > right->_average;
		//		});

		//	for (auto&& prof : _profiles)
		//	{
		//		if(prof->_average > 0.0f)
		//			ImGui::Text("Profile '%s' avg. %.6f ms", prof->_func, prof->_average / (float)prof->_numProfiles);
		//	}
		//	_lock.unlock();
		//}
		//ImGui::End();
	}


	void DebugGUI::ReportProfile(const Profiler& profile)
	{
		bool shouldRecord = false;

		_lock.lock();
		for (auto&& prof : _profiles)
		{
			if (!strcmp(prof->_func, profile._func) && !strcmp(prof->_file, profile._file) && prof->_line == profile._line)
			{
				float duration = (profile._end - profile._start) * 1000.0f;				

				if (duration > FLT_EPSILON)
				{
					prof->_numProfiles++;

					prof->_average += duration;
					//prof->_average /= (float)prof->_numProfiles;

					shouldRecord = true;
				}
				_lock.unlock();
				return;
			}

		}

		if (shouldRecord)
		{
			Profiler* prof = new Profiler(profile);
			_profiles.push_back(prof);
		}

		_lock.unlock();
	}

	void DebugGUI::AddCallback(IDebugGUICallback* callback)
	{
		_callbacks.push_back(callback);
		
	}

	void DebugGUI::RemoveCallback(IDebugGUICallback* callback)
	{
		auto it = std::remove(_callbacks.begin(), _callbacks.end(), callback);

		if (it != _callbacks.end())
			_callbacks.erase(it);
	}

	void DebugGUI::ShowGizmo(Entity* entity)
	{
		_gizmoTarget = entity;

		
	}
}