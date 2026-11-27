#pragma once

#include "UpdateComponent.hpp"
#include "../../Audio/SoundEffect.hpp"
#include <string>
#include <vector>

namespace HexEngine
{
	/**
	 * @brief Footstep sounds driven by animation events.
	 *
	 * Put it on a character with a SkeletalAnimationComponent. When the playing
	 * clip passes an event named like the left/right foot events ("FootL" /
	 * "FootR" by default - author them in the animation editor), it looks under
	 * that foot's bone, picks the hard or soft sample set from what the ray hits
	 * (soft = the hit entity's name starts with the soft-surface prefix, e.g. the
	 * voxel terrain's "VolTerrainChunk"), and plays a 3D one-shot there.
	 *
	 * Silent outside play, while the entity is hidden (DoNotRender - e.g. a
	 * resident sitting in their car), and beyond the hearing distance from the
	 * main camera. Voices are pooled across all footstep components.
	 */
	class HEX_API FootstepComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(FootstepComponent);

		FootstepComponent(Entity* entity);
		FootstepComponent(Entity* entity, FootstepComponent* copy);

		virtual void Update(float frameTime) override;
		virtual void OnMessage(Message* message, MessageListener* sender) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

		// Play a step for one foot now (what an event does). left = true for the left foot.
		void PlayStep(bool left);

	private:
		bool LoadSets();
		math::Vector3 FootPosition(bool left);
		static void SplitList(const std::string& list, std::vector<std::string>& out);

		// Authored.
		std::string _leftEvent = "FootL";
		std::string _rightEvent = "FootR";
		std::string _leftBone = "foot.L";
		std::string _rightBone = "foot.R";
		std::string _hardSounds;               // comma-separated sound paths
		std::string _softSounds;               // comma-separated; falls back to hard
		std::string _softSurfacePrefix = "VolTerrainChunk";
		float _volume = 0.55f;
		float _radius = 16.0f;                 // 3D falloff (m)
		float _hearDistance = 30.0f;           // no steps further than this from the camera (m)

		// Runtime.
		bool _loaded = false;
		std::string _loadedKey;
		std::vector<std::shared_ptr<SoundEffect>> _hard, _soft;
		int32_t _lastIndex = -1;
	};
}
