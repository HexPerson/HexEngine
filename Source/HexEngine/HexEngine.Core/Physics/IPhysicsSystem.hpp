
#pragma once

#include "../Plugin/IPlugin.hpp"
#include "IRigidBody.hpp"
#include "IController.hpp"
#include "../Entity/Component/Transform.hpp"

namespace HexEngine
{
	class RigidBody;
	class Material;

	struct ControllerParameters
	{
		math::Vector3 initialPosition;
		float maxSlope;
		float height;
		float radius;
		float density;
		float stepOffset;
	};

	/** @brief Which test produced a RayHit. */
	enum class RayHitSource : uint8_t
	{
		None = 0,
		Mesh,          // exact triangle test against a StaticMeshComponent mesh
		Collider,      // the entity's own RigidBody collider (PhysUtils' per-entity path)
		PhysicsScene,  // scene-wide physics query (mesh-less colliders, e.g. volumetric terrain chunks)
	};

	inline const char* RayHitSourceName(RayHitSource source)
	{
		switch (source)
		{
		case RayHitSource::Mesh:         return "mesh";
		case RayHitSource::Collider:     return "collider";
		case RayHitSource::PhysicsScene: return "physics";
		default:                         return "none";
		}
	}

	// NOTE: changing this struct is an ABI change - Core and every plugin that
	// uses RayHit (PhysX, VolumetricTerrain, Weather, EditorBridge, game DLLs)
	// must be rebuilt together.
	struct RayHit
	{
		math::Vector3 start;
		math::Vector3 position;
		math::Vector3 normal;        // world-space front-face (geometric) normal, unit length
		math::Vector2 uv;            // interpolated texcoord at the hit point (mesh ray-tri path only; (0,0) otherwise)
		Entity* entity = nullptr;
		float distance = 0.0f;
		std::weak_ptr<Material> material;

		math::Vector3 smoothNormal;  // interpolated vertex normal, world space (mesh path); == normal otherwise
		RayHitSource source = RayHitSource::None;
		bool backFace = false;       // the ray struck the back of the surface (normal faces along the ray)
		uint32_t triangleIndex = UINT32_MAX; // mesh path: index of the hit triangle in the mesh's index list / 3
	};

	/** @brief Filter for IPhysicsSystem::RayCastSceneFiltered. Rejected
	 *  colliders / surfaces are passed THROUGH: the query keeps looking for
	 *  the nearest hit behind them. */
	struct RaySceneFilter
	{
		// Return false to make the ray ignore every collider of this entity.
		// entity is null for a collider with no owning entity. Called from
		// inside the physics query (physics scene read-locked): keep it cheap
		// and don't touch the physics system from it.
		bool (*acceptEntity)(Entity* entity, void* context) = nullptr;
		void* context = nullptr;

		// Pass through surfaces whose world normal.y is below this (e.g. 0.5 =
		// skip walls steeper than 60 degrees). <= -1 disables it.
		float minNormalY = -2.0f;

		// Convenience: never hit this body (e.g. the caster's own).
		IRigidBody* ignoreBody = nullptr;
	};

	enum class JointType
	{
		Hinge
	};

	class IPhysicsSystem : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IPhysicsSystem, 001);

		virtual IRigidBody* CreateRigidBody(Transform* transform, RigidBody* bodyComponent, IRigidBody::BodyType type, const math::Vector3& offset = math::Vector3::Zero) = 0;

		virtual IRigidBody* CloneRigidBody(IRigidBody* physicsBody, Transform* transform, RigidBody* bodyComponent, IRigidBody::BodyType type) = 0;

		virtual void DestroyRigidBody(IRigidBody* body) = 0;

		virtual void Update(float frameTime) = 0;

		virtual void Simulate(float simulationTime) = 0;

		virtual void DebugRender() = 0;

		virtual IRigidBody* CreateController(const ControllerParameters& params, Transform* transform) = 0;

		virtual void LockWrite() = 0;

		virtual void UnlockWrite() = 0;

		virtual uint32_t RayCast(const math::Vector3& from, const math::Vector3& unitDir, float maxDist, IRigidBody* body, RayHit* hitInfo) = 0;

		// Scene-wide raycast against EVERY collider in the physics scene. Unlike
		// RayCast (a single body), this finds colliders added straight to the
		// PhysX scene - e.g. volumetric terrain chunks, which a StaticMeshComponent
		// scan can't see (PhysUtils::RayCast merges these in via
		// RayCastSceneFiltered when RayCastOptions::physics is set). Pass ignoreBody to skip the caster's own body. Returns
		// the number of blocking hits (0 or 1). Default impl returns 0 so
		// alternate physics backends still compile.
		virtual uint32_t RayCastScene(const math::Vector3& from, const math::Vector3& unitDir, float maxDist, RayHit* hitInfo, IRigidBody* ignoreBody = nullptr)
		{
			(void)from; (void)unitDir; (void)maxDist; (void)hitInfo; (void)ignoreBody;
			return 0;
		}

		virtual math::Vector3 GetGravity() = 0;

		virtual void SetGravity(const math::Vector3& gravity) = 0;

		virtual void CreateHingeJoint(IRigidBody* bodies[2], const math::Vector3 axes[2], const math::Vector3 offsets[2]) = 0;

		// RayCastScene with per-entity filtering and slope pass-through, the
		// way PhysUtils::RayCast needs it. Rejected colliders don't stop the
		// ray. Returns 0 or 1. The default implementation (for backends that
		// don't override it) runs RayCastScene and DROPS a rejected nearest
		// hit rather than looking behind it - correct, just less complete.
		// Declared after every pre-existing virtual so the vtable layout of
		// the methods above is unchanged.
		virtual uint32_t RayCastSceneFiltered(const math::Vector3& from, const math::Vector3& unitDir, float maxDist, RayHit* hitInfo, const RaySceneFilter& filter)
		{
			if (hitInfo == nullptr)
				return 0;
			RayHit hit;
			if (RayCastScene(from, unitDir, maxDist, &hit, filter.ignoreBody) == 0)
				return 0;
			if (filter.acceptEntity != nullptr && !filter.acceptEntity(hit.entity, filter.context))
				return 0;
			if (filter.minNormalY > -1.0f && hit.normal.y < filter.minNormalY)
				return 0;
			*hitInfo = hit;
			return 1;
		}

		// ---- Ragdoll joints (declared last: earlier vtable slots unchanged) ----
		// A ball-and-socket joint between two dynamic bodies with a cone (swing)
		// limit and a twist limit. `jointWorld` is the joint frame in world space
		// (SimpleMath row-vector convention); its local X is the twist axis (point
		// it along the child limb). The joined bodies don't collide with each
		// other. Returns an opaque handle for DestroyJoint, or null if unsupported.
		virtual void* CreateConeJoint(IRigidBody* parent, IRigidBody* child, const math::Matrix& jointWorld,
			float swingDegrees, float twistDegrees)
		{
			(void)parent; (void)child; (void)jointWorld; (void)swingDegrees; (void)twistDegrees;
			return nullptr;
		}
		virtual void DestroyJoint(void* joint) { (void)joint; }
	};
}
