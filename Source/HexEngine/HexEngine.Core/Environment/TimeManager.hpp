

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class TimeManager
	{
	public:
		TimeManager();

		void FrameStart(bool hasUpdatedOnce);
		void FrameEnd();
		void SetTargetFps(uint32_t targetFps);
		float GetTime() const noexcept;
		float GetFrameTime() const noexcept;

	//private:
		float _frameTime;
		float _frameTimeMS;
		float _currentTime;
		float _frameEndTime;
		float _lastTime;
		float _accumulatedSimulationTime;
		float _simulationTime;
		float _frequency;
		float _averageFrameTimeMS;
		float _accumulatedAverageFrameTimeMS;
		bool _hasInitialised;
		__int64 _counterStart;
		__int64 _frameCount;
		float _lastFPSUpdate;
		int32_t _fps;
		int32_t _accumulatedFrames;
		bool _hasFpsUpdated;
		uint32_t _targetFps;
		float _interpolationFactor;
		bool _firstFrame = true;
	};
}
