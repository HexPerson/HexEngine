#pragma once

#include "Mesh.hpp"

namespace HexEngine
{
	// Keyframe sampling and clip-editing helpers shared by the runtime evaluator
	// (SkeletalAnimationComponent) and the editor's animation editor.
	//
	// Key times are ABSOLUTE clip time in ticks (Animation::ticksPerSecond per second).
	// Matrices follow the evaluator's "composed" convention: a node's local matrix is
	// (Scale * Rotation * Translation).Transpose(), and global = parentGlobal * local.
	namespace AnimationUtils
	{
		enum class Track : uint8_t
		{
			Position = 0,
			Rotation = 1,
			Scale = 2,
		};

		struct NodePose
		{
			math::Vector3 translation = math::Vector3::Zero;
			math::Quaternion rotation = math::Quaternion::Identity;
			math::Vector3 scale = math::Vector3::One;
		};

		// Times closer than this (in ticks) are the same key.
		constexpr float kKeyTimeEpsilon = 1e-3f;

		HEX_API float GetTicksPerSecond(const Animation& anim);	// 0 in the file means 25

		HEX_API bool HasKeys(const AnimChannel& channel);

		// The channel's local pose at `ticks`. Keyed channels interpolate their tracks
		// (an empty track falls back to identity, exactly as the runtime does); a channel
		// with no keys at all returns its decomposed bind pose (nodeTransform).
		HEX_API NodePose SampleChannel(const AnimChannel& channel, float ticks);

		HEX_API math::Matrix ComposeLocal(const NodePose& pose);
		HEX_API NodePose DecomposeLocal(const math::Matrix& composedLocal);

		// Deep copy whose _rootNode / children point into the COPY's channels. A plain
		// Animation copy would keep pointing at the source's channels.
		HEX_API Animation CloneAnimation(const Animation& source);

		// Same channel tree, every key removed: the clip plays the bind pose.
		HEX_API Animation CloneAnimationStructure(const Animation& source);

		HEX_API AnimChannel* FindChannel(Animation& anim, const std::string& nodeName);
		HEX_API const AnimChannel* FindChannel(const Animation& anim, const std::string& nodeName);

		// Writes `pose` into the channel's tracks at `ticks` (replacing a key at the same
		// time). Writing any key into a channel that had none seeds all three tracks, since
		// a keyed channel with an empty track would snap that track to identity.
		HEX_API void SetPoseKey(AnimChannel& channel, float ticks, const NodePose& pose, bool position, bool rotation, bool scale);

		// Number of keys (any track) at `ticks`.
		HEX_API uint32_t CountKeysAt(const AnimChannel& channel, float ticks);
		HEX_API bool RemoveKeysAt(AnimChannel& channel, float ticks, bool position, bool rotation, bool scale);

		// Largest key time in the clip (0 when nothing is keyed).
		HEX_API float GetLastKeyTime(const Animation& anim);

		// ---- per-track key helpers (keys kept sorted by time) ------------------------

		template<typename T>
		int32_t FindKeyAt(const std::vector<std::pair<float, T>>& keys, float ticks, float epsilon = kKeyTimeEpsilon)
		{
			for (int32_t i = 0; i < (int32_t)keys.size(); ++i)
			{
				if (std::abs(keys[i].first - ticks) <= epsilon)
					return i;
			}
			return -1;
		}

		template<typename T>
		void SetKey(std::vector<std::pair<float, T>>& keys, float ticks, const T& value, float epsilon = kKeyTimeEpsilon)
		{
			if (const int32_t existing = FindKeyAt(keys, ticks, epsilon); existing >= 0)
			{
				keys[existing].second = value;
				return;
			}

			auto it = std::lower_bound(keys.begin(), keys.end(), ticks,
				[](const std::pair<float, T>& key, float t) { return key.first < t; });
			keys.insert(it, { ticks, value });
		}

		template<typename T>
		bool RemoveKey(std::vector<std::pair<float, T>>& keys, float ticks, float epsilon = kKeyTimeEpsilon)
		{
			if (const int32_t existing = FindKeyAt(keys, ticks, epsilon); existing >= 0)
			{
				keys.erase(keys.begin() + existing);
				return true;
			}
			return false;
		}

		template<typename T>
		void SortKeys(std::vector<std::pair<float, T>>& keys)
		{
			std::stable_sort(keys.begin(), keys.end(),
				[](const std::pair<float, T>& a, const std::pair<float, T>& b) { return a.first < b.first; });
		}

		// Index i of the key pair [i, i+1] to interpolate at `ticks`: the smallest i with
		// ticks <= key[i+1], or the last pair. Requires keys.size() >= 2.
		template<typename T>
		uint32_t FindKeyPair(const std::vector<std::pair<float, T>>& keys, float ticks)
		{
			const uint32_t lastPair = (uint32_t)keys.size() - 2;
			auto it = std::lower_bound(keys.begin() + 1, keys.end(), ticks,
				[](const std::pair<float, T>& key, float t) { return key.first < t; });
			const uint32_t next = (uint32_t)(it - keys.begin());
			return std::min(next - 1, lastPair);
		}

		// Interpolation factor between keys[index] and keys[index+1], clamped to [0, 1]
		// (before the first key holds it, after the last key holds that).
		template<typename T>
		float KeyFactor(const std::vector<std::pair<float, T>>& keys, uint32_t index, float ticks)
		{
			const float t1 = keys[index].first;
			const float t2 = keys[index + 1].first;
			if (t2 <= t1)
				return ticks >= t2 ? 1.0f : 0.0f;
			return std::clamp((ticks - t1) / (t2 - t1), 0.0f, 1.0f);
		}
	}
}
