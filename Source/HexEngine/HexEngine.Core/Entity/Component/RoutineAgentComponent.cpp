#include "RoutineAgentComponent.hpp"
#include "NavigationComponent.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/EntitySearch.hpp"
#include "../../GUI/Elements/LineEdit.hpp"
#include "../../GUI/Elements/AssetSearch.hpp"
#include <algorithm>
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

	RoutineAgentComponent::RoutineAgentComponent(Entity* entity) :
		UpdateComponent(entity)
	{
		RebuildRoleTags();
	}

	RoutineAgentComponent::RoutineAgentComponent(Entity* entity, RoutineAgentComponent* copy) :
		UpdateComponent(entity, copy)
	{
		if (copy != nullptr)
		{
			_roleTagsCsv = copy->_roleTagsCsv;
			_homeEntityName = copy->_homeEntityName;
			_assignedWorkplaceEntityName = copy->_assignedWorkplaceEntityName;
			_preferredVehiclePrefabPath = copy->_preferredVehiclePrefabPath;
			_isEmergencyEligible = copy->_isEmergencyEligible;
		}

		RebuildRoleTags();
	}

	void RoutineAgentComponent::Serialize(json& data, JsonFile* file)
	{
		file->Serialize(data, "_roleTagsCsv", _roleTagsCsv);
		file->Serialize(data, "_homeEntityName", _homeEntityName);
		file->Serialize(data, "_assignedWorkplaceEntityName", _assignedWorkplaceEntityName);
		file->Serialize(data, "_preferredVehiclePrefabPath", _preferredVehiclePrefabPath);
		file->Serialize(data, "_isEmergencyEligible", _isEmergencyEligible);
	}

	void RoutineAgentComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		file->Deserialize(data, "_roleTagsCsv", _roleTagsCsv);
		file->Deserialize(data, "_homeEntityName", _homeEntityName);
		file->Deserialize(data, "_assignedWorkplaceEntityName", _assignedWorkplaceEntityName);
		file->Deserialize(data, "_preferredVehiclePrefabPath", _preferredVehiclePrefabPath);
		file->Deserialize(data, "_isEmergencyEligible", _isEmergencyEligible);
		RebuildRoleTags();

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

	bool RoutineAgentComponent::CreateWidget(ComponentWidget* widget)
	{
		auto* roleTags = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Role Tags (CSV)");
		roleTags->SetValue(std::wstring(_roleTagsCsv.begin(), _roleTagsCsv.end()));
		roleTags->SetDoesCallbackWaitForReturn(false);
		roleTags->SetOnInputFn([this](LineEdit* edit, const std::wstring& value)
		{
			_roleTagsCsv = std::string(value.begin(), value.end());
			RebuildRoleTags();
		});

		auto* home = new EntitySearch(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Home Entity");
		home->SetValue(std::wstring(_homeEntityName.begin(), _homeEntityName.end()));
		home->SetOnInputFn([this](EntitySearch* search, const std::wstring& value)
		{
			_homeEntityName = std::string(value.begin(), value.end());
		});
		home->SetOnSelectFn([this](EntitySearch* search, const EntitySearchResult& result)
		{
			_homeEntityName = result.entityName;
		});

		auto* workplace = new EntitySearch(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Assigned Workplace");
		workplace->SetValue(std::wstring(_assignedWorkplaceEntityName.begin(), _assignedWorkplaceEntityName.end()));
		workplace->SetOnInputFn([this](EntitySearch* search, const std::wstring& value)
		{
			_assignedWorkplaceEntityName = std::string(value.begin(), value.end());
		});
		workplace->SetOnSelectFn([this](EntitySearch* search, const EntitySearchResult& result)
		{
			_assignedWorkplaceEntityName = result.entityName;
		});

		auto* vehiclePrefab = new AssetSearch(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Preferred Vehicle Prefab", {ResourceType::Prefab}, 
			[this](AssetSearch* edit, const AssetSearchResult& result)
			{
				_preferredVehiclePrefabPath = result.assetPath.string();
			});
		vehiclePrefab->SetValue(std::wstring(_preferredVehiclePrefabPath.begin(), _preferredVehiclePrefabPath.end()));


		auto* emergencyEligible = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Emergency Eligible", &_isEmergencyEligible);

		roleTags->SetPrefabOverrideBinding(GetComponentName(), "/_roleTagsCsv");
		home->SetPrefabOverrideBinding(GetComponentName(), "/_homeEntityName");
		workplace->SetPrefabOverrideBinding(GetComponentName(), "/_assignedWorkplaceEntityName");
		vehiclePrefab->SetPrefabOverrideBinding(GetComponentName(), "/_preferredVehiclePrefabPath");
		emergencyEligible->SetPrefabOverrideBinding(GetComponentName(), "/_isEmergencyEligible");

		return true;
	}

	void RoutineAgentComponent::OnMessage(Message* message, MessageListener* sender)
	{
		UpdateComponent::OnMessage(message, sender);

		if (message == nullptr)
			return;

		if (message->_id == MessageId::NavigationTargetReached)
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
}
