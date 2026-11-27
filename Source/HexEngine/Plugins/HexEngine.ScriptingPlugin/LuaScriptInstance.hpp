#pragma once

#include "LuaCommon.hpp"

namespace HexScripting
{
	class LuaScriptEngine;
	struct ScriptAsset;

	/** @brief A ScriptComponent's live script: a `self` table whose metatable
	 *  indexes the shared class table of its ScriptAsset.
	 *
	 * `self` exists only while started (play mode). It holds the effective
	 * properties, `self.entity`, and whatever state the script adds. A hot
	 * reload swaps the class under it (methods change, state survives).
	 */
	class LuaScriptInstance final : public HexEngine::IScriptInstance
	{
	public:
		LuaScriptInstance(LuaScriptEngine& engine, HexEngine::ScriptComponent* owner);
		virtual ~LuaScriptInstance();

		// IScriptInstance
		virtual bool IsLoaded() const override;
		virtual bool IsStarted() const override { return _started; }
		virtual void Start() override;
		virtual void Stop() override;
		virtual void Update(float dt) override;
		virtual void FixedUpdate(float dt) override;
		virtual void LateUpdate(float dt) override;
		virtual void DispatchEvent(const HexEngine::ScriptEvent& event) override;
		virtual void OnPropertiesChanged() override;

		// Engine side
		void OnAssetReloaded();
		void Detach();	// engine shutdown: drop Lua refs, release the owner's pointer

		HexEngine::ScriptComponent* GetOwner() const { return _owner; }
		const std::shared_ptr<ScriptAsset>& GetAsset() const { return _asset; }
		const sol::table& GetSelf() const { return _self; }
		HexEngine::Scene* GetScene() const;
		std::string Describe() const;

	private:
		// Runs self:<name>(args...) when the script defines it. Returns false
		// (and faults the instance) on error.
		template <typename... Args>
		bool Invoke(const char* name, Args&&... args);

		bool CanRun() const;
		void CreateSelf();
		void ApplyProperties();
		void PublishDefaults();

	private:
		LuaScriptEngine& _engine;
		HexEngine::ScriptComponent* _owner = nullptr;
		std::shared_ptr<ScriptAsset> _asset;
		uint32_t _assetGeneration = 0;

		sol::table _self;
		bool _started = false;		// play session active
		bool _faulted = false;		// a callback errored; silent until the next reload
	};
}
