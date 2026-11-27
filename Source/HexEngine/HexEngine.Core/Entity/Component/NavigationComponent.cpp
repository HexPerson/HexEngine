
#include "NavigationComponent.hpp"
#include "Transform.hpp"
#include "../../Input/HVar.hpp"
#include "Camera.hpp"
#include "../Entity.hpp"
#include "../../Math/FloatMath.hpp"
#include "../../Math/easing.h"
#include "../../GUI/UIManager.hpp"
#include "../../Scene/SceneManager.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Physics/PhysUtils.hpp"
#include "../../Physics/IPhysicsSystem.hpp"
#include "../../Environment/LogFile.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/DragInt.hpp"
#include "../../GUI/Elements/Button.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/Vector3Edit.hpp"

namespace HexEngine
{
	namespace
	{
		NavigationComponent::SegmentGateFn s_segmentGate = nullptr;
		// Every live agent, for the avoidance neighbour scan (ctor/dtor maintained).
		std::vector<NavigationComponent*> s_agents;
	}

	HVar nav_avoid("nav_avoid", "Pedestrian avoidance: agents step around each other and the player", true, false, true);
	HVar nav_avoidRadius("nav_avoidRadius", "Personal-space radius between agents (m)", 0.9f, 0.2f, 3.0f);
	HVar nav_avoidPlayerRadius("nav_avoidPlayerRadius", "Personal-space radius around the player (m)", 1.4f, 0.2f, 4.0f);
	HVar nav_avoidMaxOffset("nav_avoidMaxOffset", "Furthest an agent may drift sideways off its path line while avoiding (m)", 0.6f, 0.0f, 2.0f);

	void NavigationComponent::SetSegmentGate(SegmentGateFn gate)
	{
		s_segmentGate = gate;
	}

	NavigationComponent::NavigationComponent(Entity* entity) :
		UpdateComponent(entity)
	{
		s_agents.push_back(this);

		//g_pEnv->_debugGui->AddCallback(this);
	}

	NavigationComponent::NavigationComponent(Entity* entity, NavigationComponent* copy) :
		UpdateComponent(entity, copy)
	{
		s_agents.push_back(this);
		//g_pEnv->_debugGui->AddCallback(this);

		// Carry config across prefab spawn / duplicate (clone path doesn't deserialize).
		if (copy != nullptr)
		{
			_movementSpeed          = copy->_movementSpeed;
			_rotationSpeed          = copy->_rotationSpeed;
			_footOffset             = copy->_footOffset;
			_groundSnap             = copy->_groundSnap;
			_groundProbeMaxDistance = copy->_groundProbeMaxDistance;
			_groundProbeInterval    = copy->_groundProbeInterval;
		}
	}

	NavigationComponent::~NavigationComponent()
	{
		if (auto it = std::find(s_agents.begin(), s_agents.end(), this); it != s_agents.end())
		{
			*it = s_agents.back();
			s_agents.pop_back();
		}
		//g_pEnv->_debugGui->RemoveCallback(this);

		// Drop the one-shot pick listener if we were still waiting for a click.
		if (_awaitingPick && g_pEnv != nullptr && g_pEnv->_inputSystem != nullptr)
			g_pEnv->_inputSystem->RemoveInputListener(this);
	}

	HVar nav_logPartialPaths("nav_logPartialPaths", "Log every FindPath whose target is not reachable (agent name, from, to)", false, false, true);

	void NavigationComponent::FindPath(NavMeshId id, const math::Vector3& from, const math::Vector3& to, float stepSize)
	{
		_result.path.clear();
		_pathIndex = 0;

		math::Vector3 dirFromEndToStart = (from - to);
		dirFromEndToStart.Normalize();

		INavMeshProvider::PathParams path;
		path.from = from;
		path.to = to;
		path.searchDistance = math::Vector3(50.0f, 15.0f, 50.0f);
		path.stepSize = stepSize;
		path.meshId = id;

		_targetPosition = to;

		_result.partial = false;
		g_pEnv->_navMeshProvider->FindPath(path, _result);

		if (_result.partial && nav_logPartialPaths._val.b && GetEntity() != nullptr)
			LOG_INFO("nav partial: '%s' (%.1f %.1f %.1f) -> (%.1f %.1f %.1f)", GetEntity()->GetName().c_str(),
				from.x, from.y, from.z, to.x, to.y, to.z);
	}

