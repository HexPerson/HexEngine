#include "TrafficSpawnerComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/Graphics/DebugRenderer.hpp>
#include <HexEngine.Core/GUI/Elements/ArrayElement.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/Button.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/Scene/Scene.hpp>

namespace
{
	void DrawPointMarker(const math::Vector3& position, float extent, const math::Color& colour)
	{
		dx::BoundingBox marker;
		marker.Center = position;
		marker.Extents = math::Vector3(extent);
		HexEngine::g_pEnv->_debugRenderer->DrawAABB(marker, colour);
	}
}

TrafficSpawnerComponent::TrafficSpawnerComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
	RegisterEntityListener();
}

TrafficSpawnerComponent::TrafficSpawnerComponent(HexEngine::Entity* entity, TrafficSpawnerComponent* copy) :
	BaseComponent(entity)
{
	RegisterEntityListener();

	if (copy != nullptr)
	{
		_laneEntityName = copy->_laneEntityName;
		_vehiclePrefabPaths = copy->_vehiclePrefabPaths;
		_spawnIntervalSeconds = copy->_spawnIntervalSeconds;
		_maxActiveVehicles = copy->_maxActiveVehicles;
		_activationDistance = copy->_activationDistance;
		_spawnClearanceDistance = copy->_spawnClearanceDistance;
		_enabled = copy->_enabled;
		_despawnWhenInactive = copy->_despawnWhenInactive;
		_drawDebug = copy->_drawDebug;
	}
}

void TrafficSpawnerComponent::Destroy()
{
	UnregisterEntityListener();
	_activeVehicles.clear();
}

void TrafficSpawnerComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_laneEntityName", _laneEntityName);
	file->Serialize(data, "_vehiclePrefabPaths", _vehiclePrefabPaths);
	file->Serialize(data, "_spawnIntervalSeconds", _spawnIntervalSeconds);
	file->Serialize(data, "_maxActiveVehicles", _maxActiveVehicles);
	file->Serialize(data, "_activationDistance", _activationDistance);
	file->Serialize(data, "_spawnClearanceDistance", _spawnClearanceDistance);
	file->Serialize(data, "_enabled", _enabled);
	file->Serialize(data, "_despawnWhenInactive", _despawnWhenInactive);
	file->Serialize(data, "_drawDebug", _drawDebug);
}

void TrafficSpawnerComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	file->Deserialize(data, "_laneEntityName", _laneEntityName);
	file->Deserialize(data, "_vehiclePrefabPaths", _vehiclePrefabPaths);
	// Backward compatibility for older scenes that stored one template name.
	if (_vehiclePrefabPaths.empty())
	{
		std::string legacyTemplateName;
		file->Deserialize(data, "_vehicleTemplateEntityName", legacyTemplateName);
		if (!legacyTemplateName.empty())
			_vehiclePrefabPaths.push_back(legacyTemplateName);
	}
	file->Deserialize(data, "_spawnIntervalSeconds", _spawnIntervalSeconds);
	file->Deserialize(data, "_maxActiveVehicles", _maxActiveVehicles);
	file->Deserialize(data, "_activationDistance", _activationDistance);
	file->Deserialize(data, "_spawnClearanceDistance", _spawnClearanceDistance);
	file->Deserialize(data, "_enabled", _enabled);
	file->Deserialize(data, "_despawnWhenInactive", _despawnWhenInactive);
	file->Deserialize(data, "_drawDebug", _drawDebug);
	_spawnTimer = 0.0f;
	_activeVehicles.clear();
}

