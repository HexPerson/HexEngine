

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	class IGameExtension
	{
	public:
		/// <summary>
		/// Called when the envionment is fully prepared and its time to start creating the game objects
		/// </summary>
		virtual void OnCreateGame() = 0;

		virtual void OnStopGame() = 0;

		virtual void OnLoadGameWorld() = 0;

		virtual void OnRegisterClasses() = 0;

		virtual void OnUpdate(float frameTime) = 0;

		virtual void OnFixedUpdate(float frameTime) = 0;

		virtual void OnShutdown() = 0;

		virtual void OnDebugGUI() = 0;

		virtual void OnGUI() = 0;

		virtual void OnDebugRender() = 0;

		virtual void OnResize(int32_t width, int32_t height) = 0;

		virtual std::string GetGameName() const = 0;
	};
}
