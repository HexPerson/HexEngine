
#include "ScriptComponent.hpp"

#include "../../GUI/Elements/ComponentWidget.hpp"

namespace HexEngine
{
	ScriptComponent::ScriptComponent(Entity* entity) :
		UpdateComponent(entity)
	{

	}

	ScriptComponent::ScriptComponent(Entity* entity, ScriptComponent* copy) :
		UpdateComponent(entity)
	{

	}

	void ScriptComponent::SetScript(const std::shared_ptr<ScriptFile>& script)
	{
		_script = script;
		_script->_component = this;
	}

	void ScriptComponent::SetScript(const fs::path& path)
	{
		SetScript(ScriptFile::Create(path,this));
	}

	void ScriptComponent::Update(float frameTime)
	{
		if (!_script)
			return;

		_script->Update(frameTime);
	}

	void ScriptComponent::FixedUpdate(float frameTime)
	{

	}

	void ScriptComponent::LateUpdate(float frameTime)
	{

	}

	void ScriptComponent::OnGui(GuiRenderer* renderer)
	{

	}

	void ScriptComponent::Serialize(json& data, JsonFile* file)
	{
		fs::path scriptPath;
		if (_script != nullptr)
		{
			scriptPath = _script->GetAbsolutePath();
		}

		file->Serialize(data, "_scriptPath", scriptPath);
	}

	void ScriptComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		fs::path scriptPath;
		file->Deserialize(data, "_scriptPath", scriptPath);

		if (!scriptPath.empty())
		{
			SetScript(scriptPath);
		}
	}

	bool ScriptComponent::CreateWidget(ComponentWidget* widget)
	{
		LineEdit* scriptName = new LineEdit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Script");
		scriptName->SetPrefabOverrideBinding(GetComponentName(), "/_scriptPath");

		if (_script)
		{
			auto scriptPath = _script->GetAbsolutePath().filename().wstring();

			scriptName->SetValue(scriptPath);
		}

		scriptName->SetOnDragAndDropFn(std::bind(&ScriptComponent::SetScriptFromWidget, this, std::placeholders::_1, std::placeholders::_2));
		return true;
	}

	void ScriptComponent::SetScriptFromWidget(LineEdit* edit, const fs::path& path)
	{
		edit->SetValue(path.filename().wstring());

		SetScript(path);
	}
}