bool TrafficSpawnerComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* laneName = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Lane Entity");
	laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
	laneName->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_laneEntityName = std::string(value.begin(), value.end());
		});
	laneName->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_laneEntityName = result.entityName;
		});

	new HexEngine::ArrayElement<std::string>(
		widget,
		widget->GetNextPos(),
		HexEngine::Point(widget->GetSize().x - 20, 132),
		L"Vehicle Prefabs",
		_vehiclePrefabPaths,
		[](HexEngine::Element* parent, std::string& item, int32_t index)
		{
			auto* prefabSearch = new HexEngine::AssetSearch(
				parent,
				HexEngine::Point(0, 0),
				HexEngine::Point(parent->GetSize().x, 22),
				L"Prefab",
				{ HexEngine::ResourceType::Prefab });

			prefabSearch->SetValue(std::wstring(item.begin(), item.end()));
			prefabSearch->SetOnSelectFn([&item](HexEngine::AssetSearch* search, const HexEngine::AssetSearchResult& result)
				{
					const fs::path chosen = !result.assetPath.empty() ? result.assetPath : result.absolutePath;
					item = chosen.generic_string();
				});
		},
		[]() -> std::string
		{
			return std::string();
		},
		[](const std::string& item, int32_t index) -> int32_t
		{
			return 34;
		},
		[](const std::string& item, int32_t index) -> std::wstring
		{
			return std::format(L"Prefab {}", index + 1);
		});

	new HexEngine::Button(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Use Parent Lane", [this, laneName](HexEngine::Button* button) -> bool
		{
			auto* parent = GetEntity() != nullptr ? GetEntity()->GetParent() : nullptr;
			if (parent == nullptr || parent->GetComponent<TrafficLaneComponent>() == nullptr)
				return false;

			_laneEntityName = parent->GetName();
			laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
			return true;
		});

	auto* enabled = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Enabled", &_enabled);
	auto* despawnInactive = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Despawn Inactive", &_despawnWhenInactive);
	auto* drawDebug = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);
	auto* interval = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Spawn Interval (s)", &_spawnIntervalSeconds, 0.05f, 600.0f, 0.1f, 2);
	auto* maxActive = new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Max Active", &_maxActiveVehicles, 0, 10000, 1);
	auto* activationDistance = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Activation Distance", &_activationDistance, 1.0f, 50000.0f, 1.0f, 1);
	auto* spawnClearanceDistance = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Spawn Clearance", &_spawnClearanceDistance, 0.0f, 5000.0f, 0.1f, 2);

	laneName->SetPrefabOverrideBinding(GetComponentName(), "/_laneEntityName");
	enabled->SetPrefabOverrideBinding(GetComponentName(), "/_enabled");
	despawnInactive->SetPrefabOverrideBinding(GetComponentName(), "/_despawnWhenInactive");
	drawDebug->SetPrefabOverrideBinding(GetComponentName(), "/_drawDebug");
	interval->SetPrefabOverrideBinding(GetComponentName(), "/_spawnIntervalSeconds");
	maxActive->SetPrefabOverrideBinding(GetComponentName(), "/_maxActiveVehicles");
	activationDistance->SetPrefabOverrideBinding(GetComponentName(), "/_activationDistance");
	spawnClearanceDistance->SetPrefabOverrideBinding(GetComponentName(), "/_spawnClearanceDistance");

	return true;
}

void TrafficSpawnerComponent::OnRemoveEntity(HexEngine::Entity* entity)
{
	if (entity == nullptr || _activeVehicles.empty())
		return;

	_activeVehicles.erase(std::remove(_activeVehicles.begin(), _activeVehicles.end(), entity), _activeVehicles.end());
}

void TrafficSpawnerComponent::RegisterEntityListener()
{
	if (_isListeningForEntityRemovals)
		return;

	auto* owner = GetEntity();
	if (owner == nullptr || owner->GetScene() == nullptr)
		return;

	owner->GetScene()->AddEntityListener(this);
	_isListeningForEntityRemovals = true;
}

void TrafficSpawnerComponent::UnregisterEntityListener()
{
	if (!_isListeningForEntityRemovals)
		return;

	auto* owner = GetEntity();
	if (owner != nullptr && owner->GetScene() != nullptr)
	{
		owner->GetScene()->RemoveEntityListener(this);
	}

	_isListeningForEntityRemovals = false;
}

void TrafficSpawnerComponent::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
{
	if (!HexEngine::g_pEnv->IsEditorMode())
		return;

	if (!_drawDebug && !isSelected)
		return;

	const math::Vector3 origin = GetEntity()->GetWorldTM().Translation();
	DrawPointMarker(origin, 0.35f, math::Color(HEX_RGB_TO_FLOAT3(230, 126, 34), 1.0f));

	dx::BoundingBox activationBox;
	activationBox.Center = origin;
	activationBox.Extents = math::Vector3(std::max(_activationDistance, 0.0f));
	HexEngine::g_pEnv->_debugRenderer->DrawAABB(activationBox, math::Color(HEX_RGBA_TO_FLOAT4(230, 126, 34, 80)));
}