	void NavigationComponent::ClearPath()
	{
		_result.path.clear();
		_pathIndex = 0;
		_heldByGate = false;
	}

	void NavigationComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_movementSpeed);
		SERIALIZE_VALUE(_rotationSpeed);
		SERIALIZE_VALUE(_footOffset);
		SERIALIZE_VALUE(_groundSnap);
		SERIALIZE_VALUE(_groundProbeMaxDistance);
		SERIALIZE_VALUE(_groundProbeInterval);
	}

	void NavigationComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		DESERIALIZE_VALUE(_movementSpeed);
		DESERIALIZE_VALUE(_rotationSpeed);
		DESERIALIZE_VALUE(_footOffset);
		DESERIALIZE_VALUE(_groundSnap);
		DESERIALIZE_VALUE(_groundProbeMaxDistance);
		DESERIALIZE_VALUE(_groundProbeInterval);
	}

	void NavigationComponent::BeginPickTarget()
	{
		if (_awaitingPick || g_pEnv == nullptr || g_pEnv->_inputSystem == nullptr)
			return;

		_awaitingPick = true;
		g_pEnv->_inputSystem->AddInputListener(this, InputEvent::MouseDown);
		LOG_INFO("NavigationComponent: click in the scene to set the navigation target.");
	}

	void NavigationComponent::GoToTarget()
	{
		auto scene = (g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr)
			? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
		if (!scene)
			return;

		auto* tf = GetEntity()->GetComponent<Transform>();
		if (tf == nullptr)
			return;

		FindPath(scene->GetNavMeshId(), tf->GetPosition(), _targetPosition, 1.0f);

		if (!HasPath())
			LOG_WARN("NavigationComponent: no path to [%.2f %.2f %.2f] (target off the navmesh, or no scene navmesh built?).",
				_targetPosition.x, _targetPosition.y, _targetPosition.z);
	}

	bool NavigationComponent::ResolveGroundY(const math::Vector3& navPos, float& outY)
	{
		// Distance LOD: far agents skip the probe (their hover is invisible at range)
		// and fall back to the navmesh height.
		if (_groundProbeMaxDistance > 0.0f && g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr)
		{
			if (auto scene = g_pEnv->_sceneManager->GetCurrentScene(); scene)
			{
				Camera* cam = scene->GetMainCamera();
				if (cam != nullptr && cam->GetEntity() != nullptr)
				{
					const float d2 = (cam->GetEntity()->GetPosition() - navPos).LengthSquared();
					if (d2 > _groundProbeMaxDistance * _groundProbeMaxDistance)
					{
						_hasCachedGroundY = false; // re-probe fresh when it returns to range
						return false;
					}
				}
			}
		}

		// Throttle: re-probe only every _groundProbeInterval frames, reuse the cache
		// otherwise. (Pedestrians move slowly, so a few frames of height lag is invisible.)
		if (_hasCachedGroundY && _groundProbeCounter > 0)
		{
			--_groundProbeCounter;
			outY = _cachedGroundY;
			return true;
		}
		_groundProbeCounter = std::max(0, _groundProbeInterval);

		// Probe straight down from just above the agent to the surface beneath it.
		const math::Ray ray(navPos + math::Vector3(0.0f, 1.0f, 0.0f), math::Vector3(0.0f, -1.0f, 0.0f));
		RayHit hit;

		// Ignore the agent's WHOLE prefab hierarchy, not just the entity carrying the
		// NavigationComponent. The doctor's visible/collision mesh can live on a child
		// (or a sibling under a shared prefab root), so ignoring only GetEntity() lets
		// the downward ray hit the agent's own body and stand it on top of itself -
		// each frame a little higher, which is the "agent flies up" runaway. Walk up to
		// the root, then collect the root + every descendant.
		std::vector<Entity*> ignore;
		Entity* root = GetEntity();
		while (root != nullptr && root->GetParent() != nullptr)
			root = root->GetParent();
		if (root != nullptr)
		{
			std::vector<Entity*> stack{ root };
			while (!stack.empty())
			{
				Entity* e = stack.back();
				stack.pop_back();
				ignore.push_back(e);
				for (Entity* child : e->GetChildren())
					stack.push_back(child);
			}
		}
		else
		{
			ignore.push_back(GetEntity());
		}

		if (PhysUtils::RayCast(ray, 6.0f,
			LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry), &hit, ignore))
		{
			// Belt-and-suspenders against a self-hit runaway: a straight-down floor probe
			// should never report a surface meaningfully ABOVE the agent's feet (the
			// ground directly below is at most the step it's standing on). If it does,
			// it's a spurious hit (overhead geometry or the agent's own body) - reject it
			// and fall back to the navmesh height rather than climbing onto it.
			const float kAboveFeetTolerance = 0.5f;
			if (hit.position.y > navPos.y + kAboveFeetTolerance)
			{
				_hasCachedGroundY = false;
				return false;
			}

			// ...nor far BELOW the navmesh. Walkable surfaces without colliders (city
			// roads are render-only and carved out of the navmesh) let the probe fall
			// through to the terrain metres beneath; standing the agent there hid it
			// under the street and, with the path nodes now metres above, stalled it.
			// The navmesh's smoothed ramp sits at most about half a stair flight off
			// the real steps, so this keeps stairs while rejecting the under-world.
			const float kBelowNavTolerance = 1.25f;
			if (hit.position.y < navPos.y - kBelowNavTolerance)
			{
				_hasCachedGroundY = false;
				return false;
			}

			_cachedGroundY = hit.position.y;
			_hasCachedGroundY = true;
			outY = _cachedGroundY;
			return true;
		}

		_hasCachedGroundY = false;
		return false;
	}

	bool NavigationComponent::OnInputEvent(InputEvent event, InputData* data)
	{
		if (!_awaitingPick || event != InputEvent::MouseDown || data == nullptr)
			return false;
		if (data->MouseDown.button != VK_LBUTTON)
			return false;

		_awaitingPick = false;
		if (g_pEnv != nullptr && g_pEnv->_inputSystem != nullptr)
			g_pEnv->_inputSystem->RemoveInputListener(this);

		auto scene = (g_pEnv != nullptr && g_pEnv->_sceneManager != nullptr)
			? g_pEnv->_sceneManager->GetCurrentScene() : nullptr;
		if (!scene)
			return false;

		Camera* cam = scene->GetMainCamera();
		if (cam == nullptr || cam->GetEntity() == nullptr)
			return false;

		// Ray from the camera through the clicked pixel, into the scene.
		const math::Vector3 origin = cam->GetEntity()->GetPosition();
		const math::Vector3 dir = g_pEnv->_inputSystem->GetScreenToWorldRay(cam, data->MouseDown.xpos, data->MouseDown.ypos);

		math::Ray ray(origin, dir);
		RayHit hit;
		// Skip the camera's own entity (and children): the ray starts inside it.
		if (PhysUtils::RayCast(ray, 100000.0f,
			LAYERMASK(Layer::StaticGeometry) | LAYERMASK(Layer::DynamicGeometry), &hit, { cam->GetEntity() }))
		{
			_targetPosition = hit.position;
			LOG_INFO("NavigationComponent: target set to [%.2f %.2f %.2f].",
				_targetPosition.x, _targetPosition.y, _targetPosition.z);
			// FindPath snaps the picked point to the nearest navmesh poly (search distance),
			// so a click slightly off the mesh still resolves.
			GoToTarget();
		}
		else
		{
			LOG_WARN("NavigationComponent: pick ray hit no geometry.");
		}

		return false; // don't consume - other listeners still see the click
	}

	bool NavigationComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Movement Speed",
			&_movementSpeed, 0.0f, 100.0f, 0.1f, 2);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Rotation Speed",
			&_rotationSpeed, 0.0f, 20.0f, 0.05f, 2);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Foot Offset (sink agent)",
			&_footOffset, -5.0f, 5.0f, 0.01f, 3);

		new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Ground Snap (probe surface)", &_groundSnap);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"  Probe Distance (LOD)",
			&_groundProbeMaxDistance, 0.0f, 500.0f, 1.0f, 0);
		new DragInt(widget, widget->GetNextPos(), Point(fullWidth, 18), L"  Probe Interval (frames)",
			&_groundProbeInterval, 0, 30, 1);

		new Vector3Edit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Target", &_targetPosition,
			[this](const math::Vector3& v) { _targetPosition = v; });

		new Button(widget, widget->GetNextPos(), Point(fullWidth, 22), L"Set Target (click in scene)",
			[this](Button*) { BeginPickTarget(); return true; });
		new Button(widget, widget->GetNextPos(), Point(fullWidth, 22), L"Go To Target",
			[this](Button*) { GoToTarget(); return true; });
		new Button(widget, widget->GetNextPos(), Point(fullWidth, 22), L"Stop",
			[this](Button*) { ClearPath(); return true; });

		return true;
	}

	void NavigationComponent::Update(float dt)
	{
		UpdateComponent::Update(dt);
		_moving = false;

		if (_paused)
			return;

		if (_result.path.size() > 0)
		{
			INavMeshProvider::MoveResult result = g_pEnv->_navMeshProvider->Move(_result);

			if (result == INavMeshProvider::MoveResult::Moving)
			{
				auto transform = GetEntity()->GetComponent<Transform>();
				if (transform == nullptr)
					return;

				if (_pathIndex >= _result.path.size())
				{
					_pathIndex = 0;
					_result.path.clear();
					return;
				}

				// Work in navmesh space: the transform is parked _footOffset BELOW the
				// navmesh while moving, so add it back to reason about path heights, then
				// re-apply the sink when we write the transform.
				auto currentPos = transform->GetPosition();
				currentPos.y += _footOffset;
				constexpr float kNodeArrivalDistance = 1.0f;
				constexpr float kMinDirectionEpsilon = 0.0001f;

				// Steer and arrive in the horizontal plane. Height comes from the navmesh
				// path (below) and the ground probe, never from the steering vector: with
				// a 3D delta, any vertical gap between the agent and the next node (an
				// agent the probe stood on lower ground, a node on a raised kerb) ate the
				// step - the agent crawled at a fraction of its speed - and could hold
				// the node out of arrival range forever.
				auto horizontalTo = [&](const math::Vector3& node)
				{
					math::Vector3 d = node - currentPos;
					d.y = 0.0f;
					return d;
				};

				auto delta = horizontalTo(_result.path[_pathIndex]);
				float length = delta.Length();

				// Advance path node before normalizing to avoid invalid vectors when already on a node.
				if (length <= kNodeArrivalDistance)
				{
					++_pathIndex;

					if (_pathIndex >= _result.path.size())
					{
						//if (_reachedDestinationFn)
						//	_reachedDestinationFn(this);

						NavigationTargetReachedMessage message;
						message.targetPosition = _targetPosition;
						message.finalPosition = currentPos;

						GetEntity()->OnMessage(&message, this);

						_pathIndex = 0;
						_result.path.clear();
						return;
					}

					delta = horizontalTo(_result.path[_pathIndex]);
					length = delta.Length();
				}

				// Movement gate (e.g. a pedestrian crossing at a red signal): wait
				// where we are until it opens. The path and index are kept.
				if (s_segmentGate != nullptr && !s_segmentGate(currentPos, _result.path[_pathIndex]))
				{
					_heldByGate = true;
					return;
				}
				_heldByGate = false;

				if (length <= kMinDirectionEpsilon)
					return;

				delta /= length;

				float moveScale = 1.0f;
				const math::Vector3 avoid = nav_avoid._val.b ? ComputeAvoidance(currentPos, delta, dt, moveScale) : math::Vector3(0.0f, 0.0f, 0.0f);

				const float moveDistance = std::min(length, std::max(0.0f, dt * _movementSpeed * moveScale));
				currentPos += delta * moveDistance;
				_moveDir = delta;
				_moving = true;

				if (avoid.LengthSquared() > 0.0f)
				{
					currentPos += avoid;
					// Stay within nav_avoidMaxOffset of the path line being walked
					// (previous node -> next node): avoidance must never push an agent
					// off the pavement into the road or a wall.
					const math::Vector3& b = _result.path[_pathIndex];
					const math::Vector3 a = _pathIndex > 0 ? _result.path[_pathIndex - 1] : b - delta * 2.0f;
					const math::Vector2 ab(b.x - a.x, b.z - a.z);
					const float abLenSq = ab.LengthSquared();
					if (abLenSq > 1e-6f)
					{
						const float t = std::clamp(((currentPos.x - a.x) * ab.x + (currentPos.z - a.z) * ab.y) / abLenSq, 0.0f, 1.0f);
						const math::Vector2 closest(a.x + ab.x * t, a.z + ab.y * t);
						math::Vector2 off(currentPos.x - closest.x, currentPos.z - closest.y);
						const float maxOff = nav_avoidMaxOffset._val.f32;
						if (off.Length() > maxOff)
						{
							off.Normalize();
							currentPos.x = closest.x + off.x * maxOff;
							currentPos.z = closest.y + off.y * maxOff;
						}
					}
				}

				// Navmesh height under the new position: interpolate along the segment
				// being walked (previous node -> current node) by horizontal progress.
				{
					const math::Vector3& b = _result.path[_pathIndex];
					if (_pathIndex > 0)
					{
						const math::Vector3& a = _result.path[_pathIndex - 1];
						const math::Vector2 ab(b.x - a.x, b.z - a.z);
						const float abLenSq = ab.LengthSquared();
						float t = 1.0f;
						if (abLenSq > 1e-6f)
							t = std::clamp(((currentPos.x - a.x) * ab.x + (currentPos.z - a.z) * ab.y) / abLenSq, 0.0f, 1.0f);
						currentPos.y = a.y + (b.y - a.y) * t;
					}
					else
					{
						currentPos.y = b.y;
					}
				}

				// Base height: the real surface under the agent (ground probe) so it
				// follows stairs/slopes - where the navmesh is a smoothed ramp above the
				// steps - falling back to the navmesh height. Then sink by the foot offset
				// for model-pivot correction.
				float baseY = currentPos.y; // navmesh-space fallback
				if (_groundSnap)
				{
					float groundY = baseY;
					if (ResolveGroundY(currentPos, groundY))
						baseY = groundY;
				}

				math::Vector3 placedPos(currentPos.x, baseY - _footOffset, currentPos.z);
				transform->SetPosition(placedPos);

				float yaw = atan2(delta.x, delta.z);
				
				auto targetRotation = math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);

				if (yaw != _targetYaw /*&& _hasNewRotation == false*/)
				{
					_rotationTime = 0.0f;
					_targetYaw = yaw;
					_targetRotation = targetRotation;
					_hasNewRotation = true;
				}

				_rotationTime += dt * _rotationSpeed;
				_rotationTime = std::clamp(_rotationTime, 0.0f, 1.0f);

				auto easingFunction = getEasingFunction(EaseOutQuart);

				//if(_hasNewRotation)
					transform->SetRotation(math::Quaternion::Slerp(transform->GetRotation(), _targetRotation, (float)easingFunction(_rotationTime)));

				if (_rotationTime >= 1.0f)
				{
					_hasNewRotation = false;
				}

				//transform->SetYaw(yaw);
				

				//_result.currentPos = currentPos;
			}
			/*else if (result == INavMeshProvider::MoveResult::ReachedDestination)
			{
				INavMeshProvider::PathParams path;
				path.from = _result.currentPos;
				path.to = math::Vector3(GetRandomFloat(-5000.0f, 5000.0f), 0.0f, GetRandomFloat(-5000.0f, 5000.0f));
				path.searchDistance = math::Vector3(500.0f);

				g_pEnv->_navMeshProvider->FindPath(path, _result);
			}
			else if (result == INavMeshProvider::MoveResult::Failed)
			{
				_hasPath = false;
			}*/
		}
	}

	math::Vector3 NavigationComponent::ComputeAvoidance(const math::Vector3& pos, const math::Vector3& dir, float dt, float& moveScale) const
	{
		moveScale = 1.0f;
		const math::Vector3 right(dir.z, 0.0f, -dir.x);   // dir rotated -90 deg about Y
		float lateral = 0.0f;       // + = step right
		float brake = 0.0f;         // how blocked the way ahead is (0..1)

		auto consider = [&](const math::Vector3& otherPos, const math::Vector3& otherDir, bool otherMoving, float radius, float weight)
		{
			math::Vector3 d(pos.x - otherPos.x, 0.0f, pos.z - otherPos.z);
			const float dist = d.Length();
			if (dist > radius * 3.0f || dist < 1e-4f)
				return;
			const float ahead = -d.Dot(dir);          // >0: the other is in front of us
			const float side = d.Dot(right);          // >0: we are to its right
			// Separation: inside personal space, push apart (sideways only - pushing
			// backwards along the path just makes agents stutter).
			if (dist < radius)
			{
				const float s = (radius - dist) / radius;
				lateral += (side >= 0.0f ? 1.0f : -1.0f) * s * weight;
				if (ahead > 0.0f)
					brake = std::max(brake, s * 0.8f);
			}
			// Head-on / overtaking: someone in front within ~2.5 radii and roughly in
			// our lane - pass on the right (or keep the side we're already on).
			if (ahead > 0.0f && ahead < radius * 2.5f && std::fabs(side) < radius)
			{
				const bool oncoming = !otherMoving || otherDir.Dot(dir) < -0.3f;
				const float closeness = 1.0f - ahead / (radius * 2.5f);
				if (oncoming)
					lateral += (std::fabs(side) < 0.15f ? 1.0f : (side > 0.0f ? 1.0f : -1.0f)) * closeness * 0.7f * weight;
				else if (otherDir.Dot(dir) > 0.5f && ahead < radius * 1.2f)
					brake = std::max(brake, closeness * 0.6f);   // same direction, slower in front: queue
			}
		};

		const float r = nav_avoidRadius._val.f32;
		for (const NavigationComponent* other : s_agents)
		{
			if (other == this || other == nullptr || other->GetEntity() == nullptr || other->GetEntity()->IsPendingDeletion())
				continue;
			// Cheap reject before any matrix work: entity transforms are world for root agents.
			const math::Vector3 op = other->GetEntity()->GetWorldTM().Translation();
			if (std::fabs(op.x - pos.x) > r * 3.0f || std::fabs(op.z - pos.z) > r * 3.0f || std::fabs(op.y + other->_footOffset - pos.y) > 2.0f)
				continue;
			const bool walking = other->_moving && !other->_paused && other->HasPath();
			consider(op, other->_moveDir, walking, r, 1.0f);
		}

		// The player (main camera's entity): NPCs give way fully.
		if (auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr)
			if (auto* cam = scene->GetMainCamera(); cam != nullptr && cam->GetEntity() != nullptr)
			{
				const math::Vector3 pp = cam->GetEntity()->GetWorldTM().Translation();
				if (std::fabs(pp.y - pos.y) < 3.0f)
					consider(pp, math::Vector3(0.0f, 0.0f, 0.0f), false, nav_avoidPlayerRadius._val.f32, 1.6f);
			}

		if (lateral == 0.0f && brake == 0.0f)
			return math::Vector3(0.0f, 0.0f, 0.0f);
		moveScale = std::clamp(1.0f - brake, 0.25f, 1.0f);
		// Sidestep speed: up to ~0.9x walking speed while squeezed.
		const float step = std::clamp(lateral, -1.0f, 1.0f) * _movementSpeed * 0.9f * dt;
		return right * step;
	}

	void NavigationComponent::OnDebugGUI()
	{
		auto renderer = g_pEnv->GetUIManager().GetRenderer();

		int32_t x, y;

		if (_result.path.size() > 0)
		{
			for (auto& r : _result.path)
			{
				if (g_pEnv->_inputSystem->GetWorldToScreenPosition(
					g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera(),
					r,
					x, y))
				{
					renderer->FillQuad(x - 1, y - 1, 2, 2, math::Color(HEX_RGB_TO_FLOAT3(255, 242, 0), 1.0f));
				}
			}
		}

		if (g_pEnv->_inputSystem->GetWorldToScreenPosition(
			g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera(),
			GetEntity()->GetPosition(),
			x, y))
		{
			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)Style::FontSize::Regular,
				x, y,
				math::Color(0xFFFFFFFF),
				FontAlign::CentreLR,
				std::format(L"Index: {}/{}", _pathIndex, _result.path.size()));

			renderer->PrintText(
				renderer->_style.font.get(),
				(uint8_t)Style::FontSize::Regular,
				x, y+20,
				math::Color(0xFFFFFFFF),
				FontAlign::CentreLR,
				std::format(L"Rotation: {:2f}", _rotationTime));
		}
	}
}
