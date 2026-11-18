
#include "LuaScriptInstance.hpp"
#include "LuaScriptEngine.hpp"

namespace HexScripting
{
	using namespace HexEngine;

	namespace
	{
		// Names the instance owns on `self`; a property with one of these names
		// is ignored (the script sees the engine's value).
		bool IsReservedSelfKey(const std::string& key)
		{
			return key == "entity" || key == "script";
		}

		sol::table MakeClassMetatable(sol::state& lua, const sol::table& klass)
		{
			sol::table mt = lua.create_table();
			mt[sol::meta_function::index] = klass;
			return mt;
		}
	}

	LuaScriptInstance::LuaScriptInstance(LuaScriptEngine& engine, ScriptComponent* owner) :
		_engine(engine),
		_owner(owner)
	{
		_asset = _engine.AcquireScript(owner->GetScript());
		_assetGeneration = _asset->generation;
		PublishDefaults();
	}

	LuaScriptInstance::~LuaScriptInstance()
	{
		_self = sol::table();
	}

	bool LuaScriptInstance::IsLoaded() const
	{
		return _asset != nullptr && _asset->loaded;
	}

	Scene* LuaScriptInstance::GetScene() const
	{
		Entity* entity = _owner != nullptr ? _owner->GetEntity() : nullptr;
		return entity != nullptr ? entity->GetScene() : nullptr;
	}

	std::string LuaScriptInstance::Describe() const
	{
		std::string out = _asset ? _asset->path : std::string("<no script>");
		if (Entity* entity = _owner != nullptr ? _owner->GetEntity() : nullptr)
			out += " on '" + entity->GetName() + "'";
		return out;
	}

	template <typename... Args>
	bool LuaScriptInstance::Invoke(const char* name, Args&&... args)
	{
		if (!_self.valid())
			return true;

		sol::object fn = _self[name];
		if (!fn.is<sol::function>())
			return true;

		std::string error;
		if (_engine.Call(this, fn, error, _self, std::forward<Args>(args)...))
			return true;

		_faulted = true;
		LOG_WARN("[Script] %s: %s failed: %s", Describe().c_str(), name, error.c_str());
		LOG_WARN("[Script] %s: callbacks suspended until the script is saved again (it hot-reloads).", Describe().c_str());
		return false;
	}

	bool LuaScriptInstance::CanRun() const
	{
		return _started && !_faulted && _self.valid();
	}

	void LuaScriptInstance::PublishDefaults()
	{
		if (_owner != nullptr && _asset != nullptr && _asset->loaded)
			_owner->SetScriptPropertyDefaults(_asset->defaults);
	}

	void LuaScriptInstance::ApplyProperties()
	{
		if (!_self.valid() || _owner == nullptr)
			return;

		sol::state& lua = _engine.Lua();
		const json effective = _owner->GetEffectiveProperties();
		const json& defaults = _owner->GetScriptPropertyDefaults();

		for (auto& [key, value] : effective.items())
		{
			if (IsReservedSelfKey(key))
				continue;
			auto def = defaults.find(key);
			_self[key] = JsonToLua(lua, value, def != defaults.end() ? *def : value);
		}
	}

	void LuaScriptInstance::CreateSelf()
	{
		sol::state& lua = _engine.Lua();
		_self = lua.create_table();
		_self[sol::metatable_key] = MakeClassMetatable(lua, _asset->klass);
		_self["entity"] = EntityRef::From(_owner->GetEntity());
		_self["script"] = _asset->path;
		ApplyProperties();
	}

	void LuaScriptInstance::Start()
	{
		if (_started)
			return;

		_started = true;
		_faulted = false;

		if (!IsLoaded() || _owner == nullptr || _owner->GetEntity() == nullptr)
			return; // starts later if a hot reload fixes the script

		CreateSelf();
		Invoke("OnStart");
	}

	void LuaScriptInstance::Stop()
	{
		if (!_started)
			return;

		if (CanRun())
			Invoke("OnStop");

		_self = sol::table();
		_started = false;
		_faulted = false;
	}

	void LuaScriptInstance::Update(float dt)
	{
		if (!CanRun())
			return;

		for (const auto& [vk, down] : _engine.GetFrameKeyEvents())
		{
			if (!Invoke(down ? "OnKeyDown" : "OnKeyUp", VkToKeyName(vk)))
				return;
		}

		Invoke("OnUpdate", dt);
	}

	void LuaScriptInstance::FixedUpdate(float dt)
	{
		if (CanRun())
			Invoke("OnFixedUpdate", dt);
	}

	void LuaScriptInstance::LateUpdate(float dt)
	{
		if (CanRun())
			Invoke("OnLateUpdate", dt);
	}

	void LuaScriptInstance::DispatchEvent(const ScriptEvent& event)
	{
		if (!CanRun())
			return;

		EntityRef other;
		other.scene = GetScene();
		other.id = event.other;

		switch (event.type)
		{
		case ScriptEventType::TriggerEnter:
			Invoke("OnTriggerEnter", other);
			break;
		case ScriptEventType::TriggerExit:
			Invoke("OnTriggerExit", other);
			break;
		case ScriptEventType::Collision:
			Invoke("OnCollision", other, event.point);
			break;
		}
	}

	void LuaScriptInstance::OnPropertiesChanged()
	{
		ApplyProperties();
	}

	void LuaScriptInstance::OnAssetReloaded()
	{
		if (_asset == nullptr || _asset->generation == _assetGeneration)
			return;

		_assetGeneration = _asset->generation;
		_faulted = false;
		PublishDefaults();

		if (!_started || !_asset->loaded || _owner == nullptr || _owner->GetEntity() == nullptr)
			return;

		if (_self.valid())
		{
			// Swap the class under the live object: new methods, same state.
			// Properties the new version adds get their values; existing keys
			// keep their runtime values.
			sol::state& lua = _engine.Lua();
			_self[sol::metatable_key] = MakeClassMetatable(lua, _asset->klass);

			const json effective = _owner->GetEffectiveProperties();
			const json& defaults = _owner->GetScriptPropertyDefaults();
			for (auto& [key, value] : effective.items())
			{
				if (IsReservedSelfKey(key))
					continue;
				sol::object current = _self.raw_get<sol::object>(key);
				if (current.valid() && current.get_type() != sol::type::lua_nil)
					continue;
				auto def = defaults.find(key);
				_self[key] = JsonToLua(lua, value, def != defaults.end() ? *def : value);
			}

			Invoke("OnReload");
		}
		else
		{
			// It failed to load when play started; it works now, so start it.
			CreateSelf();
			Invoke("OnStart");
		}
	}

	void LuaScriptInstance::Detach()
	{
		_self = sol::table();
		_started = false;
		if (_owner != nullptr)
			_owner->OnScriptEngineShutdown();
		_owner = nullptr;
	}
}
