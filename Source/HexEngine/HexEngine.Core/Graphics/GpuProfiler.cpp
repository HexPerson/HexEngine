#include "GpuProfiler.hpp"
#include "../HexEngine.hpp"
#include <algorithm>
#include <sstream>

namespace HexEngine
{
	HVar r_gpuTimings("r_gpuTimings", "GPU timestamp profiling: 1 = record GPU scopes and log averages (GpuTimings: name=ms) about once a second", false, false, true);

	GpuProfiler& GpuProfiler::Get()
	{
		static GpuProfiler s_instance;
		return s_instance;
	}

	bool GpuProfiler::EnsureDevice()
	{
		if (_device != nullptr && _context != nullptr)
			return true;
		if (g_pEnv == nullptr || g_pEnv->_graphicsDevice == nullptr ||
			g_pEnv->_graphicsDevice->GetBackend() != GraphicsBackend::D3D11)
			return false;
		_device = reinterpret_cast<ID3D11Device*>(g_pEnv->_graphicsDevice->GetNativeDevice());
		_context = reinterpret_cast<ID3D11DeviceContext*>(g_pEnv->_graphicsDevice->GetNativeDeviceContext());
		return _device != nullptr && _context != nullptr;
	}

	ID3D11Query* GpuProfiler::CreateQuery(D3D11_QUERY type)
	{
		D3D11_QUERY_DESC desc = {};
		desc.Query = type;
		ID3D11Query* query = nullptr;
		if (FAILED(_device->CreateQuery(&desc, &query)))
			return nullptr;
		return query;
	}

	void GpuProfiler::ResolveSlot(FrameSlot& slot)
	{
		if (!slot.pending || slot.disjoint == nullptr)
			return;
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
		if (_context->GetData(slot.disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
		{
			// Not ready kFrameSlots-1 frames later: drop rather than stall.
			slot.pending = false;
			slot.used = 0;
			return;
		}
		slot.pending = false;
		if (disjoint.Disjoint || disjoint.Frequency == 0)
		{
			slot.used = 0;
			return;
		}
		// Scopes that repeat within a frame (one per cascade, say) sum into
		// a single per-frame sample.
		std::unordered_map<std::string, double> frameMs;
		for (uint32_t i = 0; i < slot.used; ++i)
		{
			auto& scope = slot.scopes[i];
			if (scope.open)
				continue;
			UINT64 t0 = 0;
			UINT64 t1 = 0;
			if (_context->GetData(scope.begin, &t0, sizeof(t0), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
				_context->GetData(scope.end, &t1, sizeof(t1), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
				t1 < t0)
				continue;
			frameMs[scope.name] += static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(disjoint.Frequency);
		}
		for (const auto& [name, ms] : frameMs)
		{
			auto& stat = _stats[name];
			stat.sumMs += ms;
			++stat.samples;
		}
		slot.used = 0;
	}

	void GpuProfiler::NewFrame()
	{
		if (!r_gpuTimings._val.b)
		{
			if (_active && _context != nullptr)
			{
				FrameSlot& open = _slots[_current];
				if (open.disjointOpen)
				{
					_context->End(open.disjoint);
					open.disjointOpen = false;
				}
			}
			_active = false;
			return;
		}
		if (!EnsureDevice())
			return;

		// Close the frame that was being recorded.
		FrameSlot& previous = _slots[_current];
		if (previous.disjointOpen)
		{
			_context->End(previous.disjoint);
			previous.disjointOpen = false;
			previous.pending = true;
		}

		++_frame;
		_current = static_cast<uint32_t>(_frame % kFrameSlots);
		FrameSlot& slot = _slots[_current];
		// This slot was recorded kFrameSlots frames ago - read it back.
		ResolveSlot(slot);
		slot.used = 0;
		if (slot.disjoint == nullptr)
			slot.disjoint = CreateQuery(D3D11_QUERY_TIMESTAMP_DISJOINT);
		if (slot.disjoint != nullptr)
		{
			_context->Begin(slot.disjoint);
			slot.disjointOpen = true;
		}
		_active = slot.disjointOpen;

		const auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration<double>(now - _lastLog).count() >= 1.0)
		{
			_lastLog = now;
			std::vector<std::pair<std::string, float>> rows;
			for (auto& [name, stat] : _stats)
			{
				if (stat.samples > 0)
				{
					stat.averageMs = static_cast<float>(stat.sumMs / static_cast<double>(stat.samples));
					rows.emplace_back(name, stat.averageMs);
				}
				stat.sumMs = 0.0;
				stat.samples = 0;
			}
			if (!rows.empty())
			{
				std::sort(rows.begin(), rows.end());
				std::ostringstream line;
				line.setf(std::ios::fixed);
				line.precision(3);
				for (const auto& [name, ms] : rows)
					line << name << "=" << ms << " ";
				LOG_INFO("GpuTimings: %s", line.str().c_str());
			}
		}
	}

	void GpuProfiler::Begin(const char* name)
	{
		if (!_active || name == nullptr)
			return;
		FrameSlot& slot = _slots[_current];
		if (slot.used >= slot.scopes.size())
		{
			ScopeQueries q;
			q.begin = CreateQuery(D3D11_QUERY_TIMESTAMP);
			q.end = CreateQuery(D3D11_QUERY_TIMESTAMP);
			if (q.begin == nullptr || q.end == nullptr)
				return;
			slot.scopes.push_back(q);
		}
		ScopeQueries& q = slot.scopes[slot.used++];
		q.name = name;
		q.open = true;
		_context->End(q.begin);
	}

	void GpuProfiler::End(const char* name)
	{
		if (!_active || name == nullptr)
			return;
		FrameSlot& slot = _slots[_current];
		for (uint32_t i = slot.used; i-- > 0;)
		{
			ScopeQueries& q = slot.scopes[i];
			if (q.open && q.name == name)
			{
				_context->End(q.end);
				q.open = false;
				return;
			}
		}
	}

	float GpuProfiler::GetAverageMs(const std::string& name) const
	{
		auto it = _stats.find(name);
		return it != _stats.end() ? it->second.averageMs : 0.0f;
	}

	void GpuProfiler::Shutdown()
	{
		for (auto& slot : _slots)
		{
			SAFE_RELEASE(slot.disjoint);
			for (auto& q : slot.scopes)
			{
				SAFE_RELEASE(q.begin);
				SAFE_RELEASE(q.end);
			}
			slot.scopes.clear();
			slot.used = 0;
			slot.pending = false;
			slot.disjointOpen = false;
		}
		_device = nullptr;
		_context = nullptr;
		_active = false;
	}

	GpuScope::GpuScope(const char* name) : _name(name)
	{
		GpuProfiler::Get().Begin(_name);
	}

	GpuScope::~GpuScope()
	{
		GpuProfiler::Get().End(_name);
	}
}
