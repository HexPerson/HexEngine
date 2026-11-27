#pragma once

#include "../Required.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace HexEngine
{
	/**
	 * @brief Minimal GPU timestamp profiler (D3D11 timestamp queries).
	 *
	 * The engine had no GPU timing at all - only CPU PROFILE() scopes - so
	 * GPU cost could only be inferred from editor fps. Scopes are recorded
	 * into one of kFrameSlots per-frame slots and read back kFrameSlots-1
	 * frames later without stalling (DONOTFLUSH; a not-ready slot is simply
	 * dropped). With r_gpuTimings 1 the running averages are logged about
	 * once a second as "GpuTimings: name=ms ..." (readable through the
	 * editor bridge's engine-log tool). GetAverageMs() serves code callers.
	 *
	 * NewFrame() must be called once per rendered main-camera frame, before
	 * any scope of that frame.
	 */
	class HEX_API GpuProfiler
	{
	public:
		static GpuProfiler& Get();

		void NewFrame();
		void Begin(const char* name);
		void End(const char* name);
		float GetAverageMs(const std::string& name) const;
		void Shutdown();

	private:
		static constexpr uint32_t kFrameSlots = 4;

		struct ScopeQueries
		{
			std::string name;
			ID3D11Query* begin = nullptr;
			ID3D11Query* end = nullptr;
			bool open = false;
		};

		struct FrameSlot
		{
			ID3D11Query* disjoint = nullptr;
			std::vector<ScopeQueries> scopes;
			uint32_t used = 0;
			bool disjointOpen = false;
			bool pending = false;
		};

		struct Stat
		{
			double sumMs = 0.0;
			uint32_t samples = 0;
			float averageMs = 0.0f;
		};

		bool EnsureDevice();
		ID3D11Query* CreateQuery(D3D11_QUERY type);
		void ResolveSlot(FrameSlot& slot);

		ID3D11Device* _device = nullptr;
		ID3D11DeviceContext* _context = nullptr;
		FrameSlot _slots[kFrameSlots];
		uint32_t _current = 0;
		uint64_t _frame = 0;
		bool _active = false;
		std::unordered_map<std::string, Stat> _stats;
		std::chrono::steady_clock::time_point _lastLog = std::chrono::steady_clock::now();
	};

	/** @brief RAII GPU scope. No-op unless r_gpuTimings is on. */
	class HEX_API GpuScope
	{
	public:
		explicit GpuScope(const char* name);
		~GpuScope();
	private:
		const char* _name = nullptr;
	};
}

#define HEX_GPU_SCOPE_CONCAT_INNER(a, b) a##b
#define HEX_GPU_SCOPE_CONCAT(a, b) HEX_GPU_SCOPE_CONCAT_INNER(a, b)
#define GPU_SCOPE(name) HexEngine::GpuScope HEX_GPU_SCOPE_CONCAT(_gpuScope_, __LINE__)(name)
