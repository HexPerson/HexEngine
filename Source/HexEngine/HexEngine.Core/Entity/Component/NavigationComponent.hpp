
#pragma once

#include "UpdateComponent.hpp"
#include "../../Scene/INavMeshProvider.hpp"
#include "../../GUI/DebugGUI.hpp"
#include "../../Input/InputSystem.hpp"

namespace HexEngine
{
	class ComponentWidget;

	class HEX_API NavigationComponent : public UpdateComponent, public IDebugGUICallback, public IInputListener
	{
	public:
		CREATE_COMPONENT_ID(NavigationComponent);
		DEFINE_COMPONENT_CTOR(NavigationComponent);

		//using ReachedDestinationFn = void (BaseComponent*);

		virtual ~NavigationComponent();

		void FindPath(NavMeshId id, const math::Vector3& from, const math::Vector3& to, float stepSize);

		virtual void Update(float dt) override;

		virtual void OnDebugGUI() override;

		// Editor test controls: pick a target by clicking in the scene, walk to it,
		// tune speeds.
		virtual bool CreateWidget(ComponentWidget* widget) override;
		// One-shot scene click after BeginPickTarget(); sets the target + paths to it.
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

		// Persist the tuning fields so an authored value (e.g. foot offset on an NPC
		// prefab) survives save/load - CitySim reuses an existing NavigationComponent
		// rather than re-adding one, so prefab-authored values reach spawned agents.
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		void SetRotationSpeed(float speed) { _rotationSpeed = speed; }
		void SetMovementSpeed(float speed) { _movementSpeed = speed; }
		// Arm a one-shot scene pick: the next left-click in the scene sets the target
		// and starts pathing to it. Used by the editor widget for testing.
		void BeginPickTarget();
		// Path from the entity's current position to _targetPosition on the scene navmesh.
		void GoToTarget();
		void ClearPath();
		bool HasPath() const { return !_result.path.empty(); }
		const std::vector<math::Vector3>& GetPathPoints() const { return _result.path; }
		uint32_t GetPathIndex() const { return _pathIndex; }

		// Optional global movement gate, installed by a gameplay plugin (e.g. a
		// pedestrian crossing held by a traffic signal). Asked before an agent
		// walks from its position toward the next path node; returning false
		// holds the agent where it is this frame. A plain function pointer (not
		// std::function) so nothing in Core ever runs plugin-owned destructors.
		using SegmentGateFn = bool (*)(const math::Vector3& from, const math::Vector3& to);
		static void SetSegmentGate(SegmentGateFn gate);
		// True while the gate is holding this agent (it wants to move but waits).
		bool IsHeldByGate() const { return _heldByGate; }

		//void SetReachedDestinationFn(std::function<ReachedDestinationFn> fn) { _reachedDestinationFn = fn; }

	private:
		// Resolves the ground Y under navPos via a throttled, distance-LOD'd downward
		// raycast (cached between probes); returns false (use navmesh Y) for far agents
		// or a missed ray.
		bool ResolveGroundY(const math::Vector3& navPos, float& outY);

		math::Vector3 _targetPosition;
		INavMeshProvider::PathResult _result;
		uint32_t _pathIndex = 0;
		//math::Quaternion _currentRotation;
		math::Quaternion _targetRotation;
		float _rotationTime = 0.0f;
		float _rotationSpeed = 1.25f;
		bool _hasNewRotation = false;
		float _targetYaw = 0.0f;
		float _movementSpeed = 5.0f;
		// Lowers the placed transform by this many world units, to seat models whose
		// origin sits below their feet on the ground (model-pivot correction). Applied
		// on top of the ground height (probed or navmesh).
		float _footOffset = 0.0f;

		// Ground snapping: probe straight down to the real surface and place the agent
		// there (+ foot offset) instead of on the navmesh height. Fixes stairs/slopes,
		// where the navmesh is a smoothed ramp above the actual steps. Mitigated for
		// crowds: only agents within _groundProbeMaxDistance of the camera probe, and
		// each re-probes only every _groundProbeInterval frames (cached in between).
		bool    _groundSnap            = true;
		float   _groundProbeMaxDistance = 60.0f; // distance LOD (0 = always probe)
		int32_t _groundProbeInterval    = 3;     // frames between probes per agent

		// Ground-probe runtime cache.
		int32_t _groundProbeCounter = 0;
		float   _cachedGroundY      = 0.0f;
		bool    _hasCachedGroundY   = false;

		bool _awaitingPick = false; // armed by BeginPickTarget(); next scene click sets the target
		bool _heldByGate = false;   // runtime: the segment gate refused this frame's step

		//std::function<ReachedDestinationFn> _reachedDestinationFn;
	};
}
