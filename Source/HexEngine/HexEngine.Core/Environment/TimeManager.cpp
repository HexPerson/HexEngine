

#include "TimeManager.hpp"
#include "LogFile.hpp"
#include "IEnvironment.hpp"

#include <time.h>
#include <Windows.h>

namespace HexEngine
{
	using TimeDuration = std::chrono::duration<double, std::nano>;
	using Clock = std::chrono::high_resolution_clock;
	using TimePointC = std::chrono::time_point<Clock, TimeDuration>;

	TimeManager::TimeManager() :
		_currentTime(0.0f),
		_frameTime(0.0f),
		_lastTime(0.0f),
		_accumulatedSimulationTime(0.0f),
		_simulationTime(0.0f),
		_hasInitialised(false),
		_frequency(0.0f),
		_counterStart(0),
		_frameCount(0),
		_lastFPSUpdate(0.0f),
		_accumulatedFrames(0),
		_frameTimeMS(0.0f),
		_hasFpsUpdated(false),
		_frameEndTime(0.0f),
		_targetFps(0),
		_interpolationFactor(0.0),
		_averageFrameTimeMS(0.0f),
		_accumulatedAverageFrameTimeMS(0.0f)
	{}

	void TimeManager::FrameStart(bool hasUpdatedOnce)
	{
		if (_hasInitialised == false)
		{
			LARGE_INTEGER li;
			QueryPerformanceFrequency(&li);

			_frequency = float(li.QuadPart) / 1000.0f;

			QueryPerformanceCounter(&li);

			_counterStart = li.QuadPart;

			_hasInitialised = true;
		}
		_currentTime = GetTime();

		if (_lastTime == 0.0f)
			_lastTime = _currentTime;

		_hasFpsUpdated = false;

		if (hasUpdatedOnce)
		{
			++_frameCount;

			if (_currentTime - _lastFPSUpdate >= 1.0f)
			{
				if (_accumulatedAverageFrameTimeMS > 0.0f && _accumulatedFrames > 0)
					_averageFrameTimeMS = _accumulatedAverageFrameTimeMS / (float)_accumulatedFrames;
				else
					_averageFrameTimeMS = 0.0f;

				_fps = _accumulatedFrames;
				_accumulatedFrames = 0;
				_lastFPSUpdate = _currentTime;
				_hasFpsUpdated = true;

				_accumulatedAverageFrameTimeMS = 0.0f;
			}
			else
			{
				++_accumulatedFrames;
			}
		}

		_frameTime = _firstFrame ? 0.0f : _currentTime - _lastTime;
		_lastTime = _currentTime;

		_firstFrame = false;

		_frameTimeMS = _frameTime * 1000.0f;

		_accumulatedAverageFrameTimeMS += _frameTimeMS;

		_accumulatedSimulationTime += _frameTime;

		//if (g_pEnv->GetHasFocus() == false)
		//{
		//	//_frameCount = 0;

		//	//_currentTime = 0.0f;
		//	_lastTime = 0.0f;

		//	_frameTimeMS = 0.0f;

		//	_accumulatedAverageFrameTimeMS = 0.0f;
		//	_accumulatedFrames = 0;
		//	_lastFPSUpdate = 0.0f;
		//	//_accumulatedSimulationTime = 0.0f;

		//	_fps = 0;
		//}

		// clamp the accumulated time to 5 seconds
		if (_accumulatedSimulationTime >= 5.0f)
		{
			LOG_DEBUG("Very high simulation-time detected (%.3f). This is usually a CPU-bound bottleneck somewhere in the Update or Render chain", _accumulatedSimulationTime);

			_accumulatedSimulationTime = 5.0f;			
		}

		/*if (_frameTime <= (1.0f / 60.0f))
		{
			float delta = (1.0f / 60.0f) - _frameTime;

			chrono::duration<double, milli> delta_ms(delta * 1000.0);
			auto delta_ms_duration = chrono::duration_cast<chrono::milliseconds>(delta_ms);

			this_thread::sleep_for(chrono::milliseconds(delta_ms_duration.count()));
		}*/


	}

	float TimeManager::GetTime() const noexcept
	{
		LARGE_INTEGER li;
		QueryPerformanceCounter(&li);

		float currentTime = float(li.QuadPart - _counterStart) / _frequency;
		currentTime *= 0.001f;

		

		return currentTime;
	}

	float TimeManager::GetFrameTime() const noexcept
	{
		return _frameTime;
	}

	void TimeManager::FrameEnd()
	{
		_frameEndTime = GetTime();

		if (_targetFps > 0)
		{
			double targetFrameTime = 1.0f / (double)_targetFps;

			double actualFrameDuration = _frameEndTime - _currentTime;

			double nextFrameTime = _currentTime + targetFrameTime;

			//char buf[256];
			//sprintf_s(buf, "Target Frame Time: %f\nActual Frame Duration: %f\n", targetFrameTime*1000.0, actualFrameDuration*1000.0);

			//OutputDebugString(buf);

			while (GetTime() < nextFrameTime)
			{
				double sleepTime = ((nextFrameTime - GetTime()) * 100.0);

				/*sprintf_s(buf, "Sleeping for: %f\n", sleepTime);

				OutputDebugString(buf);*/

				//Sleep((DWORD)sleepTime);

				std::chrono::duration<double, std::milli> delta_ms(sleepTime);
				auto delta_ms_duration = std::chrono::duration_cast<std::chrono::milliseconds>(delta_ms);

				std::this_thread::sleep_for(std::chrono::milliseconds(delta_ms_duration.count()));
			}

			/*printf("Frame duration: %f\n", actualFrameDuration);

			if (actualFrameDuration < targetFrameTime && actualFrameDuration > 0.0)
			{
				double sleepTime = (targetFrameTime - actualFrameDuration) * 1000.0;

				std::chrono::duration<double, std::milli> delta_ms(sleepTime);
				auto delta_ms_duration = std::chrono::duration_cast<std::chrono::milliseconds>(delta_ms);

				std::this_thread::sleep_for(std::chrono::milliseconds(delta_ms_duration.count()));
			}
			else if (actualFrameDuration == 0.0)
				printf("Frame duration is 0!\n");*/
		}
	}

	void TimeManager::SetTargetFps(uint32_t targetFps)
	{
		_targetFps = targetFps;
	}
}