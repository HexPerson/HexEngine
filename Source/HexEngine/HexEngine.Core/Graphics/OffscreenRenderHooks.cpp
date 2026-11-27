#include "OffscreenRenderHooks.hpp"

namespace HexEngine
{
	namespace OffscreenRenderHooks
	{
		namespace
		{
			struct Hook
			{
				uint32_t id;
				RenderFn fn;
			};

			std::vector<Hook>& Hooks()
			{
				static std::vector<Hook> s_hooks;
				return s_hooks;
			}

			uint32_t g_nextId = 1;
		}

		uint32_t Add(RenderFn fn)
		{
			const uint32_t id = g_nextId++;
			Hooks().push_back({ id, std::move(fn) });
			return id;
		}

		void Remove(uint32_t id)
		{
			auto& hooks = Hooks();
			hooks.erase(std::remove_if(hooks.begin(), hooks.end(), [id](const Hook& h) { return h.id == id; }), hooks.end());
		}

		void RunAll()
		{
			// Copy: a callback may add or remove hooks (e.g. an editor tab closing).
			const std::vector<Hook> hooks = Hooks();
			for (const auto& hook : hooks)
			{
				if (hook.fn)
					hook.fn();
			}
		}
	}
}
