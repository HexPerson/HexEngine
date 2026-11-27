// Animation-event authoring helpers + console commands.
//
//   anim_list_clips <mesh>                         clips with their duration and events
//   anim_detect_footsteps <mesh> <clip> [...]      find foot plants -> FootL / FootR events
//                                                  (<clip> = index or name substring; several allowed)
//   anim_clear_events <mesh> <clip> [...]
//   anim_copy_events <srcMesh> <dstMesh> [...]     copy every clip's events by clip NAME
//   anim_save_mesh <mesh>                          write the .hmesh (events included)
//
// detect/clear/copy take a trailing "save" to write each changed mesh at once - needed
// for meshes nothing in the scene holds (they are unloaded between commands).
//
// <mesh> is a resource path such as GameData.Meshes/Universe/Characters/Skinned/X.hmesh.

#include "AnimationUtils.hpp"
#include "AnimatedMesh.hpp"
#include "Mesh.hpp"
#include "../Input/HCommand.hpp"
#include "../Environment/LogFile.hpp"
#include "../Environment/IEnvironment.hpp"
#include "../Input/CommandManager.hpp"
#include "../Input/Console.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>

namespace HexEngine
{
	namespace AnimationUtils
	{
		namespace
		{
			// Mesh-space position of `nodeName` at `ticks` (same composition as
			// SkeletalAnimationComponent: global = parentGlobal * local, position =
			// global.Transpose().Translation()).
			bool NodePositionAt(const Animation& anim, const math::Matrix& root, const std::string& nodeName, float ticks, math::Vector3& out)
			{
				if (anim._rootNode == nullptr)
					return false;
				bool found = false;
				std::function<void(const AnimChannel*, const math::Matrix&)> walk = [&](const AnimChannel* ch, const math::Matrix& parent)
				{
					if (found || ch == nullptr)
						return;
					const math::Matrix global = parent * ComposeLocal(SampleChannel(*ch, ticks));
					if (ch->nodeName == nodeName)
					{
						// Model space, as playback skins it: clipRoot * global.
						out = (root * global).Transpose().Translation();
						found = true;
						return;
					}
					for (const AnimChannel* c : ch->children)
						walk(c, global);
				};
				walk(anim._rootNode, math::Matrix::Identity);
				return found;
			}
		}

		int32_t DetectFootEvents(Animation& anim, const math::Matrix& fallbackRoot, const std::string& leftNode, const std::string& rightNode,
			const std::string& leftEvent, const std::string& rightEvent)
		{
			const math::Matrix root = anim._globalInverseTransform != math::Matrix::Identity ? anim._globalInverseTransform : fallbackRoot;
			if (anim.duration <= 0.0f)
				return 0;

			// Replace earlier detections of these two events; keep any others.
			anim.events.erase(std::remove_if(anim.events.begin(), anim.events.end(),
				[&](const AnimationEvent& e) { return e.name == leftEvent || e.name == rightEvent; }), anim.events.end());

			constexpr int32_t kSamples = 120;
			int32_t added = 0;
			for (int32_t foot = 0; foot < 2; ++foot)
			{
				const std::string& node = foot == 0 ? leftNode : rightNode;
				std::vector<float> h(kSamples);
				bool ok = true;
				math::Vector3 mn3(FLT_MAX, FLT_MAX, FLT_MAX), mx3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
				for (int32_t i = 0; i < kSamples && ok; ++i)
				{
					math::Vector3 p;
					ok = NodePositionAt(anim, root, node, anim.duration * (float)i / (float)kSamples, p);
					h[i] = p.y;
					mn3 = math::Vector3::Min(mn3, p);
					mx3 = math::Vector3::Max(mx3, p);
				}
				if (ok)
					LOG_DEBUG("DetectFootEvents: '%s' %s range x %.3f y %.3f z %.3f (y %.3f..%.3f)", anim.name.c_str(), node.c_str(),
						mx3.x - mn3.x, mx3.y - mn3.y, mx3.z - mn3.z, mn3.y, mx3.y);
				if (!ok)
				{
					LOG_WARN("anim_detect_footsteps: clip '%s' has no node '%s'", anim.name.c_str(), node.c_str());
					continue;
				}
				const auto [mn, mx] = std::minmax_element(h.begin(), h.end());
				const float lo = *mn, range = *mx - *mn;
				// A foot that barely moves (idle, standing) never "plants".
				if (!(range > 0.03f))
					continue;
				// Heel strike: the foot comes down into the bottom 15% of its swing.
				const float threshold = lo + range * 0.15f;
				// Hysteresis: after a plant the foot must lift past `rearm` before it can
				// plant again (heel then toe touching down would otherwise count twice).
				// Walk the loop starting from the foot's highest point, so it starts armed.
				const float rearm = lo + range * 0.45f;
				const int32_t start = (int32_t)(std::max_element(h.begin(), h.end()) - h.begin());
				bool armed = true;
				for (int32_t k = 1; k <= kSamples; ++k)
				{
					const int32_t i = (start + k) % kSamples;
					const float prev = h[(i + kSamples - 1) % kSamples];
					if (h[i] > rearm)
						armed = true;
					if (armed && prev > threshold && h[i] <= threshold)
					{
						armed = false;
						// Interpolate the crossing between the two samples.
						const float f = (prev - threshold) / std::max(prev - h[i], 1e-6f);
						const float t = anim.duration * ((float)(i - 1) + f) / (float)kSamples;
						AnimationEvent ev;
						ev.time = std::fmod(t + anim.duration, anim.duration);
						ev.name = foot == 0 ? leftEvent : rightEvent;
						anim.events.push_back(ev);
						++added;
					}
				}
			}
			std::sort(anim.events.begin(), anim.events.end(),
				[](const AnimationEvent& a, const AnimationEvent& b) { return a.time < b.time; });
			return added;
		}
	}

