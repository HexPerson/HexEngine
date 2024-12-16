
#pragma once

#include <HexEngine.Core/HexEngine.hpp>

#include <angelscript.h>
#include "add_on/scriptstdstring/scriptstdstring.h"
#include "add_on/scriptbuilder/scriptbuilder.h"

#define INTERFACE_NAME "IHexEngineScriptComponent"

class AngelScript : public IScriptEngine, public IResourceLoader
{
public:
	virtual bool Create() override;

	virtual void Destroy() override;

	// IResourceLoader methods
	virtual IResource*					LoadResourceFromFile(const fs::path& absolutePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
	virtual IResource*					LoadResourceFromMemory(const std::vector<uint8_t>& data, const fs::path& relativePath, FileSystem* fileSystem, const ResourceLoadOptions* options = nullptr) override;
	virtual void						UnloadResource(IResource* resource) override;
	virtual std::vector<std::string>	GetSupportedResourceExtensions() override;
	virtual std::wstring				GetResourceDirectory() const override;
	virtual void						SaveResource(IResource* resource, const fs::path& path) override { }

private:
	//void MessageCallback(const std::string& in);

private:
	asIScriptEngine* _engine = nullptr;
};