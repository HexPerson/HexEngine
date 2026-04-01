#include "ServiceStationComponent.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/DragInt.hpp"
#include "../../GUI/Elements/EntitySearch.hpp"
#include "../../GUI/Elements/LineEdit.hpp"
#include <sstream>

namespace HexEngine
{
	namespace
	{
		std::string Trim(const std::string& text)
		{
			const auto begin = text.find_first_not_of(" \t\r\n");
			if (begin == std::string::npos)
				return {};
			const auto end = text.find_last_not_of(" \t\r\n");
			return text.substr(begin, end - begin + 1);
		}
	}

	ServiceStationComponent::ServiceStationComponent(Entity* entity) :
		BaseComponent(entity)
	{
		RebuildServiceTags();
	}

	ServiceStationComponent::ServiceStationComponent(Entity* entity, ServiceStationComponent* copy) :
		BaseComponent(entity)
	{
		if (copy != nullptr)
		{
			_serviceTagsCsv = copy->_serviceTagsCsv;
			_dispatchRadius = copy->_dispatchRadius;
			_priority = copy->_priority;
			_vehiclePrefabPath = copy->_vehiclePrefabPath;
			_entryWaypointEntityName = copy->_entryWaypointEntityName;
			_parkingWaypointEntityName = copy->_parkingWaypointEntityName;
		}
		RebuildServiceTags();
	}

	void ServiceStationComponent::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "_serviceTagsCsv", _serviceTagsCsv);
		file->Serialize(data, "_dispatchRadius", _dispatchRadius);
		file->Serialize(data, "_priority", _priority);
		file->Serialize(data, "_vehiclePrefabPath", _vehiclePrefabPath);
		file->Serialize(data, "_entryWaypointEntityName", _entryWaypointEntityName);
		file->Serialize(data, "_parkingWaypointEntityName", _parkingWaypointEntityName);
	}

	void ServiceStationComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		file->Deserialize(data, "_serviceTagsCsv", _serviceTagsCsv);
		file->Deserialize(data, "_dispatchRadius", _dispatchRadius);
		file->Deserialize(data, "_priority", _priority);
		file->Deserialize(data, "_vehiclePrefabPath", _vehiclePrefabPath);
		file->Deserialize(data, "_entryWaypointEntityName", _entryWaypointEntityName);
		file->Deserialize(data, "_parkingWaypointEntityName", _parkingWaypointEntityName);
		RebuildServiceTags();
	}

	bool ServiceStationComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* tags = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Service Tags (CSV)");
		tags->SetValue(std::wstring(_serviceTagsCsv.begin(), _serviceTagsCsv.end()));
		tags->SetDoesCallbackWaitForReturn(false);
		tags->SetOnInputFn([this](LineEdit* edit, const std::wstring& value)
		{
			_serviceTagsCsv = std::string(value.begin(), value.end());
			RebuildServiceTags();
		});

		auto* radius = new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Dispatch Radius", &_dispatchRadius, 1.0f, 1000000.0f, 1.0f, 1);
		auto* priority = new DragInt(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Priority", &_priority, -100, 100, 1);

		auto* vehicle = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Vehicle Prefab");
		vehicle->SetValue(std::wstring(_vehiclePrefabPath.begin(), _vehiclePrefabPath.end()));
		vehicle->SetDoesCallbackWaitForReturn(false);
		vehicle->SetOnInputFn([this](LineEdit* edit, const std::wstring& value)
		{
			_vehiclePrefabPath = std::string(value.begin(), value.end());
		});

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

		tags->SetPrefabOverrideBinding(GetComponentName(), "/_serviceTagsCsv");
		radius->SetPrefabOverrideBinding(GetComponentName(), "/_dispatchRadius");
		priority->SetPrefabOverrideBinding(GetComponentName(), "/_priority");
		vehicle->SetPrefabOverrideBinding(GetComponentName(), "/_vehiclePrefabPath");
		entry->SetPrefabOverrideBinding(GetComponentName(), "/_entryWaypointEntityName");
		parking->SetPrefabOverrideBinding(GetComponentName(), "/_parkingWaypointEntityName");

		return true;
	}

	void ServiceStationComponent::RebuildServiceTags()
	{
		_serviceTags.clear();
		std::stringstream stream(_serviceTagsCsv);
		std::string token;
		while (std::getline(stream, token, ','))
		{
			token = Trim(token);
			if (!token.empty())
				_serviceTags.push_back(token);
		}
	}
}
