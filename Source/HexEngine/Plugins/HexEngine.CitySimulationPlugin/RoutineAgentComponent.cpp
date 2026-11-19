#include "RoutineAgentComponent.hpp"
#include <HexEngine.Core/Entity/Component/NavigationComponent.hpp>
#include <HexEngine.Core/Entity/Component/Transform.hpp>
#include <HexEngine.Core/Entity/Entity.hpp>
#include <HexEngine.Core/GUI/Elements/Checkbox.hpp>
#include <HexEngine.Core/GUI/Elements/ComponentWidget.hpp>
#include <HexEngine.Core/GUI/Elements/EntitySearch.hpp>
#include <HexEngine.Core/GUI/Elements/LineEdit.hpp>
#include <HexEngine.Core/GUI/Elements/AssetSearch.hpp>
#include <HexEngine.Core/GUI/Elements/DragFloat.hpp>
#include <HexEngine.Core/GUI/Elements/DragInt.hpp>
#include <algorithm>
#include <sstream>

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

RoutineAgentComponent::RoutineAgentComponent(HexEngine::Entity* entity) :
	UpdateComponent(entity)
{
	RebuildRoleTags();
	RebuildSchedule();
}

RoutineAgentComponent::RoutineAgentComponent(HexEngine::Entity* entity, RoutineAgentComponent* copy) :
	UpdateComponent(entity, copy)
{
	if (copy != nullptr)
	{
		_roleTagsCsv = copy->_roleTagsCsv;
		_homeEntityName = copy->_homeEntityName;
		_assignedWorkplaceEntityName = copy->_assignedWorkplaceEntityName;
		_preferredVehiclePrefabPath = copy->_preferredVehiclePrefabPath;
		_isEmergencyEligible = copy->_isEmergencyEligible;
		_scheduleCsv = copy->_scheduleCsv;
		_travelPreference = copy->_travelPreference;
		_walkSpeed = copy->_walkSpeed;
		_scheduleJitterHours = copy->_scheduleJitterHours;
	}

	RebuildRoleTags();
	RebuildSchedule();
}

void RoutineAgentComponent::Serialize(json& data, HexEngine::JsonFile* file)
{
	file->Serialize(data, "_roleTagsCsv", _roleTagsCsv);
	file->Serialize(data, "_homeEntityName", _homeEntityName);
	file->Serialize(data, "_assignedWorkplaceEntityName", _assignedWorkplaceEntityName);
	file->Serialize(data, "_preferredVehiclePrefabPath", _preferredVehiclePrefabPath);
	file->Serialize(data, "_isEmergencyEligible", _isEmergencyEligible);
	file->Serialize(data, "_scheduleCsv", _scheduleCsv);
	file->Serialize(data, "_travelPreference", _travelPreference);
	file->Serialize(data, "_walkSpeed", _walkSpeed);
	file->Serialize(data, "_scheduleJitterHours", _scheduleJitterHours);
}

void RoutineAgentComponent::Deserialize(json& data, HexEngine::JsonFile* file, uint32_t mask)
{
	(void)mask;
	file->Deserialize(data, "_roleTagsCsv", _roleTagsCsv);
	file->Deserialize(data, "_homeEntityName", _homeEntityName);
	file->Deserialize(data, "_assignedWorkplaceEntityName", _assignedWorkplaceEntityName);
	file->Deserialize(data, "_preferredVehiclePrefabPath", _preferredVehiclePrefabPath);
	file->Deserialize(data, "_isEmergencyEligible", _isEmergencyEligible);
	file->Deserialize(data, "_scheduleCsv", _scheduleCsv);
	file->Deserialize(data, "_travelPreference", _travelPreference);
	file->Deserialize(data, "_walkSpeed", _walkSpeed);
	file->Deserialize(data, "_scheduleJitterHours", _scheduleJitterHours);
	RebuildRoleTags();
	RebuildSchedule();
	_plannedBlockKey = ~0ull;
	_plannedTargetName.clear();
	_seedCooldown = 0.0f;

	_isBusy = false;
	_isOnShift = false;
	_currentVehicleEntityName.clear();
	_taskQueue.clear();
	_activeTask.reset();
	_suspendedTask.reset();
	_executionState = RoutineExecutionState::Idle;
	_stateTimer = 0.0f;
	_retryTimer = 0.0f;
	_navigationReachedFlag = false;
}

