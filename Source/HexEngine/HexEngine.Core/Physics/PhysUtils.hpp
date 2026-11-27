#pragma once

#include "../Required.hpp"
#include "../Environment/IEnvironment.hpp"
#include "IPhysicsSystem.hpp"
#include "../Entity/Entity.hpp"

namespace HexEngine
{
	class Scene;

	/** @brief Options for PhysUtils::RayCast(Scene*, ...).
	 *
	 * The ray reports the NEAREST accepted surface. Everything a filter rejects
	 * (an ignored entity, a hidden mesh, a layer outside the mask, a surface
	 * steeper than minNormalY) is passed straight through: whatever lies
	 * behind it can still be hit. */
	struct RayCastOptions
	{
		// Ray length in world units. <= 0 means unlimited (physics queries are
		// then capped at kUnlimitedDistance).
		float maxDistance = 10000.0f;

		// Entities whose layer is not in the mask are passed through. Applies
		// to every source; physics colliders with no owning entity have no
		// layer and are always eligible (unless `only` is set).
		LayerMask layerMask = ~LayerMask(0);

		// Exact triangle test against every StaticMeshComponent mesh (nearest
		// triangle, both faces). Animated meshes are skipped (their CPU
		// vertices are the bind pose, not what is drawn).
		bool meshes = true;

		// Mesh path only: an entity that also has a RigidBody is tested against
		// its COLLIDER instead of its triangles (the long-standing behaviour -
		// gameplay probes want what the player stands on). Set false for exact
		// visual hits (editor placement). A collider hit rejected by minNormalY
		// falls back to that entity's triangles.
		bool rigidBodyColliders = true;

		// Also query the physics scene (IPhysicsSystem::RayCastSceneFiltered),
		// which finds colliders that have no StaticMeshComponent, e.g.
		// volumetric terrain chunks. Also finds character-controller capsules,
		// so ignore the caster's own entity.
		bool physics = true;

		// Mesh path: include entities flagged EntityFlags::DoNotRender. Physics
		// colliders are physical and ignore this flag.
		bool includeHidden = false;

		// Pass through surfaces whose world normal.y is below this (0.5 = skip
		// anything steeper than 60 degrees). <= -1 disables it.
		float minNormalY = -2.0f;

		// ignore / only also cover every descendant of the listed entities.
		bool hierarchical = true;

		// Never hit these.
		std::unordered_set<Entity*> ignore;

		// When non-empty, hit ONLY these.
		std::unordered_set<Entity*> only;

		static constexpr float kUnlimitedDistance = 1.0e6f;
	};

	namespace PhysUtils
	{
		/** @brief Raycast an explicit scene.
		 *
		 * @param scene      the scene to test (e.g. the editor's edited scene - never
		 *                   assume GetCurrentScene(), which can be an internal preview
		 *                   scene). The scene is locked for the duration.
		 * @param origin     world-space ray origin
		 * @param direction  world-space direction (need not be unit length)
		 * @param options    filters, see RayCastOptions
		 * @param hitInfo    on success: position, world front-face normal, distance,
		 *                   entity, source, backFace, uv/material (mesh & collider
		 *                   paths), smoothNormal and triangleIndex (mesh path)
		 * @return true when something was hit
		 */
		bool HEX_API RayCast(Scene* scene, const math::Vector3& origin, const math::Vector3& direction, const RayCastOptions& options, RayHit* hitInfo);

		/** @brief Legacy form: the CURRENT scene, meshes only (no physics-scene
		 *  merge), visible entities in `mask`, `entsToIgnore` and their children
		 *  skipped. Stops at `to`. */
		bool HEX_API RayCast(const math::Vector3& from, const math::Vector3& to, LayerMask mask, RayHit* hitInfo, const std::vector<Entity*>& entsToIgnore = {});

		/** @brief Legacy form, as above; maxDistance <= 0 means unlimited. */
		bool HEX_API RayCast(const math::Ray& ray, float maxDistance, LayerMask mask, RayHit* hitInfo, const std::vector<Entity*>& entsToIgnore = {});
	}
}