	namespace
	{
		std::shared_ptr<AnimatedMesh> LoadAnimated(const std::string& path)
		{
			auto mesh = std::dynamic_pointer_cast<AnimatedMesh>(Mesh::Create(path));
			if (mesh == nullptr || mesh->GetAnimationData() == nullptr)
				CON_ECHO("^r'%s' is not an animated mesh", path.c_str());
			return mesh != nullptr && mesh->GetAnimationData() != nullptr ? mesh : nullptr;
		}

		bool ClipMatches(const Animation& anim, uint32_t index, const std::vector<std::string>& filters, size_t first)
		{
			for (size_t i = first; i < filters.size(); ++i)
			{
				const std::string& f = filters[i];
				if (f == "*")
					return true;
				if (!f.empty() && std::all_of(f.begin(), f.end(), ::isdigit) && (uint32_t)std::stoul(f) == index)
					return true;
				if (anim.name.find(f) != std::string::npos)
					return true;
			}
			return false;
		}

		std::string EventsText(const Animation& anim)
		{
			std::string s;
			const float tps = AnimationUtils::GetTicksPerSecond(anim);
			for (const auto& e : anim.events)
			{
				char buf[64];
				snprintf(buf, sizeof(buf), "%s%s@%.3fs", s.empty() ? "" : " ", e.name.c_str(), tps > 0.0f ? e.time / tps : e.time);
				s += buf;
			}
			return s;
		}
	}

	HEX_COMMAND(anim_list_clips)
	{
		(void)pressed; (void)param;
		if (args->Count() < 1) { CON_ECHO("^ranim_list_clips <mesh>"); return; }
		auto mesh = LoadAnimated(args->Get<std::string>(0));
		if (mesh == nullptr) return;
		const auto& clips = mesh->GetAnimationData()->_animations;
		for (uint32_t i = 0; i < clips.size(); ++i)
		{
			const float tps = AnimationUtils::GetTicksPerSecond(clips[i]);
			LOG_INFO("anim_list_clips: [%u] '%s' %.2fs, %zu event(s) %s", i, clips[i].name.c_str(),
				tps > 0.0f ? clips[i].duration / tps : 0.0f, clips[i].events.size(), EventsText(clips[i]).c_str());
		}
	}

