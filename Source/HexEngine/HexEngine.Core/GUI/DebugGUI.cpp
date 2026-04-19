

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
		if (!_profilingEnabled)
			return;

		_lock.lock();
		std::sort(_profiles.begin(), _profiles.end(), [](const Profiler* left, const Profiler* right) {
			//float avg1 = (left->_average / (float)left->_numProfiles);
			//float avg2 = (right->_average / (float)right->_numProfiles);
			return left->_peak > right->_peak;
			});

		int32_t c = 0;

		for (auto&& prof : _profiles)
		{
			if (prof->_average > 0.0f)
			{
				float avg = (prof->_average / (float)prof->_numProfiles);

				if (avg < 0.01f)
					continue;

				std::wstring func(prof->_func.begin(), prof->_func.end());

				g_pEnv->GetUIManager().GetRenderer()->PrintText(
					g_pEnv->GetUIManager().GetRenderer()->_style.font.get(),
					(uint8_t)Style::FontSize::Small,
					650, 100 + 18 * c,
					math::Color(1, 1, 1, 1),
					FontAlign::None,
					std::format(L"Profile '{}:{}' peak: {:.2f} ms, avg: {:.2f} ms", func, prof->_line, prof->_peak, avg),
					TextEffectSettings::Shadow());
				++c;
			}

		}
		_lock.unlock();
	}


	void DebugGUI::ReportProfile(const Profiler& profile)
	{
		if (!_profilingEnabled)
			return;

		bool shouldRecord = true;

		_lock.lock();
		for (auto&& prof : _profiles)
		{
			if (prof->_func == profile._func && prof->_file == profile._file && prof->_line == profile._line)
			{
				shouldRecord = false;

				float duration = (profile._end - profile._start) * 1000.0f;				

				if (duration > FLT_EPSILON)
				{
					prof->_numProfiles++;

					prof->_average += duration;

					if (duration > prof->_peak)
						prof->_peak = duration;
				
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