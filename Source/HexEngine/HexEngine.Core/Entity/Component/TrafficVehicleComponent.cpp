#include "TrafficVehicleComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../GUI/Elements/Button.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/EntitySearch.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Math/FloatMath.hpp"

namespace HexEngine
{
	std::vector<TrafficVehicleComponent*> TrafficVehicleComponent::s_allVehicles;

	namespace
	{
		struct WaypointGraphNode
		{
			math::Vector3 position = math::Vector3::Zero;
			std::vector<std::string> neighbours;
		};

		void DrawPointMarker(const math::Vector3& position, float extent, const math::Color& colour)
		{
			dx::BoundingBox marker;
			marker.Center = position;
			marker.Extents = math::Vector3(extent);
			g_pEnv->_debugRenderer->DrawAABB(marker, colour);
		}

		void AddWaypointEdge(std::unordered_map<std::string, WaypointGraphNode>& graph, const std::string& from, const std::string& to)
		{
			if (from.empty() || to.empty() || from == to)
				return;

			auto fromIt = graph.find(from);
			auto toIt = graph.find(to);
			if (fromIt == graph.end() || toIt == graph.end())
				return;

			auto& neighbours = fromIt->second.neighbours;
			if (std::find(neighbours.begin(), neighbours.end(), to) == neighbours.end())
				neighbours.push_back(to);
		}

		bool TryResolveNextLane(Scene* scene, TrafficLaneComponent* currentLane, std::string& outNextLaneName, TrafficLaneComponent*& outNextLane)
		{
			outNextLaneName.clear();
			outNextLane = nullptr;

			if (scene == nullptr || currentLane == nullptr)
				return false;

			const auto& configuredNext = currentLane->GetNextLaneEntityNames();
			if (configuredNext.empty())
				return false;

			// Keep round-robin behaviour, but skip invalid/deleted targets instead of immediately failing.
			for (size_t attempt = 0; attempt < configuredNext.size(); ++attempt)
			{
				const std::string candidateName = currentLane->GetNextLaneEntityName();
				if (candidateName.empty())
					continue;

				auto* nextLaneEntity = scene->GetEntityByName(candidateName);
				if (nextLaneEntity == nullptr || nextLaneEntity->IsPendingDeletion())
					continue;

				auto* nextLane = nextLaneEntity->GetComponent<TrafficLaneComponent>();
				if (nextLane == nullptr)
					continue;

				outNextLaneName = candidateName;
				outNextLane = nextLane;
				return true;
			}

			return false;
		}
	}

	TrafficVehicleComponent::TrafficVehicleComponent(Entity* entity) :
		UpdateComponent(entity)
	{
		s_allVehicles.push_back(this);
	}

	TrafficVehicleComponent::TrafficVehicleComponent(Entity* entity, TrafficVehicleComponent* copy) :
		UpdateComponent(entity, copy)
	{
		if (copy != nullptr)
		{
			_laneEntityName = copy->_laneEntityName;
			_targetIndex = copy->_targetIndex;
			_speed = copy->_speed;
			_acceleration = copy->_acceleration;
			_rotationLerp = copy->_rotationLerp;
			_arrivalDistance = copy->_arrivalDistance;
			_currentSpeed = 0.0f;
			_useLaneSpeedLimit = copy->_useLaneSpeedLimit;
			_invertDirection = copy->_invertDirection;
			_despawnAtRouteEnd = copy->_despawnAtRouteEnd;
			_useWaypointRoute = copy->_useWaypointRoute;
			_startWaypointEntityName = copy->_startWaypointEntityName;
			_destinationWaypointEntityName = copy->_destinationWaypointEntityName;
			_drawDebug = copy->_drawDebug;
			_avoidanceEnabled = copy->_avoidanceEnabled;
			_avoidanceLookAheadDistance = copy->_avoidanceLookAheadDistance;
			_avoidanceFollowDistance = copy->_avoidanceFollowDistance;
			_brakingStrength = copy->_brakingStrength;
		}

		s_allVehicles.push_back(this);
	}

