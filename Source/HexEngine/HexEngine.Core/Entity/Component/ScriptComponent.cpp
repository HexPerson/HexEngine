
#include "ScriptComponent.hpp"

#include "../Entity.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/LineEdit.hpp"

namespace HexEngine
{
	namespace
	{
		// Text shown in the inspector for a property value.
		std::wstring PropertyToText(const json& v)
		{
			std::string s;
			if (v.is_string())
				s = v.get<std::string>();
			else if (v.is_array())
			{
				for (size_t i = 0; i < v.size(); ++i)
				{
					if (i) s += ", ";
					s += v[i].is_number() ? std::to_string(v[i].get<double>()) : v[i].dump();
				}
			}
			else
				s = v.dump();
			return s2ws(s);
		}

		// Parse inspector text back into a value shaped like `like` (the
		// script default). Returns false when the text doesn't fit the shape.
		bool TextToProperty(const std::wstring& wtext, const json& like, json& out)
		{
			const std::string text = ws2s(wtext);
			try
			{
				if (like.is_boolean())
				{
					if (text == "true" || text == "1") { out = true; return true; }
					if (text == "false" || text == "0") { out = false; return true; }
					return false;
				}
				if (like.is_number())
				{
					size_t used = 0;
					const double d = std::stod(text, &used);
					out = d;
					return used > 0;
				}
				if (like.is_array())
				{
					json arr = json::array();
					std::string cur;
					for (size_t i = 0; i <= text.size(); ++i)
					{
						if (i == text.size() || text[i] == ',')
						{
							arr.push_back(std::stod(cur));
							cur.clear();
						}
						else
							cur += text[i];
					}
					if (arr.size() != like.size())
						return false;
					out = arr;
					return true;
				}
				out = text;
				return true;
			}
			catch (...)
			{
				return false;
			}
		}
	}

	ScriptComponent::ScriptComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	ScriptComponent::ScriptComponent(Entity* entity, ScriptComponent* copy) :
		UpdateComponent(entity)
	{
		if (copy != nullptr)
		{
			_scriptPath = copy->_scriptPath;
			_propertyOverrides = copy->_propertyOverrides;
			_propertyDefaults = copy->_propertyDefaults;
			_defaultsValid = copy->_defaultsValid;
		}
	}

	ScriptComponent::~ScriptComponent()
	{
		ReleaseInstance(false);
	}

	void ScriptComponent::Destroy()
	{
		// Destroyed during play (Destroy() from a script, a level unload): the
		// script gets its OnStop. Edit-mode teardown just drops the instance.
		ReleaseInstance(g_pEnv != nullptr && g_pEnv->IsGameRunning());
	}

	void ScriptComponent::ReleaseInstance(bool callStop)
	{
		if (_instance == nullptr)
			return;

		IScriptInstance* instance = _instance;
		_instance = nullptr;

		IScriptEngine* engine = g_pEnv != nullptr ? g_pEnv->_scriptEngine : nullptr;
		if (engine == nullptr)
			return; // engine already gone; it released its instances itself

		if (callStop && instance->IsStarted())
			instance->Stop();

		engine->DestroyInstance(instance);
	}

	void ScriptComponent::SetScript(const std::string& scriptPath)
	{
		std::string canonical = scriptPath;

		if (IScriptEngine* engine = g_pEnv != nullptr ? g_pEnv->_scriptEngine : nullptr; engine != nullptr && !scriptPath.empty())
		{
			fs::path absolute;
			std::string resolved;
			if (engine->ResolveScriptPath(scriptPath, absolute, &resolved) && !resolved.empty())
				canonical = resolved;
		}

		if (canonical == _scriptPath)
			return;

		_scriptPath = canonical;
		_defaultsValid = false;
		_recreateInstance = true;
	}

	void ScriptComponent::SetScriptPropertyDefaults(const json& defaults)
	{
		_propertyDefaults = defaults.is_object() ? defaults : json::object();
		_defaultsValid = true;
		PruneOverrides();
	}

	void ScriptComponent::PruneOverrides()
	{
		if (!_defaultsValid)
			return;

		// An override equal to the script's default is not an override: drop it,
		// so changing the default in the script reaches this instance. Overrides
		// for properties the script no longer declares are kept (a rename or a
		// script with a syntax error must not silently wipe authored values).
		for (auto it = _propertyOverrides.begin(); it != _propertyOverrides.end();)
		{
			auto def = _propertyDefaults.find(it.key());
			if (def != _propertyDefaults.end() && *def == it.value())
				it = _propertyOverrides.erase(it);
			else
				++it;
		}
	}

