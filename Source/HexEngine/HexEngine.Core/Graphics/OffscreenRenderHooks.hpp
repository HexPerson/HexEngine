#pragma once

#include "../Required.hpp"

namespace HexEngine
{
	// Callbacks that render private scenes into their own targets once per frame,
	// after the frame has begun and before the registered scenes render (the same
	// slot IconService uses). For editor previews - e.g. the animation editor's
	// character viewport - that must not be registered with SceneManager, whose
	// update/render loops would repoint its "current scene" at them.
	namespace OffscreenRenderHooks
	{
		using RenderFn = std::function<void()>;

		// Returns an id for Remove(). Callbacks run on the render thread in
		// registration order.
		HEX_API uint32_t Add(RenderFn fn);
		HEX_API void Remove(uint32_t id);

		// Called by the environment's frame loop.
		HEX_API void RunAll();
	}
}