	TrafficVehicleComponent::~TrafficVehicleComponent()
	{
		s_allVehicles.erase(std::remove(s_allVehicles.begin(), s_allVehicles.end(), this), s_allVehicles.end());
	}

	void TrafficVehicleComponent::SetLaneEntityName(const std::string& laneEntityName)
	{
		_laneEntityName = laneEntityName;
		RestartPath();
	}

	void TrafficVehicleComponent::SetWaypointRouteEndpoints(const std::string& startWaypointEntityName, const std::string& destinationWaypointEntityName)
	{
		_startWaypointEntityName = startWaypointEntityName;
		_destinationWaypointEntityName = destinationWaypointEntityName;
	}

	bool TrafficVehicleComponent::ConsumeRouteEndReachedEvent()
	{
		const bool reached = _routeEndReachedEvent;
		_routeEndReachedEvent = false;
		return reached;
	}

	void TrafficVehicleComponent::RestartPath()
	{
		_targetIndex = 0;
		_plannedRoutePoints.clear();
		_routeEndReachedEvent = false;

		if (_useWaypointRoute)
		{
			std::vector<math::Vector3> route;
			if (BuildWaypointRoute(route) && route.size() >= 2)
			{
				_plannedRoutePoints = std::move(route);
			}
		}

		if (_invertDirection)
		{
			std::vector<math::Vector3> points;
			if (GatherPlannedRoute(points) && !points.empty())
			{
				_targetIndex = points.size() - 1;
			}
		}

		_currentSpeed = 0.0f;
	}

	bool TrafficVehicleComponent::GatherPlannedRoute(std::vector<math::Vector3>& outPoints)
	{
		outPoints.clear();

		if (_useWaypointRoute && _plannedRoutePoints.size() >= 2)
		{
			outPoints = _plannedRoutePoints;
			return true;
		}

		return GatherLanePoints(outPoints);
	}

