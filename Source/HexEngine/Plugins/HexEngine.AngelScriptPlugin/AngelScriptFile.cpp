
#include "AngelScriptFile.hpp"
#include "Plugin.hpp"
#include "ScriptEntity.hpp"

bool AngelScriptFile::Create(asIScriptModule* mod, asIScriptEngine* engine)
{
	_engine = engine;
	_mod = mod;
	_ctx = engine->CreateContext();

	asITypeInfo* type = 0;
	int tc = mod->GetObjectTypeCount();
	for (int n = 0; n < tc; n++)
	{
		bool found = false;
		type = mod->GetObjectTypeByIndex(n);
		int ic = type->GetInterfaceCount();
		for (int i = 0; i < ic; i++)
		{
			if (strcmp(type->GetInterface(i)->GetName(), INTERFACE_NAME) == 0)
			{
				found = true;
				break;
			}
		}

		if (found == true)
		{
			_type = type;
			break;
		}
	}

	if (_type == nullptr)
	{
		LOG_CRIT("Could not find a class that inheritys from %s", INTERFACE_NAME);
		return false;
	}

	// Find the factory function
	// The game engine will pass in the owning CGameObj to the controller for storage
	std::string s = std::string(type->GetName()) + "@ " + std::string(type->GetName()) + "(ScriptEntity @, ScriptEntity @)";
	auto factoryFunc = type->GetFactoryByDecl(s.c_str());
	if (factoryFunc == 0)
	{
		LOG_CRIT("Script constructor was not found");
		return false;
	}

	asIScriptContext* ctx = _engine->CreateContext();

	assert(ctx->Prepare(factoryFunc) >= 0);
	ctx->SetArgObject(0, new ScriptEntity(_component->GetEntity()));
	ctx->SetArgObject(1, new ScriptEntity(g_pEnv->_sceneManager->GetCurrentScene()->GetMainCamera()->GetEntity()));

	int r = ctx->Execute(); assert(r >= 0);

	if (r == asEXECUTION_FINISHED)
	{
		// Get the newly created object
		_object = *((asIScriptObject**)ctx->GetAddressOfReturnValue());

		// Since a reference will be kept to this object 
		// it is necessary to increase the ref count
		_object->AddRef();
	}
	assert(ctx->Release() >= 0);

	_onUpdateMethod = _type->GetMethodByDecl("void OnUpdate(float dt)");
	_onFixedUpdateMethod = _type->GetMethodByDecl("void OnFixedUpdate(float dt)");
	_onLateUpdateMethod = _type->GetMethodByDecl("void OnLateUpdate(float dt)");
	//_onGuiMethod = _type->GetMethodByDecl("void OnGui(GuiRenderer* renderer)");	

	return true;
}

void AngelScriptFile::Destroy()
{
	_ctx->Release();
}

void AngelScriptFile::Update(float dt)
{
	if (_onUpdateMethod)
	{
		asIScriptContext* ctx = _engine->CreateContext();

		assert(ctx->Prepare(_onUpdateMethod) >= 0);
		assert(ctx->SetObject(_object) >= 0);
		assert(ctx->SetArgFloat(0, dt) >= 0);
		assert(ctx->Execute() >= 0);
		assert(ctx->Release() >= 0);
	}
}

void AngelScriptFile::FixedUpdate(float dt)
{

}

void AngelScriptFile::LateUpdate(float dt)
{

}