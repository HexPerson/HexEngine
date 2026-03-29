#include "TrafficSpawnerComponent.hpp"
#include "TrafficLaneComponent.hpp"
#include "../Entity.hpp"
#include "../../Graphics/DebugRenderer.hpp"
#include "../../GUI/Elements/ArrayElement.hpp"
#include "../../GUI/Elements/AssetSearch.hpp"
#include "../../GUI/Elements/Button.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/DragInt.hpp"
#include "../../GUI/Elements/LineEdit.hpp"
#include "../../Scene/Scene.hpp"
#include <algorithm>
#include <format>

namespace HexEngine
{
	namespace
	{
		void DrawPointMarker(const math::Vector3& position, float extent, const math::Color& colour)
		{
			dx::BoundingBox marker;
			marker.Center = position;
			marker.Extents = math::Vector3(extent);
			g_pEnv->_debugRenderer->DrawAABB(marker, colour);
		}
	}

	TrafficSpawnerComponent::TrafficSpawnerComponent(Entity* entity) :
		BaseComponent(entity)
	{
		RegisterEntityListener();
	}

	TrafficSpawnerComponent::TrafficSpawnerComponent(Entity* entity, TrafficSpawnerComponent* copy) :
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

	void TrafficSpawnerComponent::Serialize(json& data, JsonFile* file)
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

	void TrafficSpawnerComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
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

	bool TrafficSpawnerComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* laneName = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Lane Entity");
		laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
		laneName->SetDoesCallbackWaitForReturn(false);
		laneName->SetOnInputFn([this](LineEdit* edit, const std::wstring& value)
		{
			_laneEntityName = std::string(value.begin(), value.end());
		});

		new ArrayElement<std::string>(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 20, 132),
			L"Vehicle Prefabs",
			_vehiclePrefabPaths,
			[](Element* parent, std::string& item, int32_t index)
			{
				auto* prefabSearch = new AssetSearch(
					parent,
					Point(0, 0),
					Point(parent->GetSize().x, 22),
					L"Prefab",
					{ ResourceType::Prefab });

				prefabSearch->SetValue(std::wstring(item.begin(), item.end()));
				prefabSearch->SetOnSelectFn([&item](AssetSearch* search, const AssetSearchResult& result)
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

		new Button(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Use Parent Lane", [this, laneName](Button* button) -> bool
		{
			auto* parent = GetEntity() != nullptr ? GetEntity()->GetParent() : nullptr;
			if (parent == nullptr || parent->GetComponent<TrafficLaneComponent>() == nullptr)
				return false;

			_laneEntityName = parent->GetName();
			laneName->SetValue(std::wstring(_laneEntityName.begin(), _laneEntityName.end()));
			return true;
		});

		auto* enabled = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Enabled", &_enabled);
		auto* despawnInactive = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Despawn Inactive", &_despawnWhenInactive);
		auto* drawDebug = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Draw Debug", &_drawDebug);
		auto* interval = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Spawn Interval (s)", &_spawnIntervalSeconds, 0.05f, 600.0f, 0.1f, 2);
		auto* maxActive = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Max Active", &_maxActiveVehicles, 0, 10000, 1);
		auto* activationDistance = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Activation Distance", &_activationDistance, 1.0f, 50000.0f, 1.0f, 1);
		auto* spawnClearanceDistance = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Spawn Clearance", &_spawnClearanceDistance, 0.0f, 5000.0f, 0.1f, 2);

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

	void TrafficSpawnerComponent::OnRemoveEntity(Entity* entity)
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
		if (!g_pEnv->IsEditorMode())
			return;

		if (!_drawDebug && !isSelected)
			return;

		const math::Vector3 origin = GetEntity()->GetWorldTM().Translation();
		DrawPointMarker(origin, 0.35f, math::Color(HEX_RGB_TO_FLOAT3(230, 126, 34), 1.0f));

		dx::BoundingBox activationBox;
		activationBox.Center = origin;
		activationBox.Extents = math::Vector3(std::max(_activationDistance, 0.0f));
		g_pEnv->_debugRenderer->DrawAABB(activationBox, math::Color(HEX_RGBA_TO_FLOAT4(230, 126, 34, 80)));
	}
}
