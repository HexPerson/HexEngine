#include "PlaceOfWorkComponent.hpp"
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>

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

PlaceOfWorkComponent::PlaceOfWorkComponent(HexEngine::Entity* entity) :
	BaseComponent(entity)
{
	RebuildRoleTags();
	RebuildShiftsFromCsv();
}

PlaceOfWorkComponent::PlaceOfWorkComponent(HexEngine::Entity* entity, PlaceOfWorkComponent* copy) :
	BaseComponent(entity)
{
	if (copy != nullptr)
	{
		_roleTagsCsv = copy->_roleTagsCsv;
		_workerCapacity = copy->_workerCapacity;
		_servicePriority = copy->_servicePriority;
		_shiftWindowsCsv = copy->_shiftWindowsCsv;
		_entryWaypointEntityName = copy->_entryWaypointEntityName;
		_parkingWaypointEntityName = copy->_parkingWaypointEntityName;
	}

	RebuildRoleTags();
	RebuildShiftsFromCsv();
}

void PlaceOfWorkComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_roleTagsCsv", _roleTagsCsv);
	file->Serialize(data, "_workerCapacity", _workerCapacity);
	file->Serialize(data, "_servicePriority", _servicePriority);
	file->Serialize(data, "_shiftWindowsCsv", _shiftWindowsCsv);
	file->Serialize(data, "_entryWaypointEntityName", _entryWaypointEntityName);
	file->Serialize(data, "_parkingWaypointEntityName", _parkingWaypointEntityName);
}

void PlaceOfWorkComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_roleTagsCsv", _roleTagsCsv);
	file->Deserialize(data, "_workerCapacity", _workerCapacity);
	file->Deserialize(data, "_servicePriority", _servicePriority);
	file->Deserialize(data, "_shiftWindowsCsv", _shiftWindowsCsv);
	file->Deserialize(data, "_entryWaypointEntityName", _entryWaypointEntityName);
	file->Deserialize(data, "_parkingWaypointEntityName", _parkingWaypointEntityName);

	RebuildRoleTags();
	RebuildShiftsFromCsv();
}

bool PlaceOfWorkComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* roles = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Role Tags (CSV)");
	roles->SetValue(std::wstring(_roleTagsCsv.begin(), _roleTagsCsv.end()));
	roles->SetDoesCallbackWaitForReturn(false);
	roles->SetOnInputFn([this](HexEngine::LineEdit* edit, const std::wstring& value)
		{
			_roleTagsCsv = ws2s(value);
			RebuildRoleTags();
		});

	auto* capacity = new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Worker Capacity", &_workerCapacity, 1, 100000, 1);
	auto* priority = new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Service Priority", &_servicePriority, -100, 100, 1);

	auto* shifts = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Shift Windows (CSV)");
	shifts->SetValue(std::wstring(_shiftWindowsCsv.begin(), _shiftWindowsCsv.end()));
	shifts->SetDoesCallbackWaitForReturn(false);
	shifts->SetOnInputFn([this](HexEngine::LineEdit* edit, const std::wstring& value)
		{
			_shiftWindowsCsv = ws2s(value);
			RebuildShiftsFromCsv();
		});

	auto* entry = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Entry Waypoint");
	entry->SetValue(std::wstring(_entryWaypointEntityName.begin(), _entryWaypointEntityName.end()));
	entry->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_entryWaypointEntityName = ws2s(value);
		});
	entry->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_entryWaypointEntityName = result.entityName;
		});

	auto* parking = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Parking Waypoint");
	parking->SetValue(std::wstring(_parkingWaypointEntityName.begin(), _parkingWaypointEntityName.end()));
	parking->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_parkingWaypointEntityName = ws2s(value);
		});
	parking->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_parkingWaypointEntityName = result.entityName;
		});

	roles->SetPrefabOverrideBinding(GetComponentName(), "/_roleTagsCsv");
	capacity->SetPrefabOverrideBinding(GetComponentName(), "/_workerCapacity");
	priority->SetPrefabOverrideBinding(GetComponentName(), "/_servicePriority");
	shifts->SetPrefabOverrideBinding(GetComponentName(), "/_shiftWindowsCsv");
	entry->SetPrefabOverrideBinding(GetComponentName(), "/_entryWaypointEntityName");
	parking->SetPrefabOverrideBinding(GetComponentName(), "/_parkingWaypointEntityName");

	return true;
}

bool PlaceOfWorkComponent::IsHourInAnyShift(float hour) const
{
	for (const auto& shift : _shiftWindows)
	{
		const float start = shift.x;
		const float end = shift.y;

		if (start <= end)
		{
			if (hour >= start && hour < end)
				return true;
		}
		else
		{
			if (hour >= start || hour < end)
				return true;
		}
	}
	return false;
}

void PlaceOfWorkComponent::RebuildRoleTags()
{
	_roleTags.clear();
	std::stringstream stream(_roleTagsCsv);
	std::string token;
	while (std::getline(stream, token, ','))
	{
		token = Trim(token);
		if (!token.empty())
			_roleTags.push_back(token);
	}
}

void PlaceOfWorkComponent::RebuildShiftsFromCsv()
{
	_shiftWindows.clear();
	std::stringstream stream(_shiftWindowsCsv);
	std::string token;
	while (std::getline(stream, token, ','))
	{
		token = Trim(token);
		if (token.empty())
			continue;

		auto dash = token.find('-');
		if (dash == std::string::npos)
			continue;

		const std::string left = Trim(token.substr(0, dash));
		const std::string right = Trim(token.substr(dash + 1));
		if (left.empty() || right.empty())
			continue;

		try
		{
			const float start = std::stof(left);
			const float end = std::stof(right);
			_shiftWindows.emplace_back(start, end);
		}
		catch (...)
		{
		}
	}

	if (_shiftWindows.empty())
		_shiftWindows.emplace_back(9.0f, 17.0f);
}