

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class HEX_API TimeManager
	{
	public:
		TimeManager();

		void FrameStart(bool hasUpdatedOnce);
		void FrameEnd();
		void SetTargetFps(uint32_t targetFps);
		float GetTime() const noexcept;
		float GetFrameTime() const noexcept;
		// Frame time as GAMEPLAY sees it: scaled by _timeScale, 0 while the
		// simulation is paused (except on a frame that runs requested steps).
		// UI, input and render temporal effects keep using GetFrameTime().
		float GetGameFrameTime() const noexcept { return _gameFrameTime; }

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

		// Simulation control (editor play mode: pause / step / slow-motion, e.g.
		// driven by the MCP editor bridge). Only honoured while the game is
		// running; reset to defaults otherwise so edit mode never freezes.
		// Paused = no fixed ticks and zero game frame time; each requested step
		// runs exactly one fixed tick (cl_simulationRate) on the next frame.
		bool _simulationPaused = false;
		float _timeScale = 1.0f;
		uint32_t _pendingSimulationSteps = 0;
		float _gameFrameTime = 0.0f;
	};
}
