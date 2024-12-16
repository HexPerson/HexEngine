
#pragma once

#include <HexEngine.Core/HexEngine.hpp>
#include <angelscript.h>

class AngelScriptFile : public ScriptFile
{
public:
	bool Create(asIScriptModule* mod, asIScriptEngine* engine);

	virtual void Destroy() override;

	virtual void Update(float dt) override;
	virtual void FixedUpdate(float dt) override;
	virtual void LateUpdate(float dt) override;

private:
	asIScriptEngine* _engine = nullptr;
	asIScriptContext* _ctx = nullptr;
	asIScriptModule* _mod = nullptr;
	asITypeInfo* _type = nullptr;
	asIScriptFunction* _onUpdateMethod = nullptr;
	asIScriptFunction* _onFixedUpdateMethod = nullptr;
	asIScriptFunction* _onLateUpdateMethod = nullptr;
	asIScriptFunction* _onGuiMethod = nullptr;
	asIScriptObject* _object = nullptr;
};