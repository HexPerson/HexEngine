
#pragma once

#include "../FileSystem/IResource.hpp"

namespace HexEngine
{
	class ScriptComponent;

	struct ScriptLoadOptions : ResourceLoadOptions
	{
		ScriptComponent* component;
	};	

	class ScriptFile : public IResource
	{
	public:
		static std::shared_ptr<ScriptFile> Create(const fs::path& path, ScriptComponent* component);

		virtual void Update(float dt) {};
		virtual void FixedUpdate(float dt) {};
		virtual void LateUpdate(float dt) {};

	public:
		ScriptComponent* _component;
	};
}
