

#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	/**
	 * @brief Runtime game module contract implemented by game DLLs.
	 *
	 * The engine/editor calls this interface for lifecycle transitions,
	 * simulation ticks, rendering hooks, and UI callbacks.
	 */
	class IGameExtension
	{
	public:
		/** @brief Called when the runtime is ready for gameplay object creation. */
		virtual void OnCreateGame() = 0;

		/** @brief Called when gameplay is being stopped (for example editor stop/hot reload). */
		virtual void OnStopGame() = 0;

		/** @brief Called when a world/scene has finished loading and can be initialized. */
		virtual void OnLoadGameWorld() = 0;

		/** @brief Registers game-side reflected/serializable classes with the engine registry. */
		virtual void OnRegisterClasses() = 0;

		/**
		 * @brief Per-frame game update.
		 * @param frameTime Delta time in seconds for the current frame.
		 */
		virtual void OnUpdate(float frameTime) = 0;

		/**
		 * @brief Fixed-timestep game update.
		 * @param frameTime Fixed-step delta in seconds.
		 */
		virtual void OnFixedUpdate(float frameTime) = 0;

		/** @brief Final shutdown callback before the game module is unloaded. */
		virtual void OnShutdown() = 0;

		/** @brief Draws debug UI elements. */
		virtual void OnDebugGUI() = 0;

		/** @brief Draws in-game UI elements. */
		virtual void OnGUI() = 0;

		/** @brief Draws debug render primitives. */
		virtual void OnDebugRender() = 0;

		/**
		 * @brief Notifies the game module about output resolution changes.
		 * @param width New framebuffer width in pixels.
		 * @param height New framebuffer height in pixels.
		 */
		virtual void OnResize(int32_t width, int32_t height) = 0;

		/** @brief Returns a user-facing game/module name. */
		virtual std::string GetGameName() const = 0;
	};
}