	bool TrafficVehicleComponent::BuildWaypointRoute(std::vector<math::Vector3>& outPoints) const
	{
		outPoints.clear();

		auto* entity = GetEntity();
		auto* scene = entity != nullptr ? entity->GetScene() : nullptr;
		if (scene == nullptr)
			return false;

		if (_startWaypointEntityName.empty() || _destinationWaypointEntityName.empty())
			return false;

		auto* startEntity = scene->GetEntityByName(_startWaypointEntityName);
		auto* destinationEntity = scene->GetEntityByName(_destinationWaypointEntityName);
		if (startEntity == nullptr || destinationEntity == nullptr)
			return false;

		std::unordered_map<std::string, WaypointGraphNode> graph;
		std::unordered_map<Entity*, std::vector<Entity*>> laneWaypointCache;

		std::vector<TrafficLaneComponent*> lanes;
		scene->GetComponents<TrafficLaneComponent>(lanes);

		for (auto* lane : lanes)
		{
			if (lane == nullptr || lane->GetEntity() == nullptr || lane->GetEntity()->IsPendingDeletion())
				continue;

			std::vector<Entity*> laneWaypoints;
			lane->GatherLaneWaypointEntities(laneWaypoints);
			laneWaypointCache[lane->GetEntity()] = laneWaypoints;

			for (auto* waypoint : laneWaypoints)
			{
				if (waypoint == nullptr || waypoint->IsPendingDeletion())
					continue;

				auto& node = graph[waypoint->GetName()];
				node.position = waypoint->GetWorldTM().Translation();
			}
		}

		for (auto* lane : lanes)
		{
			if (lane == nullptr || lane->GetEntity() == nullptr || lane->GetEntity()->IsPendingDeletion())
				continue;

			const auto cacheIt = laneWaypointCache.find(lane->GetEntity());
			if (cacheIt == laneWaypointCache.end())
				continue;

			const auto& laneWaypoints = cacheIt->second;
			if (laneWaypoints.empty())
				continue;

			for (size_t i = 0; i + 1 < laneWaypoints.size(); ++i)
			{
				auto* a = laneWaypoints[i];
				auto* b = laneWaypoints[i + 1];
				if (a == nullptr || b == nullptr || a->IsPendingDeletion() || b->IsPendingDeletion())
					continue;

				AddWaypointEdge(graph, a->GetName(), b->GetName());
				AddWaypointEdge(graph, b->GetName(), a->GetName());
			}

			if (lane->IsLooping() && laneWaypoints.size() > 2)
			{
				auto* first = laneWaypoints.front();
				auto* last = laneWaypoints.back();
				if (first != nullptr && last != nullptr && !first->IsPendingDeletion() && !last->IsPendingDeletion())
				{
					AddWaypointEdge(graph, last->GetName(), first->GetName());
					AddWaypointEdge(graph, first->GetName(), last->GetName());
				}
			}

			const auto& nextLaneNames = lane->GetNextLaneEntityNames();
			if (!nextLaneNames.empty())
			{
				auto* laneEntity = lane->GetEntity();
				auto* laneTerminal = laneWaypoints.back();
				if (laneEntity != nullptr && laneTerminal != nullptr && !laneTerminal->IsPendingDeletion())
				{
					for (const auto& nextLaneName : nextLaneNames)
					{
						if (nextLaneName.empty())
							continue;

						auto* nextLaneEntity = scene->GetEntityByName(nextLaneName);
						if (nextLaneEntity == nullptr || nextLaneEntity->IsPendingDeletion())
							continue;

						auto* nextLane = nextLaneEntity->GetComponent<TrafficLaneComponent>();
						if (nextLane == nullptr)
							continue;

						const auto nextCacheIt = laneWaypointCache.find(nextLaneEntity);
						if (nextCacheIt == laneWaypointCache.end())
							continue;

						const auto& nextWaypoints = nextCacheIt->second;
						if (nextWaypoints.empty() || nextWaypoints.front() == nullptr || nextWaypoints.front()->IsPendingDeletion())
							continue;

						AddWaypointEdge(graph, laneTerminal->GetName(), nextWaypoints.front()->GetName());
					}
				}
			}
		}

		const std::string startName = startEntity->GetName();
		const std::string destinationName = destinationEntity->GetName();
		if (graph.find(startName) == graph.end() || graph.find(destinationName) == graph.end())
			return false;

		struct OpenNode
		{
			float f = 0.0f;
			std::string name;
		};
		auto cmp = [](const OpenNode& a, const OpenNode& b) { return a.f > b.f; };
		std::priority_queue<OpenNode, std::vector<OpenNode>, decltype(cmp)> open(cmp);
		std::unordered_map<std::string, float> gScore;
		std::unordered_map<std::string, std::string> cameFrom;
		std::unordered_set<std::string> closed;

		for (const auto& [name, node] : graph)
			gScore[name] = std::numeric_limits<float>::infinity();

		gScore[startName] = 0.0f;
		open.push({ (graph[startName].position - graph[destinationName].position).Length(), startName });

		while (!open.empty())
		{
			const OpenNode current = open.top();
			open.pop();

			if (closed.find(current.name) != closed.end())
				continue;
			closed.insert(current.name);

			if (current.name == destinationName)
				break;

			const auto currentIt = graph.find(current.name);
			if (currentIt == graph.end())
				continue;

			const auto& currentNode = currentIt->second;
			for (const auto& neighbourName : currentNode.neighbours)
			{
				const auto neighbourIt = graph.find(neighbourName);
				if (neighbourIt == graph.end())
					continue;

				const float travelCost = (neighbourIt->second.position - currentNode.position).Length();
				const float tentativeG = gScore[current.name] + travelCost;
				if (tentativeG >= gScore[neighbourName])
					continue;

				cameFrom[neighbourName] = current.name;
				gScore[neighbourName] = tentativeG;
				const float h = (neighbourIt->second.position - graph[destinationName].position).Length();
				open.push({ tentativeG + h, neighbourName });
			}
		}

		if (startName != destinationName && cameFrom.find(destinationName) == cameFrom.end())
			return false;

		std::vector<std::string> pathNames;
		pathNames.push_back(destinationName);
		while (pathNames.back() != startName)
		{
			auto it = cameFrom.find(pathNames.back());
			if (it == cameFrom.end())
				return false;
			pathNames.push_back(it->second);
		}
		std::reverse(pathNames.begin(), pathNames.end());

		outPoints.reserve(pathNames.size());
		for (const auto& name : pathNames)
		{
			auto it = graph.find(name);
			if (it != graph.end())
				outPoints.push_back(it->second.position);
		}

		return outPoints.size() >= 2;
	}