bool RoutineAgentComponent::CreateWidget(HexEngine::ComponentWidget* widget)
{
	auto* roleTags = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Role Tags (CSV)");
	roleTags->SetValue(std::wstring(_roleTagsCsv.begin(), _roleTagsCsv.end()));
	roleTags->SetDoesCallbackWaitForReturn(false);
	roleTags->SetOnInputFn([this](HexEngine::LineEdit* edit, const std::wstring& value)
		{
			_roleTagsCsv = ws2s(value);
			RebuildRoleTags();
		});

	auto* home = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Home Entity");
	home->SetValue(std::wstring(_homeEntityName.begin(), _homeEntityName.end()));
	home->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_homeEntityName = ws2s(value);
		});
	home->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_homeEntityName = result.entityName;
		});

	auto* workplace = new HexEngine::EntitySearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Assigned Workplace");
	workplace->SetValue(std::wstring(_assignedWorkplaceEntityName.begin(), _assignedWorkplaceEntityName.end()));
	workplace->SetOnInputFn([this](HexEngine::EntitySearch* search, const std::wstring& value)
		{
			_assignedWorkplaceEntityName = ws2s(value);
		});
	workplace->SetOnSelectFn([this](HexEngine::EntitySearch* search, const HexEngine::EntitySearchResult& result)
		{
			_assignedWorkplaceEntityName = result.entityName;
		});

	auto* vehiclePrefab = new HexEngine::AssetSearch(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Preferred Vehicle Prefab", { HexEngine::ResourceType::Prefab },
		[this](HexEngine::AssetSearch* edit, const HexEngine::AssetSearchResult& result)
		{
			_preferredVehiclePrefabPath = result.assetPath.string();
		});
	vehiclePrefab->SetValue(std::wstring(_preferredVehiclePrefabPath.begin(), _preferredVehiclePrefabPath.end()));


	auto* emergencyEligible = new HexEngine::Checkbox(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Emergency Eligible", &_isEmergencyEligible);

	auto* schedule = new HexEngine::LineEdit(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Schedule (start-end:Activity, ...)");
	schedule->SetValue(std::wstring(_scheduleCsv.begin(), _scheduleCsv.end()));
	schedule->SetDoesCallbackWaitForReturn(false);
	schedule->SetOnInputFn([this](HexEngine::LineEdit* edit, const std::wstring& value)
		{
			_scheduleCsv = ws2s(value);
			RebuildSchedule();
		});
	auto* travel = new HexEngine::DragInt(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Travel (0 auto, 1 walk, 2 drive)", &_travelPreference, 0, 2, 1);
	auto* walkSpeed = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Walk Speed", &_walkSpeed, 0.2f, 10.0f, 0.05f, 2);
	auto* jitter = new HexEngine::DragFloat(widget, widget->GetNextPos(), HexEngine::Point(widget->GetSize().x - 20, 18), L"Schedule Jitter (h)", &_scheduleJitterHours, 0.0f, 4.0f, 0.05f, 2);

	roleTags->SetPrefabOverrideBinding(GetComponentName(), "/_roleTagsCsv");
	home->SetPrefabOverrideBinding(GetComponentName(), "/_homeEntityName");
	workplace->SetPrefabOverrideBinding(GetComponentName(), "/_assignedWorkplaceEntityName");
	vehiclePrefab->SetPrefabOverrideBinding(GetComponentName(), "/_preferredVehiclePrefabPath");
	emergencyEligible->SetPrefabOverrideBinding(GetComponentName(), "/_isEmergencyEligible");
	schedule->SetPrefabOverrideBinding(GetComponentName(), "/_scheduleCsv");
	travel->SetPrefabOverrideBinding(GetComponentName(), "/_travelPreference");
	walkSpeed->SetPrefabOverrideBinding(GetComponentName(), "/_walkSpeed");
	jitter->SetPrefabOverrideBinding(GetComponentName(), "/_scheduleJitterHours");

	return true;
}

void RoutineAgentComponent::OnMessage(HexEngine::Message* message, MessageListener* sender)
{
	UpdateComponent::OnMessage(message, sender);

	if (message == nullptr)
		return;

	if (message->_id == HexEngine::MessageId::NavigationTargetReached)
	{
		_navigationReachedFlag = true;
	}
}

void RoutineAgentComponent::EnqueueTask(const RoutineTaskSpec& task)
{
	// Keep queue priority-ordered so higher-priority tasks can preempt deterministicly.
	const auto insertIt = std::find_if(_taskQueue.begin(), _taskQueue.end(),
		[&task](const RoutineTaskSpec& existing)
		{
			if (task.priority != existing.priority)
				return task.priority > existing.priority;
			if (task.preemptive != existing.preemptive)
				return task.preemptive && !existing.preemptive;
			return false;
		});
	_taskQueue.insert(insertIt, task);
}

bool RoutineAgentComponent::PeekTask(RoutineTaskSpec& outTask) const
{
	if (_taskQueue.empty())
		return false;

	outTask = _taskQueue.front();
	return true;
}

bool RoutineAgentComponent::TryPopTask(RoutineTaskSpec& outTask)
{
	if (_taskQueue.empty())
		return false;

	outTask = _taskQueue.front();
	_taskQueue.pop_front();
	return true;
}

void RoutineAgentComponent::ClearQueuedTasks()
{
	_taskQueue.clear();
}

void RoutineAgentComponent::SuspendCurrentTask()
{
	if (_activeTask.has_value())
	{
		_suspendedTask = _activeTask;
		_activeTask.reset();
		_executionState = RoutineExecutionState::Idle;
	}
}

bool RoutineAgentComponent::RestoreSuspendedTask()
{
	if (!_suspendedTask.has_value())
		return false;

	_activeTask = _suspendedTask;
	_suspendedTask.reset();
	_isBusy = true;
	_executionState = RoutineExecutionState::Idle;
	_stateTimer = 0.0f;
	_retryTimer = 0.0f;
	return true;
}

bool RoutineAgentComponent::ConsumeNavigationReached()
{
	const bool reached = _navigationReachedFlag;
	_navigationReachedFlag = false;
	return reached;
}

void RoutineAgentComponent::RebuildRoleTags()
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

void RoutineAgentComponent::RebuildSchedule()
{
	_schedule.clear();
	std::string normalised = _scheduleCsv;
	std::replace(normalised.begin(), normalised.end(), ';', ',');
	std::stringstream stream(normalised);
	std::string token;
	while (std::getline(stream, token, ','))
	{
		token = Trim(token);
		const auto colon = token.find(':');
		const auto dash = token.find('-');
		if (token.empty() || colon == std::string::npos || dash == std::string::npos || dash > colon)
			continue;

		try
		{
			RoutineScheduleBlock block;
			block.start = std::stof(Trim(token.substr(0, dash)));
			block.end = std::stof(Trim(token.substr(dash + 1, colon - dash - 1)));
			block.activity = Trim(token.substr(colon + 1));
			if (block.activity.empty())
				continue;
			block.start = std::clamp(block.start, 0.0f, 24.0f);
			block.end = std::clamp(block.end, 0.0f, 24.0f);
			_schedule.push_back(block);
		}
		catch (...)
		{
		}
	}

	_plannedBlockKey = ~0ull;
	_plannedTargetName.clear();
}