	json ScriptComponent::GetEffectiveProperties() const
	{
		json merged = _propertyDefaults.is_object() ? _propertyDefaults : json::object();
		for (auto& [key, value] : _propertyOverrides.items())
			merged[key] = value;
		return merged;
	}

	void ScriptComponent::SetProperty(const std::string& name, const json& value)
	{
		_propertyOverrides[name] = value;
		PruneOverrides();
		_propertiesDirty = true;
	}

	bool ScriptComponent::ShouldRun()
	{
		if (g_pEnv == nullptr || !g_pEnv->IsGameRunning())
			return false;

		Entity* entity = GetEntity();
		if (entity == nullptr || entity->IsPendingDeletion())
			return false;

		// Editor-internal scenes (prefab stage, icon previews) never run gameplay.
		Scene* scene = entity->GetScene();
		if (scene == nullptr || HEX_HASFLAG(scene->GetFlags(), SceneFlags::Utility))
			return false;

		return true;
	}

	IScriptInstance* ScriptComponent::SyncInstance()
	{
		IScriptEngine* engine = g_pEnv != nullptr ? g_pEnv->_scriptEngine : nullptr;
		if (engine == nullptr)
			return nullptr;

		if (_scriptPath.empty())
		{
			ReleaseInstance(true);
			return nullptr;
		}

		if (_recreateInstance.exchange(false) || _instance == nullptr)
		{
			ReleaseInstance(true);
			_instance = engine->CreateInstance(this);
			_propertiesDirty = false; // a fresh instance reads the current overrides
		}

		if (_instance == nullptr)
			return nullptr;

		if (_propertiesDirty.exchange(false))
			_instance->OnPropertiesChanged();

		const bool shouldRun = ShouldRun();
		if (shouldRun && !_instance->IsStarted())
			_instance->Start();
		else if (!shouldRun && _instance->IsStarted())
			_instance->Stop();

		if (!_instance || !_instance->IsStarted())
		{
			std::scoped_lock lock(_eventLock);
			_pendingEvents.clear();
			return nullptr;
		}

		return _instance;
	}

	void ScriptComponent::DispatchPendingEvents()
	{
		std::vector<ScriptEvent> events;
		{
			std::scoped_lock lock(_eventLock);
			events.swap(_pendingEvents);
		}

		for (const ScriptEvent& ev : events)
		{
			if (_instance == nullptr || !_instance->IsStarted())
				break;
			_instance->DispatchEvent(ev);
		}
	}

	void ScriptComponent::Update(float frameTime)
	{
		UpdateComponent::Update(frameTime);

		IScriptInstance* instance = SyncInstance();
		if (instance == nullptr)
			return;

		DispatchPendingEvents();

		// Paused simulation: Scene::Update still ticks with a zero game frame
		// time; gameplay scripts sit it out (a requested step gives dt > 0).
		if (frameTime <= 0.0f && g_pEnv->_timeManager->_simulationPaused)
			return;

		if (_instance != nullptr)
			_instance->Update(frameTime);
	}

	void ScriptComponent::FixedUpdate(float frameTime)
	{
		if (_instance == nullptr || !_instance->IsStarted() || !ShouldRun())
			return;

		DispatchPendingEvents();

		if (_instance != nullptr)
			_instance->FixedUpdate(frameTime);
	}

	void ScriptComponent::LateUpdate(float frameTime)
	{
		if (_instance == nullptr || !_instance->IsStarted() || !ShouldRun())
			return;

		if (frameTime <= 0.0f && g_pEnv->_timeManager->_simulationPaused)
			return;

		_instance->LateUpdate(frameTime);
	}

	void ScriptComponent::QueueEvent(const ScriptEvent& event)
	{
		// Only worth queuing while a play session is live; cap it so a body
		// resting in a trigger can't grow the queue without bound.
		if (g_pEnv == nullptr || !g_pEnv->IsGameRunning() || _scriptPath.empty())
			return;

		std::scoped_lock lock(_eventLock);
		if (_pendingEvents.size() < 256)
			_pendingEvents.push_back(event);
	}