	bool TrafficVehicleComponent::AdvancePlannedRouteIndex(size_t numPoints)
	{
		if (numPoints < 2)
		{
			_targetIndex = 0;
			return true;
		}

		if (_invertDirection)
		{
			if (_targetIndex == 0)
				return true;

			--_targetIndex;
			return false;
		}

		++_targetIndex;
		if (_targetIndex >= numPoints)
		{
			_targetIndex = numPoints - 1;
			return true;
		}

		return false;
	}

	TrafficLaneComponent* TrafficVehicleComponent::ResolveLane()
	{
		auto* entity = GetEntity();
		if (entity == nullptr)
			return nullptr;

		auto* scene = entity->GetScene();
		if (scene == nullptr)
			return nullptr;

		if (_laneEntityName.empty())
			return nullptr;

		auto* laneEntity = scene->GetEntityByName(_laneEntityName);
		if (laneEntity == nullptr || laneEntity->IsPendingDeletion())
			return nullptr;

		return laneEntity->GetComponent<TrafficLaneComponent>();
	}

	bool TrafficVehicleComponent::GatherLanePoints(std::vector<math::Vector3>& outPoints)
	{
		auto* lane = ResolveLane();
		if (lane == nullptr)
			return false;

		lane->GatherLanePoints(outPoints);
		if (outPoints.size() >= 2)
			return true;

		// Lane graph can be authored as explicit next-node links.
		// Build a deterministic forward chain to provide at least 2 points for non-route driving mode.
		auto* scene = GetEntity() != nullptr ? GetEntity()->GetScene() : nullptr;
		if (scene == nullptr || outPoints.empty())
			return false;

		std::unordered_set<std::string> visited;
		auto* cursor = lane;
		while (cursor != nullptr && cursor->GetEntity() != nullptr)
		{
			if (!visited.insert(cursor->GetEntity()->GetName()).second)
				break;

			std::string nextName;
			TrafficLaneComponent* nextLane = nullptr;
			if (!TryResolveNextLane(scene, cursor, nextName, nextLane))
				break;

			auto* nextEntity = nextLane->GetEntity();
			if (nextEntity == nullptr || nextEntity->IsPendingDeletion())
				break;

			outPoints.push_back(nextEntity->GetWorldTM().Translation());
			if (outPoints.size() >= 2)
				return true;

			cursor = nextLane;
		}

		return outPoints.size() >= 2;
	}

	bool TrafficVehicleComponent::TrySwitchToConnectedLane()
	{
		auto* currentLane = ResolveLane();
		if (currentLane == nullptr)
			return false;

		auto* scene = GetEntity()->GetScene();
		if (scene == nullptr)
			return false;

		std::string nextLaneName;
		TrafficLaneComponent* nextLane = nullptr;
		if (!TryResolveNextLane(scene, currentLane, nextLaneName, nextLane))
			return false;

		std::vector<math::Vector3> points;
		nextLane->GatherLanePoints(points);
		if (points.empty())
			return false;

		const math::Vector3 vehiclePos = GetEntity()->GetPosition();
		const float startDistanceSq = (points.front() - vehiclePos).LengthSquared();
		const float endDistanceSq = (points.back() - vehiclePos).LengthSquared();

		_laneEntityName = nextLaneName;
		if (points.size() >= 2)
		{
			_invertDirection = (endDistanceSq < startDistanceSq);
			_targetIndex = _invertDirection ? (points.size() - 1) : 0;
		}
		else
		{
			_invertDirection = false;
			_targetIndex = 0;
		}
		return true;
	}

