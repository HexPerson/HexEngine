
#pragma once

#include "UpdateComponent.hpp"
#include "../../Scripting/ScriptFile.hpp"
#include "../../GUI/Elements/LineEdit.hpp"

namespace HexEngine
{
	class ScriptComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(ScriptComponent);
		DEFINE_COMPONENT_CTOR(ScriptComponent);

		void SetScript(const std::shared_ptr<ScriptFile>& script);
		void SetScript(const fs::path& path);

		virtual void Update(float frameTime) override;

		virtual void FixedUpdate(float frameTime) override;

		virtual void LateUpdate(float frameTime) override;

		virtual void OnGui(GuiRenderer* renderer);

		virtual bool CreateWidget(ComponentWidget* widget) override;

	private:
		void SetScriptFromWidget(LineEdit* edit, const fs::path& path);

	private:
		std::shared_ptr<ScriptFile> _script;
	};
}
