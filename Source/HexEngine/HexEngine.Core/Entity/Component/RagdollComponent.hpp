#pragma once

#include "UpdateComponent.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace HexEngine
{
	class IRigidBody;

	/**
	 * @brief Limp ragdoll for a skinned character, triggered by a hard hit.
	 *
	 * On Activate (or when the player's ridden vehicle hits the character fast
	 * enough) it builds 11 dynamic capsule bodies - pelvis, chest, head, upper/
	 * lower arms and legs - matching the current animation pose, joins them with
	 * cone/twist-limited joints, hands them the hit's velocity, and drives the
	 * skeleton from them (SkeletalAnimationComponent::SetPhysicsPose). The
	 * character's own collider is switched off and its routine paused meanwhile.
	 * Once the body settles it stays down a moment, then the character is moved
	 * to where it fell and blends back to its animation and carries on.
	 *
	 * Rigs: the Synty skeleton (spine / upperarm.L / thigh.L ...) and Mixamo's
	 * (mixamorig:Hips / LeftArm / LeftUpLeg ...), picked from the bone names.
	 */
	class HEX_API RagdollComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(RagdollComponent);

		RagdollComponent(Entity* entity);
		RagdollComponent(Entity* entity, RagdollComponent* copy);
		virtual ~RagdollComponent();

		virtual void Update(float frameTime) override;
		virtual void Destroy() override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;

		// Go limp now, thrown with `velocity` (m/s, world). False if it can't
		// (no skeleton / unknown rig / already down).
		bool Activate(const math::Vector3& velocity);
		bool IsDown() const { return _state != State::Standing; }

		// One limb body of a rig profile.
		struct PartDef
		{
			const char* node;
			const char* end;         // node at the far end (nullptr = extend along the parent)
			int32_t parent;          // index into the def list, -1 = root
			float radius;
			float mass;
			float swing;             // joint limits to the parent (degrees)
			float twist;
		};

	private:
		enum class State { Standing, Ragdoll, Recovering };

		struct Part
		{
			std::string node;        // skeleton node this body drives
			std::string bodyName;    // runtime entity holding the body
			IRigidBody* body = nullptr;
			math::Matrix rel;        // node world frame = rel * body world pose (row convention)
			float mass = 1.0f;
		};

		const std::vector<PartDef>* PickRig();
		void CheckForHit();
		void UpdateRagdoll(float dt);
		void BeginRecovery();
		void FinishRecovery();
		void DestroyBodies();
		void SetCharacterActive(bool active);   // own collider + routine

		// Tuning (serialized).
		float _hitSpeed = 5.0f;      // vehicle speed (m/s) that knocks the character down
		float _downTime = 2.5f;      // seconds lying still before getting up
		float _maxDownTime = 9.0f;   // give up waiting for the body to settle
		float _throwScale = 0.75f;   // fraction of the vehicle's velocity handed to the body
		bool _holdStill = false;     // keep the character idle where it stands (testing / staging)
		bool _holding = false;       // runtime: the hold is currently applied

		// Runtime.
		State _state = State::Standing;
		std::vector<Part> _parts;
		std::vector<void*> _joints;
		std::unordered_map<std::string, math::Matrix> _frames;   // node -> entity-local frame
		float _timer = 0.0f;
		float _stillTimer = 0.0f;
		float _blend = 1.0f;
	};
}
