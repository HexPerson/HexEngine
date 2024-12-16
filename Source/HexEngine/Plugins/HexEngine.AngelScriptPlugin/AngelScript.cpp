

#include "AngelScript.hpp"
#include "AngelScriptFile.hpp"
#include "ScriptEntity.hpp"

void print(const std::string& in)
{
	bool a = false;
}

void MessageCallback(const asSMessageInfo* msg, void* param)
{
	const char* type = "ERR ";
	if (msg->type == asMSGTYPE_WARNING)
		type = "WARN";
	else if (msg->type == asMSGTYPE_INFORMATION)
		type = "INFO";
	LOG_DEBUG("%s (%d, %d) : %s : %s", msg->section, msg->row, msg->col, type, msg->message);
}

bool AngelScript::Create()
{
	_engine = asCreateScriptEngine();

	// Set the message callback to receive information on errors in human readable form.
	int r = _engine->SetMessageCallback(asFUNCTION(MessageCallback), this, asCALL_STDCALL); assert(r >= 0);

	// AngelScript doesn't have a built-in string type, as there is no definite standard 
	// string type for C++ applications. Every developer is free to register its own string type.
	// The SDK do however provide a standard add-on for registering a string type, so it's not
	// necessary to implement the registration yourself if you don't want to.
	RegisterStdString(_engine);

	// Register the function that we want the scripts to call 
	r = _engine->RegisterGlobalFunction("void print(const string &in)", asFUNCTION(print), asCALL_CDECL); assert(r >= 0);

	r = _engine->RegisterInterface(INTERFACE_NAME);
	//r = _engine->RegisterInterfaceMethod(INTERFACE_NAME, "void Update(float dt)");
	// 
	
	r = _engine->RegisterObjectType("ScriptEntity", 0, asOBJ_REF); assert(r >= 0);
	r = _engine->RegisterObjectBehaviour("ScriptEntity", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptEntity, AddRef), asCALL_THISCALL); assert(r >= 0);
	r = _engine->RegisterObjectBehaviour("ScriptEntity", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptEntity, Release), asCALL_THISCALL); assert(r >= 0);
	r = _engine->RegisterObjectBehaviour("ScriptEntity", asBEHAVE_GET_WEAKREF_FLAG, "int &f()", asMETHOD(ScriptEntity, GetWeakRefFlag), asCALL_THISCALL); assert(r >= 0);

	r = _engine->RegisterObjectType("BaseComponent", 0, asOBJ_REF | asOBJ_NOCOUNT); assert(r >= 0);

	r = _engine->RegisterObjectMethod("ScriptEntity", "bool IsLookingAt(ScriptEntity @, float)", asMETHOD(ScriptEntity, IsLookingAt), asCALL_THISCALL); assert(r >= 0);
	r = _engine->RegisterObjectMethod("ScriptEntity", "BaseComponent@ GetComponent(const string &in)", asMETHOD(ScriptEntity, GetComponent), asCALL_THISCALL); assert(r >= 0);

	

	r = _engine->RegisterObjectType("Transform", 0, asOBJ_REF | asOBJ_NOCOUNT); assert(r >= 0);
	r = _engine->RegisterObjectMethod("Transform", "float GetYaw()", asMETHOD(Transform, GetYaw), asCALL_THISCALL); assert(r >= 0);
	r = _engine->RegisterObjectMethod("Transform", "float SetYaw(float)", asMETHOD(Transform, SetYaw), asCALL_THISCALL); assert(r >= 0);
	

	g_pEnv->_resourceSystem->RegisterResourceLoader(this);

	return true;
}

void AngelScript::Destroy()
{
	_engine->ShutDownAndRelease();

	g_pEnv->_resourceSystem->UnregisterResourceLoader(this);
}

//void AngelScript::MessageCallback(const std::string& in)
//{
//	bool a = false;
//}

IResource* AngelScript::LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
{
	ScriptLoadOptions* scriptOptions = (ScriptLoadOptions*)options;

	auto moduleName = absolutePath.stem().string();

	// performs a pre-processing pass if necessary, and then tells
	// the engine to build a script module.
	CScriptBuilder builder;
	int r = builder.StartNewModule(_engine, moduleName.c_str());
	if (r < 0)
	{
		// If the code fails here it is usually because there
		// is no more memory to allocate the module
		printf("Unrecoverable error while starting a new module.\n");
		return nullptr;
	}
	r = builder.AddSectionFromFile(absolutePath.string().c_str());
	if (r < 0)
	{
		// The builder wasn't able to load the file. Maybe the file
		// has been removed, or the wrong name was given, or some
		// preprocessing commands are incorrectly written.
		printf("Please correct the errors in the script and try again.\n");
		return nullptr;
	}
	r = builder.BuildModule();
	if (r < 0)
	{
		// An error occurred. Instruct the script writer to fix the 
		// compilation errors that were listed in the output stream.
		printf("Please correct the errors in the script and try again.\n");
		return nullptr;
	}

	// Find the function that is to be called. 
	asIScriptModule* mod = _engine->GetModule(moduleName.c_str());

	AngelScriptFile* script = new AngelScriptFile;
	script->_component = scriptOptions->component;
	script->Create(mod, _engine);

	//asIScriptFunction* func = mod->GetFunctionByDecl("void main()");
	//if (func == 0)
	//{
	//	// The function couldn't be found. Instruct the script writer
	//	// to include the expected function in the script.
	//	printf("The script must have the function 'void main()'. Please add it and try again.\n");
	//	return nullptr;
	//}

	//// Create our context, prepare it, and then execute
	//asIScriptContext* ctx = _engine->CreateContext();
	//ctx->Prepare(func);
	//
	//r = ctx->Execute();
	//if (r != asEXECUTION_FINISHED)
	//{
	//	// The execution didn't complete as expected. Determine what happened.
	//	if (r == asEXECUTION_EXCEPTION)
	//	{
	//		// An exception occurred, let the script writer know what happened so it can be corrected.
	//		printf("An exception '%s' occurred. Please correct the code and try again.\n", ctx->GetExceptionString());
	//	}
	//}

	return script;
}

IResource* AngelScript::LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options)
{
	return nullptr;
}

void AngelScript::UnloadResource(IResource* resource)
{
	SAFE_DELETE(resource);
}

std::vector<std::string> AngelScript::GetSupportedResourceExtensions()
{
	return { ".as" };
}

std::wstring AngelScript::GetResourceDirectory() const
{
	return L"Scripts";
}