	bool TrafficVehicleComponent::AdvanceTargetIndex(size_t numPoints)
	{
		auto* lane = ResolveLane();
		if (lane == nullptr)
		{
			_targetIndex = 0;
			return true;
		}

		if (numPoints < 2)
		{
			if (!TrySwitchToConnectedLane())
			{
				// If next lanes are configured but currently unresolved, keep retrying instead of treating as route end.
				if (lane->GetNextLaneEntityNames().empty() == false)
				{
					_targetIndex = 0;
					return false;
				}

				_targetIndex = 0;
				return true;
			}
			return false;
		}

		if (_invertDirection)
		{
			if (_targetIndex == 0)
			{
				// In lane-graph mode, explicit next-lane links take precedence over local looping.
				if (TrySwitchToConnectedLane())
				{
					return false;
				}

				if (lane->IsLooping())
				{
					_targetIndex = numPoints - 1;
					return false;
				}
				else
				{
					if (lane->GetNextLaneEntityNames().empty() == false)
					{
						_targetIndex = 0;
						return false;
					}

					_targetIndex = 0;
					return true;
				}
				return false;
			}
			else
			{
				--_targetIndex;
				return false;
			}
		}
		else
		{
			++_targetIndex;
			if (_targetIndex >= numPoints)
			{
				// In lane-graph mode, explicit next-lane links take precedence over local looping.
				if (TrySwitchToConnectedLane())
				{
					return false;
				}

				if (lane->IsLooping())
				{
					_targetIndex = 0;
					return false;
				}
				else
				{
					if (lane->GetNextLaneEntityNames().empty() == false)
					{
						_targetIndex = numPoints - 1;
						return false;
					}

					_targetIndex = numPoints - 1;
					return true;
				}
				return false;
			}
		}

		return false;
	}

	float TrafficVehicleComponent::ComputeAvoidanceSpeed(const math::Vector3& currentPosition, const math::Vector3& moveDirection, float maxSpeed) const
	{
		if (!_avoidanceEnabled)
			return maxSpeed;

		const float lookAhead = std::max(_avoidanceLookAheadDistance, 0.01f);
		const float followDistance = std::clamp(_avoidanceFollowDistance, 0.0f, lookAhead);
		const float laneWidth = 3.5f;

		float minAllowedSpeed = maxSpeed;
		float nearestAheadDistance = std::numeric_limits<float>::max();

		for (auto* other : s_allVehicles)
		{
			if (other == nullptr || other == this)
				continue;

			if (other->GetEntity() == nullptr || other->GetEntity()->IsPendingDeletion())
				continue;

			const math::Vector3 toOther = other->GetEntity()->GetWorldTM().Translation() - currentPosition;
			const float projectedDistance = toOther.Dot(moveDirection);
			if (projectedDistance > lookAhead || projectedDistance < -(followDistance * 0.5f))
				continue;

			const float lateralDistanceSq = std::max(0.0f, toOther.LengthSquared() - projectedDistance * projectedDistance);
			const float lateralDistance = sqrtf(lateralDistanceSq);
			if (lateralDistance > laneWidth)
				continue;

			if (projectedDistance <= followDistance && projectedDistance >= 0.0f)
				return 0.0f;

			if (projectedDistance >= 0.0f)
			{
				nearestAheadDistance = std::min(nearestAheadDistance, projectedDistance);
			}
		}

		if (nearestAheadDistance == std::numeric_limits<float>::max())
			return maxSpeed;

		if (nearestAheadDistance <= followDistance)
			return 0.0f;

		const float denom = std::max(lookAhead - followDistance, 0.001f);
		const float alpha = std::clamp((nearestAheadDistance - followDistance) / denom, 0.0f, 1.0f);
		minAllowedSpeed = std::min(minAllowedSpeed, maxSpeed * alpha);
		return minAllowedSpeed;
	}

