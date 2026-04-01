#include "ResidenceComponent.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragInt.hpp"
#include "../../GUI/Elements/EntitySearch.hpp"

namespace HexEngine
{
	ResidenceComponent::ResidenceComponent(Entity* entity) :
		BaseComponent(entity)
	{
	}

	ResidenceComponent::ResidenceComponent(Entity* entity, ResidenceComponent* copy) :
		BaseComponent(entity)
	{
		if (copy != nullptr)
		{
			_householdCapacity = copy->_householdCapacity;
			_entryWaypointEntityName = copy->_entryWaypointEntityName;
			_parkingWaypointEntityName = copy->_parkingWaypointEntityName;
		}
	}

	void ResidenceComponent::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "_householdCapacity", _householdCapacity);
		file->Serialize(data, "_entryWaypointEntityName", _entryWaypointEntityName);
		file->Serialize(data, "_parkingWaypointEntityName", _parkingWaypointEntityName);
	}

	void ResidenceComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		file->Deserialize(data, "_householdCapacity", _householdCapacity);
		file->Deserialize(data, "_entryWaypointEntityName", _entryWaypointEntityName);
		file->Deserialize(data, "_parkingWaypointEntityName", _parkingWaypointEntityName);
	}

	bool ResidenceComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* capacity = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Household Capacity", &_householdCapacity, 1, 1024, 1);

		auto* entry = new EntitySearch(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Entry Waypoint");
		entry->SetValue(std::wstring(_entryWaypointEntityName.begin(), _entryWaypointEntityName.end()));
		entry->SetOnInputFn([this](EntitySearch* search, const std::wstring& value)
		{
			_entryWaypointEntityName = std::string(value.begin(), value.end());
		});
		entry->SetOnSelectFn([this](EntitySearch* search, const EntitySearchResult& result)
		{
			_entryWaypointEntityName = result.entityName;
		});

		auto* parking = new EntitySearch(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Parking Waypoint");
		parking->SetValue(std::wstring(_parkingWaypointEntityName.begin(), _parkingWaypointEntityName.end()));
		parking->SetOnInputFn([this](EntitySearch* search, const std::wstring& value)
		{
			_parkingWaypointEntityName = std::string(value.begin(), value.end());
		});
		parking->SetOnSelectFn([this](EntitySearch* search, const EntitySearchResult& result)
		{
			_parkingWaypointEntityName = result.entityName;
		});

		capacity->SetPrefabOverrideBinding(GetComponentName(), "/_householdCapacity");
		entry->SetPrefabOverrideBinding(GetComponentName(), "/_entryWaypointEntityName");
		parking->SetPrefabOverrideBinding(GetComponentName(), "/_parkingWaypointEntityName");

		return true;
	}
}
