

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

		// Running total of _gameFrameTime (scaled, zero while paused). Components
		// that tick every N frames measure their elapsed time with this, NOT the
		// real clock - real time kept running through every pause (tally, home
		// computer, menus) and the city routine read frozen walkers as stuck.
		// Appended last: Game.dll reads earlier fields by offset.
		double _gameTime = 0.0;
	};
}
