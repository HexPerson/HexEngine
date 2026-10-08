#include "AnimationUtils.hpp"

namespace HexEngine
{
	namespace AnimationUtils
	{
		namespace
		{
			math::Vector3 SampleVectorTrack(const std::vector<std::pair<float, math::Vector3>>& keys, float ticks, const math::Vector3& fallback)
			{
				if (keys.empty())
					return fallback;
				if (keys.size() == 1)
					return keys[0].second;

				const uint32_t i = FindKeyPair(keys, ticks);
				const math::Vector3& start = keys[i].second;
				return start + KeyFactor(keys, i, ticks) * (keys[i + 1].second - start);
			}

			math::Quaternion SampleRotationTrack(const std::vector<std::pair<float, math::Quaternion>>& keys, float ticks)
			{
				if (keys.empty())
					return math::Quaternion::Identity;
				if (keys.size() == 1)
					return keys[0].second;

				const uint32_t i = FindKeyPair(keys, ticks);
				math::Quaternion out;
				math::Quaternion::Lerp(keys[i].second, keys[i + 1].second, KeyFactor(keys, i, ticks), out);
				return out;
			}

			// Re-points `copy`'s tree at its own channels, using `source`'s tree as the map.
			void RelinkTree(const Animation& source, Animation& copy)
			{
				const AnimChannel* base = source.channels.empty() ? nullptr : &source.channels[0];
				const auto indexOf = [&](const AnimChannel* channel) -> int64_t
				{
					if (channel == nullptr || base == nullptr)
						return -1;
					const int64_t index = channel - base;
					return (index >= 0 && index < (int64_t)source.channels.size()) ? index : -1;
				};

				copy._rootNode = nullptr;
				if (const int64_t root = indexOf(source._rootNode); root >= 0)
					copy._rootNode = &copy.channels[(size_t)root];

				for (size_t c = 0; c < copy.channels.size(); ++c)
				{
					auto& children = copy.channels[c].children;
					const auto& sourceChildren = source.channels[c].children;
					children.clear();
					children.reserve(sourceChildren.size());
					for (const AnimChannel* child : sourceChildren)
					{
						if (const int64_t index = indexOf(child); index >= 0)
							children.push_back(&copy.channels[(size_t)index]);
					}
				}
			}
		}

		float GetTicksPerSecond(const Animation& anim)
		{
			return anim.ticksPerSecond != 0.0f ? anim.ticksPerSecond : 25.0f;
		}

		bool HasKeys(const AnimChannel& channel)
		{
			return !channel.positionKeys.empty() || !channel.rotationKeys.empty() || !channel.scaleKeys.empty();
		}

		NodePose SampleChannel(const AnimChannel& channel, float ticks)
		{
			if (!HasKeys(channel))
				return DecomposeLocal(channel.nodeTransform);

			NodePose pose;
			pose.translation = SampleVectorTrack(channel.positionKeys, ticks, math::Vector3::Zero);
			pose.rotation = SampleRotationTrack(channel.rotationKeys, ticks);
			pose.scale = SampleVectorTrack(channel.scaleKeys, ticks, math::Vector3::One);
			return pose;
		}

		math::Matrix ComposeLocal(const NodePose& pose)
		{
			// Same as (CreateScale * CreateFromQuaternion * CreateTranslation).Transpose().
			math::Matrix m = math::Matrix::CreateFromQuaternion(pose.rotation);
			m._11 *= pose.scale.x; m._12 *= pose.scale.x; m._13 *= pose.scale.x;
			m._21 *= pose.scale.y; m._22 *= pose.scale.y; m._23 *= pose.scale.y;
			m._31 *= pose.scale.z; m._32 *= pose.scale.z; m._33 *= pose.scale.z;
			m._41 = pose.translation.x; m._42 = pose.translation.y; m._43 = pose.translation.z;
			return m.Transpose();
		}

		NodePose DecomposeLocal(const math::Matrix& composedLocal)
		{
			NodePose pose;
			math::Matrix rowForm = composedLocal.Transpose();
			if (!rowForm.Decompose(pose.scale, pose.rotation, pose.translation))
			{
				pose = NodePose{};
				pose.translation = rowForm.Translation();
			}
			return pose;
		}

		Animation CloneAnimation(const Animation& source)
		{
			Animation copy;
			copy.name = source.name;
			copy.ticksPerSecond = source.ticksPerSecond;
			copy.duration = source.duration;
			copy._globalInverseTransform = source._globalInverseTransform;
			copy.channels = source.channels;	// keys copied; tree pointers fixed below
			RelinkTree(source, copy);
			return copy;
		}

		Animation CloneAnimationStructure(const Animation& source)
		{
			Animation copy = CloneAnimation(source);
			for (auto& channel : copy.channels)
			{
				channel.positionKeys.clear();
				channel.rotationKeys.clear();
				channel.scaleKeys.clear();
			}
			return copy;
		}

		AnimChannel* FindChannel(Animation& anim, const std::string& nodeName)
		{
			for (auto& channel : anim.channels)
			{
				if (channel.nodeName == nodeName)
					return &channel;
			}
			return nullptr;
		}

		const AnimChannel* FindChannel(const Animation& anim, const std::string& nodeName)
		{
			return FindChannel(const_cast<Animation&>(anim), nodeName);
		}

		void SetPoseKey(AnimChannel& channel, float ticks, const NodePose& pose, bool position, bool rotation, bool scale)
		{
			if (!HasKeys(channel))
				position = rotation = scale = true;

			if (position || channel.positionKeys.empty())
				SetKey(channel.positionKeys, ticks, pose.translation);
			if (rotation || channel.rotationKeys.empty())
				SetKey(channel.rotationKeys, ticks, pose.rotation);
			if (scale || channel.scaleKeys.empty())
				SetKey(channel.scaleKeys, ticks, pose.scale);
		}

		uint32_t CountKeysAt(const AnimChannel& channel, float ticks)
		{
			return (FindKeyAt(channel.positionKeys, ticks) >= 0 ? 1u : 0u)
				+ (FindKeyAt(channel.rotationKeys, ticks) >= 0 ? 1u : 0u)
				+ (FindKeyAt(channel.scaleKeys, ticks) >= 0 ? 1u : 0u);
		}

		bool RemoveKeysAt(AnimChannel& channel, float ticks, bool position, bool rotation, bool scale)
		{
			bool removed = false;
			if (position) removed |= RemoveKey(channel.positionKeys, ticks);
			if (rotation) removed |= RemoveKey(channel.rotationKeys, ticks);
			if (scale) removed |= RemoveKey(channel.scaleKeys, ticks);
			return removed;
		}

		float GetLastKeyTime(const Animation& anim)
		{
			float last = 0.0f;
			for (const auto& channel : anim.channels)
			{
				if (!channel.positionKeys.empty()) last = std::max(last, channel.positionKeys.back().first);
				if (!channel.rotationKeys.empty()) last = std::max(last, channel.rotationKeys.back().first);
				if (!channel.scaleKeys.empty()) last = std::max(last, channel.scaleKeys.back().first);
			}
			return last;
		}
	}
}