	void TrafficVehicleComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		if (frameTime <= 0.0f)
			return;

		auto* transform = GetEntity()->GetComponent<Transform>();
		if (transform == nullptr)
			return;

		std::vector<math::Vector3> points;
		if (!GatherPlannedRoute(points))
			return;

		_targetIndex = std::min(_targetIndex, points.size() - 1);
		math::Vector3 targetPoint = points[_targetIndex];
		math::Vector3 currentPosition = transform->GetPosition();

		math::Vector3 toTarget = targetPoint - currentPosition;
		float distanceToTarget = toTarget.Length();

		const float arrivalDistance = std::max(_arrivalDistance, 0.01f);
		if (distanceToTarget <= arrivalDistance)
		{
			const bool reachedRouteEnd = _useWaypointRoute
				? AdvancePlannedRouteIndex(points.size())
				: AdvanceTargetIndex(points.size());

			if (reachedRouteEnd)
			{
				if (_despawnAtRouteEnd)
				{
					GetEntity()->DeleteMe();
				}
				else
				{
					_routeEndReachedEvent = true;
				}
				_currentSpeed = 0.0f;
				return;
			}

			if (!GatherPlannedRoute(points))
				return;

			_targetIndex = std::min(_targetIndex, points.size() - 1);
			targetPoint = points[_targetIndex];
			toTarget = targetPoint - currentPosition;
			distanceToTarget = toTarget.Length();
		}

		if (distanceToTarget <= 0.001f)
			return;

		toTarget.Normalize();

		auto* lane = ResolveLane();
		const float laneSpeed = lane != nullptr ? lane->GetSpeedLimit() : _speed;
		const float maxSpeed = std::max(_useLaneSpeedLimit ? laneSpeed : _speed, 0.0f);
		if (maxSpeed <= 0.0f)
		{
			_currentSpeed = 0.0f;
			return;
		}

		const float desiredSpeed = ComputeAvoidanceSpeed(currentPosition, toTarget, maxSpeed);
		const float accel = std::max(_acceleration, 0.0f);
		const float brake = std::max(_brakingStrength, 0.0f);

		if (_currentSpeed < desiredSpeed)
		{
			_currentSpeed = std::min(_currentSpeed + accel * frameTime, desiredSpeed);
		}
		else
		{
			_currentSpeed = std::max(_currentSpeed - brake * frameTime, desiredSpeed);
		}

		currentPosition += toTarget * (_currentSpeed * frameTime);
		transform->SetPositionNoNotify(currentPosition);