	HEX_COMMAND(anim_detect_footsteps)
	{
		(void)pressed; (void)param;
		if (args->Count() < 2) { CON_ECHO("^ranim_detect_footsteps <mesh> <clip index|name|*> [...]"); return; }
		auto mesh = LoadAnimated(args->Get<std::string>(0));
		if (mesh == nullptr) return;
		auto data = mesh->GetAnimationData();
		const auto& all = args->GetAll();
		// Foot nodes: the Synty rig's foot.L / foot.R, else Mixamo's.
		const auto hasBone = [&](const char* n) { return mesh->GetBoneMap().count(n) != 0; };
		const std::string left = hasBone("foot.L") ? "foot.L" : "mixamorig:LeftFoot";
		const std::string right = hasBone("foot.R") ? "foot.R" : "mixamorig:RightFoot";
		for (uint32_t i = 0; i < data->_animations.size(); ++i)
		{
			Animation& anim = data->_animations[i];
			if (!ClipMatches(anim, i, all, 1))
				continue;
			const int32_t n = AnimationUtils::DetectFootEvents(anim, data->_globalInverseTransform, left, right, "FootL", "FootR");
			LOG_INFO("anim_detect_footsteps: [%u] '%s' -> %d foot event(s): %s", i, anim.name.c_str(), n, EventsText(anim).c_str());
		}
		++data->_revision;
		if (all.back() == "save")
			mesh->Save();
	}

	HEX_COMMAND(anim_clear_events)
	{
		(void)pressed; (void)param;
		if (args->Count() < 2) { CON_ECHO("^ranim_clear_events <mesh> <clip index|name|*> [...]"); return; }
		auto mesh = LoadAnimated(args->Get<std::string>(0));
		if (mesh == nullptr) return;
		auto data = mesh->GetAnimationData();
		const auto& all = args->GetAll();
		for (uint32_t i = 0; i < data->_animations.size(); ++i)
			if (ClipMatches(data->_animations[i], i, all, 1))
				data->_animations[i].events.clear();
		++data->_revision;
		if (all.back() == "save")
			mesh->Save();
	}

	HEX_COMMAND(anim_copy_events)
	{
		(void)pressed; (void)param;
		if (args->Count() < 2) { CON_ECHO("^ranim_copy_events <srcMesh> <dstMesh> [...]"); return; }
		auto src = LoadAnimated(args->Get<std::string>(0));
		if (src == nullptr) return;
		const auto& srcClips = src->GetAnimationData()->_animations;
		const bool save = args->GetAll().back() == "save";
		for (size_t a = 1; a < args->Count(); ++a)
		{
			if (args->Get<std::string>((int32_t)a) == "save")
				continue;
			auto dst = LoadAnimated(args->Get<std::string>((int32_t)a));
			if (dst == nullptr) continue;
			auto data = dst->GetAnimationData();
			int32_t copied = 0;
			for (auto& clip : data->_animations)
			{
				auto it = std::find_if(srcClips.begin(), srcClips.end(), [&](const Animation& s) { return s.name == clip.name; });
				if (it == srcClips.end())
					continue;
				// Same clip name = same timeline; rescale if the durations differ.
				clip.events = it->events;
				if (it->duration > 0.0f && clip.duration > 0.0f && std::fabs(it->duration - clip.duration) > 1e-3f)
					for (auto& e : clip.events)
						e.time *= clip.duration / it->duration;
				copied += (int32_t)clip.events.size();
			}
			++data->_revision;
			if (save)
				dst->Save();
			LOG_INFO("anim_copy_events: %s <- %d event(s)%s", args->Get<std::string>((int32_t)a).c_str(), copied, save ? " (saved)" : "");
		}
	}

	HEX_COMMAND(anim_save_mesh)
	{
		(void)pressed; (void)param;
		if (args->Count() < 1) { CON_ECHO("^ranim_save_mesh <mesh> [...]"); return; }
		for (size_t a = 0; a < args->Count(); ++a)
		{
			auto mesh = LoadAnimated(args->Get<std::string>((int32_t)a));
			if (mesh == nullptr) continue;
			mesh->Save();
			LOG_INFO("anim_save_mesh: saved %s", args->Get<std::string>((int32_t)a).c_str());
		}
	}
}