	void ScriptComponent::OnMessage(Message* message, MessageListener* sender)
	{
		UpdateComponent::OnMessage(message, sender);

		switch (message->_id)
		{
		case MessageId::EnterTrigger:
		case MessageId::LeaveTrigger:
		{
			// Delivered to both sides of the overlap: `other` is the entity on
			// the far side (older senders only filled `trigger`).
			EntityId other = InvalidEntityId;
			if (auto* enter = message->CastAs<EnterTriggerMessage>())
				other = enter->otherId.IsValid() ? enter->otherId : enter->triggerId;
			else if (auto* leave = message->CastAs<LeaveTriggerMessage>())
				other = leave->otherId.IsValid() ? leave->otherId : leave->triggerId;

			ScriptEvent ev;
			ev.type = message->_id == MessageId::EnterTrigger ? ScriptEventType::TriggerEnter : ScriptEventType::TriggerExit;
			ev.other = other;
			QueueEvent(ev);
			break;
		}
		case MessageId::RigidBodyCollision:
		{
			if (auto* hit = message->CastAs<RigidBodyCollision>())
			{
				ScriptEvent ev;
				ev.type = ScriptEventType::Collision;
				ev.other = hit->collidedWithId;
				ev.point = hit->collisionPoint;
				QueueEvent(ev);
			}
			break;
		}
		default:
			break;
		}
	}

	void ScriptComponent::Serialize(json& data, JsonFile* file)
	{
		data["script"] = _scriptPath;
		data["properties"] = GetEffectiveProperties();
	}

	void ScriptComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		std::string scriptPath;
		if (auto it = data.find("script"); it != data.end() && it->is_string())
			scriptPath = it->get<std::string>();
		else if (auto legacy = data.find("_scriptPath"); legacy != data.end() && legacy->is_string())
			scriptPath = legacy->get<std::string>(); // pre-scripting-plugin scenes

		// No ResolveScriptPath here: this can run on the scene-loader thread.
		// The engine resolves the path when it creates the instance.
		if (scriptPath != _scriptPath)
		{
			_scriptPath = scriptPath;
			_defaultsValid = false;
			_recreateInstance = true;
		}

		if (auto props = data.find("properties"); props != data.end() && props->is_object())
			_propertyOverrides = *props;
		else
			_propertyOverrides = json::object();

		PruneOverrides();
		_propertiesDirty = true;
	}

	bool ScriptComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		LineEdit* scriptName = new LineEdit(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Script");
		scriptName->SetPrefabOverrideBinding(GetComponentName(), "/script");
		scriptName->SetValue(s2ws(_scriptPath));
		scriptName->SetOnDragAndDropFn(std::bind(&ScriptComponent::SetScriptFromWidget, this, std::placeholders::_1, std::placeholders::_2));
		scriptName->SetOnInputFn([this](LineEdit*, const std::wstring& text)
		{
			SetScript(ws2s(text));
		});

		// One text field per exposed property. Values parse to the shape of
		// the script default (number / bool / string / "x, y, z"). Properties
		// appear once the script has compiled; reselect the entity after
		// attaching a new script to see them.
		const json effective = GetEffectiveProperties();
		for (auto& [key, value] : effective.items())
		{
			const std::string name = key;
			const std::wstring label = s2ws(name);
			LineEdit* edit = new LineEdit(widget, widget->GetNextPos(), Point(fullWidth, 18), label);
			edit->SetPrefabOverrideBinding(GetComponentName(), "/properties/" + name);
			edit->SetValue(PropertyToText(value));
			edit->SetOnInputFn([this, name](LineEdit* e, const std::wstring& text)
			{
				const json effectiveNow = GetEffectiveProperties();
				auto like = effectiveNow.find(name);
				json parsed;
				if (like != effectiveNow.end() && TextToProperty(text, *like, parsed))
					SetProperty(name, parsed);
				else if (like != effectiveNow.end())
					e->SetValue(PropertyToText(*like)); // reject: show the current value again
			});
		}

		return true;
	}

	void ScriptComponent::SetScriptFromWidget(LineEdit* edit, const fs::path& path)
	{
		SetScript(ws2s(path.wstring()));
		edit->SetValue(s2ws(_scriptPath));
	}
}
