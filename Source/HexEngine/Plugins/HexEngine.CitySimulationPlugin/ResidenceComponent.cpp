#include "ResidenceComponent.hpp"
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>

ResidenceComponent::ResidenceComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
}

ResidenceComponent::ResidenceComponent(HexEngine::Entity* entity, ResidenceComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_householdCapacity = copy->_householdCapacity;
		_entryWaypointEntityName = copy->_entryWaypointEntityName;
		_parkingWaypointEntityName = copy->_parkingWaypointEntityName;
	}
}

void ResidenceComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_householdCapacity", _householdCapacity);
	file->Serialize(data, "_entryWaypointEntityName", _entryWaypointEntityName);
	file->Serialize(data, "_parkingWaypointEntityName", _parkingWaypointEntityName);
}

void ResidenceComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_householdCapacity", _householdCapacity);
	file->Deserialize(data, "_entryWaypointEntityName", _entryWaypointEntityName);
	file->Deserialize(data, "_parkingWaypointEntityName", _parkingWaypointEntityName);
}

bool ResidenceComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* capacity = new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Household Capacity", &_householdCapacity, 1, 1024, 1);

	auto* entry = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Entry Waypoint");
	entry->SetValue(std::wstring(_entryWaypointEntityName.begin(), _entryWaypointEntityName.end()));
	entry->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_entryWaypointEntityName = std::string(value.begin(), value.end());
		});
	entry->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_entryWaypointEntityName = result.entityName;
		});

	auto* parking = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Parking Waypoint");
	parking->SetValue(std::wstring(_parkingWaypointEntityName.begin(), _parkingWaypointEntityName.end()));
	parking->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_parkingWaypointEntityName = std::string(value.begin(), value.end());
		});
	parking->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_parkingWaypointEntityName = result.entityName;
		});

	capacity->SetPrefabOverrideBinding(GetComponentName(), "/_householdCapacity");
	entry->SetPrefabOverrideBinding(GetComponentName(), "/_entryWaypointEntityName");
	parking->SetPrefabOverrideBinding(GetComponentName(), "/_parkingWaypointEntityName");

	return true;
}