		const float yaw = atan2f(toTarget.x, toTarget.z);
		const auto targetRotation = math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
		const float rotationBlend = std::clamp(frameTime * std::max(_rotationLerp, 0.0f), 0.0f, 1.0f);
		transform->SetRotationNoNotify(math::Quaternion::Slerp(transform->GetRotation(), targetRotation, rotationBlend));
	}

	void TrafficVehicleComponent::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "_laneEntityName", _laneEntityName);
		file->Serialize(data, "_speed", _speed);
		file->Serialize(data, "_acceleration", _acceleration);
		file->Serialize(data, "_rotationLerp", _rotationLerp);
		file->Serialize(data, "_arrivalDistance", _arrivalDistance);
		file->Serialize(data, "_useLaneSpeedLimit", _useLaneSpeedLimit);
		file->Serialize(data, "_invertDirection", _invertDirection);
		file->Serialize(data, "_despawnAtRouteEnd", _despawnAtRouteEnd);
		file->Serialize(data, "_useWaypointRoute", _useWaypointRoute);
		file->Serialize(data, "_startWaypointEntityName", _startWaypointEntityName);
		file->Serialize(data, "_destinationWaypointEntityName", _destinationWaypointEntityName);
		file->Serialize(data, "_drawDebug", _drawDebug);
		file->Serialize(data, "_avoidanceEnabled", _avoidanceEnabled);
		file->Serialize(data, "_avoidanceLookAheadDistance", _avoidanceLookAheadDistance);
		file->Serialize(data, "_avoidanceFollowDistance", _avoidanceFollowDistance);
		file->Serialize(data, "_brakingStrength", _brakingStrength);
	}

	void TrafficVehicleComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		file->Deserialize(data, "_laneEntityName", _laneEntityName);
		file->Deserialize(data, "_speed", _speed);
		file->Deserialize(data, "_acceleration", _acceleration);
		file->Deserialize(data, "_rotationLerp", _rotationLerp);
		file->Deserialize(data, "_arrivalDistance", _arrivalDistance);
		file->Deserialize(data, "_useLaneSpeedLimit", _useLaneSpeedLimit);
		file->Deserialize(data, "_invertDirection", _invertDirection);
		file->Deserialize(data, "_despawnAtRouteEnd", _despawnAtRouteEnd);
		file->Deserialize(data, "_useWaypointRoute", _useWaypointRoute);
		file->Deserialize(data, "_startWaypointEntityName", _startWaypointEntityName);
		file->Deserialize(data, "_destinationWaypointEntityName", _destinationWaypointEntityName);
		file->Deserialize(data, "_drawDebug", _drawDebug);
		file->Deserialize(data, "_avoidanceEnabled", _avoidanceEnabled);
		file->Deserialize(data, "_avoidanceLookAheadDistance", _avoidanceLookAheadDistance);
		file->Deserialize(data, "_avoidanceFollowDistance", _avoidanceFollowDistance);
		file->Deserialize(data, "_brakingStrength", _brakingStrength);
		RestartPath();
	}

	bool TrafficVehicleComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* laneName = new EntitySearch(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Lane Entity");
		laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
		laneName->SetOnInputFn([this](EntitySearch* search, const std::wstring& value)
		{
			_laneEntityName = std::string(value.begin(), value.end());
			RestartPath();
		});
		laneName->SetOnSelectFn([this](EntitySearch* search, const EntitySearchResult& result)
		{
			_laneEntityName = result.entityName;
			RestartPath();
		});
		laneName->SetPrefabOverrideBinding(GetComponentName(), "/_laneEntityName");

		new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Use Parent Lane", [this, laneName](Button* button) -> bool
		{
			auto* parent = GetEntity() != nullptr ? GetEntity()->GetParent() : nullptr;
			if (parent == nullptr || parent->GetComponent<TrafficLaneComponent>() == nullptr)
				return false;

			_laneEntityName = parent->GetName();
			laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
			RestartPath();
			return true;
		});

		auto* startWaypoint = new EntitySearch(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Route Start Waypoint");
		startWaypoint->SetValue(std::wstring(_startWaypointEntityName.begin(), _startWaypointEntityName.end()));
		startWaypoint->SetOnInputFn([this](EntitySearch* search, const std::wstring& value)
		{
			_startWaypointEntityName = std::string(value.begin(), value.end());
			RestartPath();
		});
		startWaypoint->SetOnSelectFn([this](EntitySearch* search, const EntitySearchResult& result)
		{
			_startWaypointEntityName = result.entityName;
			RestartPath();
		});

		auto* destinationWaypoint = new EntitySearch(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Route Destination");
		destinationWaypoint->SetValue(std::wstring(_destinationWaypointEntityName.begin(), _destinationWaypointEntityName.end()));
		destinationWaypoint->SetOnInputFn([this](EntitySearch* search, const std::wstring& value)
		{
			_destinationWaypointEntityName = std::string(value.begin(), value.end());
			RestartPath();
		});
		destinationWaypoint->SetOnSelectFn([this](EntitySearch* search, const EntitySearchResult& result)
		{
			_destinationWaypointEntityName = result.entityName;
			RestartPath();
		});

		auto* speed = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Speed", &_speed, 0.0f, 5000.0f, 0.1f, 2);
		auto* acceleration = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Acceleration", &_acceleration, 0.0f, 10000.0f, 0.1f, 2);
		auto* braking = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Braking Strength", &_brakingStrength, 0.0f, 10000.0f, 0.1f, 2);
		auto* rotationLerp = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Rotation Lerp", &_rotationLerp, 0.0f, 100.0f, 0.1f, 2);
		auto* arrivalDistance = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Arrival Distance", &_arrivalDistance, 0.01f, 500.0f, 0.1f, 2);
		auto* useLaneSpeed = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Use Lane Speed", &_useLaneSpeedLimit);
		auto* invertDirection = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Invert Direction", &_invertDirection);
		auto* despawnAtRouteEnd = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Despawn At Route End", &_despawnAtRouteEnd);
		auto* useWaypointRoute = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Use Waypoint Route", &_useWaypointRoute);
		auto* avoidanceEnabled = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Avoidance Enabled", &_avoidanceEnabled);
		auto* avoidLookAhead = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Avoid Look Ahead", &_avoidanceLookAheadDistance, 0.1f, 1000.0f, 0.1f, 2);
		auto* avoidFollow = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Avoid Follow Dist", &_avoidanceFollowDistance, 0.0f, 1000.0f, 0.1f, 2);
		auto* drawDebug = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);

		new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Rebuild Route", [this](Button* button) -> bool
		{
			RestartPath();
			return true;
		});

		speed->SetPrefabOverrideBinding(GetComponentName(), "/_speed");
		acceleration->SetPrefabOverrideBinding(GetComponentName(), "/_acceleration");
		braking->SetPrefabOverrideBinding(GetComponentName(), "/_brakingStrength");
		rotationLerp->SetPrefabOverrideBinding(GetComponentName(), "/_rotationLerp");
		arrivalDistance->SetPrefabOverrideBinding(GetComponentName(), "/_arrivalDistance");
		useLaneSpeed->SetPrefabOverrideBinding(GetComponentName(), "/_useLaneSpeedLimit");
		invertDirection->SetPrefabOverrideBinding(GetComponentName(), "/_invertDirection");
		despawnAtRouteEnd->SetPrefabOverrideBinding(GetComponentName(), "/_despawnAtRouteEnd");
		useWaypointRoute->SetPrefabOverrideBinding(GetComponentName(), "/_useWaypointRoute");
		startWaypoint->SetPrefabOverrideBinding(GetComponentName(), "/_startWaypointEntityName");
		destinationWaypoint->SetPrefabOverrideBinding(GetComponentName(), "/_destinationWaypointEntityName");
		avoidanceEnabled->SetPrefabOverrideBinding(GetComponentName(), "/_avoidanceEnabled");
		avoidLookAhead->SetPrefabOverrideBinding(GetComponentName(), "/_avoidanceLookAheadDistance");
		avoidFollow->SetPrefabOverrideBinding(GetComponentName(), "/_avoidanceFollowDistance");
		drawDebug->SetPrefabOverrideBinding(GetComponentName(), "/_drawDebug");

		return true;
	}

	void TrafficVehicleComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		if (!g_pEnv->IsEditorMode())
			return;

		if (!_drawDebug && !isSelected)
			return;

		std::vector<math::Vector3> points;
		if (!GatherPlannedRoute(points) || points.empty())
			return;

		_targetIndex = std::min(_targetIndex, points.size() - 1);
		const math::Vector3 from = GetEntity()->GetWorldTM().Translation();
		const math::Vector3 to = points[_targetIndex];

		g_pEnv->_debugRenderer->DrawLine(from, to, math::Color(HEX_RGB_TO_FLOAT3(52, 152, 219), 1.0f));
		DrawPointMarker(to, 0.4f, math::Color(HEX_RGB_TO_FLOAT3(52, 152, 219), 1.0f));

		if (_useWaypointRoute && points.size() > 1)
		{
			for (size_t i = 0; i + 1 < points.size(); ++i)
			{
				g_pEnv->_debugRenderer->DrawLine(points[i], points[i + 1], math::Color(HEX_RGBA_TO_FLOAT4(52, 152, 219, 90)));
			}
		}
	}
}
