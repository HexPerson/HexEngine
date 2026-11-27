#include "RagdollComponent.hpp"
#include "SkeletalAnimationComponent.hpp"
#include "StaticMeshComponent.hpp"
#include "DrivableComponent.hpp"
#include "RigidBody.hpp"
#include "Transform.hpp"
#include "IRoutineAgentState.hpp"
#include "../Entity.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/AnimatedMesh.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Physics/IPhysicsSystem.hpp"
#include "../../Physics/IRigidBody.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/Button.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace HexEngine
{
	// Rig profiles: node, end node, parent part, capsule radius, mass, joint swing/twist (deg).
	// Synty: spine > spine.02 > neck > head, shoulder > upperarm > forearm > hand,
	// pelvis > thigh > shin > foot.
	static const std::vector<RagdollComponent::PartDef> s_syntyRig = {
		{ "spine",      "spine.02", -1, 0.12f, 12.0f,  0.0f,  0.0f },
		{ "spine.02",   "neck",      0, 0.13f, 16.0f, 30.0f, 20.0f },
		{ "head",       nullptr,     1, 0.10f,  5.0f, 40.0f, 35.0f },
		{ "upperarm.L", "forearm.L", 1, 0.05f,  2.5f, 80.0f, 40.0f },
		{ "forearm.L",  "hand.L",    3, 0.045f, 1.6f, 70.0f, 15.0f },
		{ "upperarm.R", "forearm.R", 1, 0.05f,  2.5f, 80.0f, 40.0f },
		{ "forearm.R",  "hand.R",    5, 0.045f, 1.6f, 70.0f, 15.0f },
		{ "thigh.L",    "shin.L",    0, 0.065f, 8.0f, 55.0f, 20.0f },
		{ "shin.L",     "foot.L",    7, 0.05f,  4.0f, 65.0f,  8.0f },
		{ "thigh.R",    "shin.R",    0, 0.065f, 8.0f, 55.0f, 20.0f },
		{ "shin.R",     "foot.R",    9, 0.05f,  4.0f, 65.0f,  8.0f },
	};
	static const std::vector<RagdollComponent::PartDef> s_mixamoRig = {
		{ "mixamorig:Hips",         "mixamorig:Spine1",       -1, 0.12f, 12.0f,  0.0f,  0.0f },
		{ "mixamorig:Spine1",       "mixamorig:Neck",          0, 0.13f, 16.0f, 30.0f, 20.0f },
		{ "mixamorig:Head",         nullptr,                   1, 0.10f,  5.0f, 40.0f, 35.0f },
		{ "mixamorig:LeftArm",      "mixamorig:LeftForeArm",   1, 0.05f,  2.5f, 80.0f, 40.0f },
		{ "mixamorig:LeftForeArm",  "mixamorig:LeftHand",      3, 0.045f, 1.6f, 70.0f, 15.0f },
		{ "mixamorig:RightArm",     "mixamorig:RightForeArm",  1, 0.05f,  2.5f, 80.0f, 40.0f },
		{ "mixamorig:RightForeArm", "mixamorig:RightHand",     5, 0.045f, 1.6f, 70.0f, 15.0f },
		{ "mixamorig:LeftUpLeg",    "mixamorig:LeftLeg",       0, 0.065f, 8.0f, 55.0f, 20.0f },
		{ "mixamorig:LeftLeg",      "mixamorig:LeftFoot",      7, 0.05f,  4.0f, 65.0f,  8.0f },
		{ "mixamorig:RightUpLeg",   "mixamorig:RightLeg",      0, 0.065f, 8.0f, 55.0f, 20.0f },
		{ "mixamorig:RightLeg",     "mixamorig:RightFoot",     9, 0.05f,  4.0f, 65.0f,  8.0f },
	};

	namespace
	{
		const std::vector<RagdollComponent::PartDef>& SyntyRig() { return s_syntyRig; }
		const std::vector<RagdollComponent::PartDef>& MixamoRig() { return s_mixamoRig; }

		math::Matrix PoseMatrix(const math::Quaternion& q, const math::Vector3& p)
		{
			return math::Matrix::CreateFromQuaternion(q) * math::Matrix::CreateTranslation(p);
		}

		// The vehicle the player is riding, refreshed once per frame for every
		// character's hit test: its world box (all mesh parts) and velocity.
		struct RideCache
		{
			uint64_t frame = UINT64_MAX;
			Entity* vehicle = nullptr;
			dx::BoundingBox box;
			math::Vector3 velocity;
		};
		RideCache s_ride;

		void RefreshRide(Scene* scene)
		{
			const uint64_t frame = g_pEnv->_timeManager != nullptr ? (uint64_t)g_pEnv->_timeManager->_frameCount : 0;
			if (frame == s_ride.frame)
				return;
			s_ride.frame = frame;
			s_ride.vehicle = nullptr;

			std::vector<DrivableComponent*> drivables;
			scene->GetComponents<DrivableComponent>(drivables);
			for (auto* d : drivables)
			{
				if (d == nullptr || !d->IsPlayerControlled() || d->GetEntity() == nullptr)
					continue;
				Entity* v = d->GetEntity();
				auto* rb = v->GetComponent<RigidBody>();
				if (rb == nullptr || rb->GetIRigidBody() == nullptr)
					continue;
				math::Vector3 mn(FLT_MAX, FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
				std::vector<Entity*> stack{ v };
				while (!stack.empty())
				{
					Entity* e = stack.back();
					stack.pop_back();
					if (e == nullptr)
						continue;
					if (e->GetComponent<StaticMeshComponent>() != nullptr)
					{
						const dx::BoundingBox& b = e->GetWorldAABB();
						mn = math::Vector3::Min(mn, math::Vector3(b.Center) - math::Vector3(b.Extents));
						mx = math::Vector3::Max(mx, math::Vector3(b.Center) + math::Vector3(b.Extents));
					}
					for (auto* c : e->GetChildren())
						stack.push_back(c);
				}
				if (mn.x > mx.x)
					continue;
				dx::BoundingBox::CreateFromPoints(s_ride.box, mn, mx);
				s_ride.velocity = rb->GetIRigidBody()->GetLinearVelocity();
				s_ride.vehicle = v;
				return;
			}
		}
	}

	RagdollComponent::RagdollComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	RagdollComponent::RagdollComponent(Entity* entity, RagdollComponent* copy) :
		UpdateComponent(entity)
	{
		if (copy != nullptr)
		{
			_hitSpeed = copy->_hitSpeed;
			_downTime = copy->_downTime;
			_maxDownTime = copy->_maxDownTime;
			_throwScale = copy->_throwScale;
			_holdStill = copy->_holdStill;
		}
	}

	RagdollComponent::~RagdollComponent()
	{
		DestroyBodies();
	}

	void RagdollComponent::Destroy()
	{
		if (_state != State::Standing)
		{
			DestroyBodies();
			if (auto* skel = GetEntity()->GetComponent<SkeletalAnimationComponent>())
				skel->ClearPhysicsPose();
			SetCharacterActive(true);
			_state = State::Standing;
		}
		UpdateComponent::Destroy();
	}

	const std::vector<RagdollComponent::PartDef>* RagdollComponent::PickRig()
	{
		auto* skel = GetEntity()->GetComponent<SkeletalAnimationComponent>();
		if (skel == nullptr || skel->GetBoundMesh() == nullptr)
			return nullptr;
		const auto& bones = skel->GetBoundMesh()->GetBoneMap();
		if (bones.count("upperarm.L") != 0 && bones.count("thigh.L") != 0)
			return &SyntyRig();
		if (bones.count("mixamorig:LeftArm") != 0 && bones.count("mixamorig:LeftUpLeg") != 0)
			return &MixamoRig();
		return nullptr;
	}

	void RagdollComponent::SetCharacterActive(bool active)
	{
		Entity* e = GetEntity();
		if (auto* rb = e->GetComponent<RigidBody>(); rb != nullptr && rb->GetIRigidBody() != nullptr)
			rb->GetIRigidBody()->SetIsSimulated(active);
		if (auto* agent = e->GetComponentDerived<IRoutineAgentState>(); agent != nullptr)
			agent->SetConversationPaused(!active);
	}

	bool RagdollComponent::Activate(const math::Vector3& velocity)
	{
		if (_state != State::Standing || g_pEnv == nullptr || g_pEnv->_physicsSystem == nullptr)
			return false;
		Entity* e = GetEntity();
		Scene* scene = e->GetScene();
		auto* skel = e->GetComponent<SkeletalAnimationComponent>();
		const auto* rig = PickRig();
		if (scene == nullptr || skel == nullptr || rig == nullptr)
			return false;

		// Current pose of every driven node, in world space.
		const math::Matrix E = e->GetWorldTM();
		std::vector<math::Matrix> world(rig->size());
		for (size_t i = 0; i < rig->size(); ++i)
		{
			math::Matrix local;
			if (!skel->GetNodeEntityFrame((*rig)[i].node, local))
			{
				LOG_WARN("Ragdoll '%s': node '%s' not in the pose", e->GetName().c_str(), (*rig)[i].node);
				return false;
			}
			world[i] = local * E;
		}
		const auto nodeWorldPos = [&](const char* node, math::Vector3& out) -> bool
		{
			math::Matrix local;
			if (node == nullptr || !skel->GetNodeEntityFrame(node, local))
				return false;
			out = (local * E).Translation();
			return true;
		};

		SetCharacterActive(false);
		DestroyBodies();
		_parts.clear();
		_parts.resize(rig->size());

		for (size_t i = 0; i < rig->size(); ++i)
		{
			const PartDef& def = (*rig)[i];
			const math::Vector3 start = world[i].Translation();
			math::Vector3 end;
			if (!nodeWorldPos(def.end, end))
			{
				// No end node (the head): continue the parent's direction.
				const math::Vector3 from = def.parent >= 0 ? world[def.parent].Translation() : start - math::Vector3(0.0f, 0.2f, 0.0f);
				math::Vector3 dir = start - from;
				dir = dir.LengthSquared() > 1e-6f ? dir / dir.Length() : math::Vector3(0.0f, 1.0f, 0.0f);
				end = start + dir * 0.24f;
			}
			math::Vector3 axisX = end - start;
			const float length = axisX.Length();
			axisX = length > 1e-4f ? axisX / length : math::Vector3(0.0f, 1.0f, 0.0f);
			// Roll the body with the bone: its Y axis (orthogonalised against X).
			math::Vector3 up(world[i]._21, world[i]._22, world[i]._23);
			if (up.LengthSquared() < 1e-8f || std::fabs(up.Dot(axisX)) > 0.95f * up.Length())
				up = math::Vector3(world[i]._31, world[i]._32, world[i]._33);
			math::Vector3 axisZ = axisX.Cross(up);
			axisZ.Normalize();
			math::Vector3 axisY = axisZ.Cross(axisX);
			math::Matrix rot = math::Matrix::Identity;
			rot._11 = axisX.x; rot._12 = axisX.y; rot._13 = axisX.z;
			rot._21 = axisY.x; rot._22 = axisY.y; rot._23 = axisY.z;
			rot._31 = axisZ.x; rot._32 = axisZ.y; rot._33 = axisZ.z;
			math::Quaternion q = math::Quaternion::CreateFromRotationMatrix(rot);
			q.Normalize();
			const math::Vector3 centre = (start + end) * 0.5f;

			Part& part = _parts[i];
			part.node = def.node;
			part.mass = def.mass;
			part.bodyName = e->GetName() + "_Ragdoll_" + std::to_string(i);
			Entity* body = scene->CreateEntity(part.bodyName, centre, q);
			if (body == nullptr)
				continue;
			part.bodyName = body->GetName();   // the scene may have suffixed it
			body->SetFlag(EntityFlags::DoNotSave);
			body->SetFlag(EntityFlags::DoNotRender);
			auto* rb = body->AddComponent<RigidBody>(IRigidBody::BodyType::Dynamic);
			const float height = std::max(length - 2.0f * def.radius - 0.02f, 0.02f);
			rb->AddCapsuleCollider(def.radius, height, 0, math::Vector3::Zero);
			rb->SetMass(def.mass);
			part.body = rb->GetIRigidBody();
			if (part.body != nullptr)
			{
				part.body->SetLinearVelocityDamping(0.05f);
				part.body->SetAngularVelocityDamping(0.8f);
			}
			// node world = rel * body world.
			part.rel = world[i] * PoseMatrix(q, centre).Invert();
		}

		// Joints: each part hangs off its parent at its own start, twist along the child limb.
		for (size_t i = 0; i < rig->size(); ++i)
		{
			const PartDef& def = (*rig)[i];
			if (def.parent < 0 || _parts[i].body == nullptr || _parts[def.parent].body == nullptr)
				continue;
			const math::Quaternion q = _parts[i].body->GetPhysicsRotation();
			const math::Matrix jointWorld = PoseMatrix(q, world[i].Translation());
			if (void* j = g_pEnv->_physicsSystem->CreateConeJoint(_parts[def.parent].body, _parts[i].body, jointWorld, def.swing, def.twist))
				_joints.push_back(j);
		}

		// Thrown: every limb takes the hit, the head and arms a little more so it reads as a fall.
		for (size_t i = 0; i < _parts.size(); ++i)
		{
			if (_parts[i].body == nullptr)
				continue;
			const float extra = (i == 2 || (*rig)[i].mass < 3.0f) ? 1.15f : 1.0f;
			_parts[i].body->SetLinearVelocity(velocity * extra);
		}

		_state = State::Ragdoll;
		_timer = 0.0f;
		_stillTimer = 0.0f;
		_blend = 1.0f;
		LOG_INFO("Ragdoll: '%s' knocked down (%.1f m/s, %zu bodies, %zu joints)", e->GetName().c_str(), velocity.Length(), _parts.size(), _joints.size());
		UpdateRagdoll(0.0f);
		return true;
	}

	void RagdollComponent::DestroyBodies()
	{
		if (g_pEnv != nullptr && g_pEnv->_physicsSystem != nullptr)
			for (void* j : _joints)
				g_pEnv->_physicsSystem->DestroyJoint(j);
		_joints.clear();
		Scene* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
		for (Part& part : _parts)
		{
			if (scene != nullptr && !part.bodyName.empty())
				if (Entity* body = scene->GetEntityByName(part.bodyName); body != nullptr && !body->IsPendingDeletion())
					scene->DestroyEntity(body);
			part.body = nullptr;
		}
	}

	void RagdollComponent::UpdateRagdoll(float dt)
	{
		auto* skel = GetEntity()->GetComponent<SkeletalAnimationComponent>();
		if (skel == nullptr)
			return;

		// The character's root follows the pelvis across the ground. Culling and
		// everything that locates the character use the root's bounds: left at
		// the impact point, a body thrown a few metres was culled whenever that
		// spot was off screen. The skinned frames below are relative to this
		// moved root, so the body itself is unaffected.
		math::Matrix E = GetEntity()->GetWorldTM();
		if (!_parts.empty() && _parts[0].body != nullptr)
		{
			const math::Vector3 pelvis = _parts[0].body->GetPhysicsPosition();
			const math::Vector3 rootPos(pelvis.x, E._42, pelvis.z);
			if ((rootPos - E.Translation()).LengthSquared() > 1e-4f)
			{
				if (auto* tf = GetEntity()->GetComponent<Transform>())
					tf->SetPosition(rootPos);
				// Build the matrix directly: the cached world TM can lag a SetPosition.
				E._41 = rootPos.x; E._42 = rootPos.y; E._43 = rootPos.z;
			}
		}
		const math::Matrix toEntity = E.Invert();
		float maxSpeed = 0.0f;
		_frames.clear();
		for (const Part& part : _parts)
		{
			if (part.body == nullptr)
				continue;
			const math::Matrix bodyWorld = PoseMatrix(part.body->GetPhysicsRotation(), part.body->GetPhysicsPosition());
			_frames[part.node] = part.rel * bodyWorld * toEntity;
			maxSpeed = std::max(maxSpeed, part.body->GetLinearVelocity().Length());
		}
		skel->SetPhysicsPose(_frames, 1.0f);

		_timer += dt;
		_stillTimer = maxSpeed < 0.35f ? _stillTimer + dt : 0.0f;
		if (dt > 0.0f && ((_stillTimer > _downTime && _timer > 1.0f) || _timer > _maxDownTime))
			BeginRecovery();
	}

	void RagdollComponent::BeginRecovery()
	{
		Entity* e = GetEntity();
		// Where the body ended up: the pelvis, on the ground.
		math::Vector3 pelvis = e->GetWorldTM().Translation();
		std::vector<math::Matrix> lastWorld(_parts.size());
		for (size_t i = 0; i < _parts.size(); ++i)
		{
			if (_parts[i].body == nullptr)
				continue;
			lastWorld[i] = _parts[i].rel * PoseMatrix(_parts[i].body->GetPhysicsRotation(), _parts[i].body->GetPhysicsPosition());
			if (i == 0)
				pelvis = _parts[i].body->GetPhysicsPosition();
		}
		DestroyBodies();

		math::Vector3 standAt = e->GetWorldTM().Translation();
		standAt.x = pelvis.x;
		standAt.z = pelvis.z;
		RayHit hit;
		if (g_pEnv->_physicsSystem->RayCastScene(pelvis + math::Vector3(0.0f, 0.8f, 0.0f), math::Vector3(0.0f, -1.0f, 0.0f), 3.0f, &hit, nullptr) > 0)
			standAt.y = hit.position.y;
		if (auto* tf = e->GetComponent<Transform>())
			tf->SetPosition(standAt);
		if (auto* rb = e->GetComponent<RigidBody>(); rb != nullptr && rb->GetIRigidBody() != nullptr)
			rb->GetIRigidBody()->UpdatePosePosition(standAt);

		// Hold the lying pose (relative to the moved character) and fade back to animation.
		math::Matrix E = e->GetWorldTM();
		E._41 = standAt.x; E._42 = standAt.y; E._43 = standAt.z;   // don't trust a just-moved cached TM
		const math::Matrix toEntity = E.Invert();
		_frames.clear();
		for (size_t i = 0; i < _parts.size(); ++i)
			if (!_parts[i].node.empty())
				_frames[_parts[i].node] = lastWorld[i] * toEntity;
		_parts.clear();
		_state = State::Recovering;
		_blend = 1.0f;
	}

	void RagdollComponent::FinishRecovery()
	{
		if (auto* skel = GetEntity()->GetComponent<SkeletalAnimationComponent>())
			skel->ClearPhysicsPose();
		SetCharacterActive(true);
		_holding = false;   // re-applied next frame if "hold still" is on
		_frames.clear();
		_state = State::Standing;
		LOG_INFO("Ragdoll: '%s' back up", GetEntity()->GetName().c_str());
	}

	void RagdollComponent::CheckForHit()
	{
		Entity* e = GetEntity();
		Scene* scene = e->GetScene();
		if (scene == nullptr || e->HasFlag(EntityFlags::DoNotRender))
			return;   // hidden: e.g. sitting in their own car
		RefreshRide(scene);
		if (s_ride.vehicle == nullptr || s_ride.velocity.Length() < _hitSpeed)
			return;
		const math::Vector3 chest = e->GetWorldTM().Translation() + math::Vector3(0.0f, 0.9f, 0.0f);
		// Reach = the vehicle's box grown by the character's body radius plus a
		// frame's travel: the bodies collide (and the vehicle stops) before the
		// box itself reaches the character's centre.
		dx::BoundingBox reach = s_ride.box;
		reach.Extents.x += 0.8f;
		reach.Extents.y += 0.5f;
		reach.Extents.z += 0.8f;
		if (reach.Contains(chest) == dx::DISJOINT)
			return;
		// Only what's ahead of the vehicle (not someone it is pulling away from).
		const math::Vector3 toChar = chest - math::Vector3(s_ride.box.Center);
		if (toChar.Dot(s_ride.velocity) < 0.0f)
			return;
		Activate(s_ride.velocity * _throwScale + math::Vector3(0.0f, 1.8f, 0.0f));
	}

	void RagdollComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);
		if (g_pEnv == nullptr || !g_pEnv->IsGameRunning())
		{
			// Play stopped while knocked down: the stop already destroyed the
			// body entities (play-time spawns), but nothing reset us - the NPC
			// stayed in its frozen limp pose in the editor with its capsule
			// unsimulated. None of this is serialised, so the restore can't help.
			if (_state != State::Standing)
			{
				DestroyBodies();	// joints; bodies are looked up by name (already gone)
				_parts.clear();
				_frames.clear();
				if (auto* skel = GetEntity()->GetComponent<SkeletalAnimationComponent>())
					skel->ClearPhysicsPose();
				SetCharacterActive(true);
				_state = State::Standing;
			}
			_holding = false;
			return;
		}

		switch (_state)
		{
		case State::Standing:
			// "Hold still": pause the character's routine so it idles where it is.
			if (_holdStill != _holding)
			{
				if (auto* agent = GetEntity()->GetComponentDerived<IRoutineAgentState>(); agent != nullptr)
					agent->SetConversationPaused(_holdStill);
				_holding = _holdStill;
			}
			CheckForHit();
			break;
		case State::Ragdoll:
			UpdateRagdoll(frameTime);
			break;
		case State::Recovering:
		{
			_blend -= frameTime / 0.7f;
			if (_blend <= 0.0f)
			{
				FinishRecovery();
				break;
			}
			if (auto* skel = GetEntity()->GetComponent<SkeletalAnimationComponent>())
				skel->SetPhysicsPose(_frames, _blend);
			break;
		}
		}
	}

	void RagdollComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_hitSpeed);
		SERIALIZE_VALUE(_downTime);
		SERIALIZE_VALUE(_maxDownTime);
		SERIALIZE_VALUE(_throwScale);
		SERIALIZE_VALUE(_holdStill);
	}

	void RagdollComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		DESERIALIZE_VALUE(_hitSpeed);
		DESERIALIZE_VALUE(_downTime);
		DESERIALIZE_VALUE(_maxDownTime);
		DESERIALIZE_VALUE(_throwScale);
		DESERIALIZE_VALUE(_holdStill);
	}

	bool RagdollComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t w = widget->GetSize().x - 20;
		auto* hit = new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Knockdown speed (m/s)", &_hitSpeed, 0.5f, 40.0f, 0.1f, 1);
		hit->SetPrefabOverrideBinding(GetComponentName(), "/_hitSpeed");
		auto* down = new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Time down once still (s)", &_downTime, 0.0f, 30.0f, 0.1f, 1);
		down->SetPrefabOverrideBinding(GetComponentName(), "/_downTime");
		auto* maxDown = new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Max time down (s)", &_maxDownTime, 1.0f, 60.0f, 0.5f, 1);
		maxDown->SetPrefabOverrideBinding(GetComponentName(), "/_maxDownTime");
		auto* throwScale = new DragFloat(widget, widget->GetNextPos(), Point(w, 18), L"Throw (x vehicle speed)", &_throwScale, 0.0f, 3.0f, 0.05f, 2);
		throwScale->SetPrefabOverrideBinding(GetComponentName(), "/_throwScale");
		auto* hold = new Checkbox(widget, widget->GetNextPos(), Point(w, 18), L"Hold still (idle in place)", &_holdStill);
		hold->SetPrefabOverrideBinding(GetComponentName(), "/_holdStill");
		new Button(widget, widget->GetNextPos(), Point(w, 20), L"Knock down (play mode)", [this](Button*)
		{
			math::Vector3 back = math::Vector3::TransformNormal(math::Vector3(0.0f, 0.0f, -1.0f), GetEntity()->GetWorldTM());
			back.y = 0.0f;
			if (back.LengthSquared() > 1e-6f)
				back.Normalize();
			Activate(back * 4.0f + math::Vector3(0.0f, 2.0f, 0.0f));
			return true;
		});
		return true;
	}
}
