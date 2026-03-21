
#include "Inspector.hpp"
#include "../EditorUI.hpp"

namespace HexEditor
{
	Inspector::Inspector(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size) :
		Dock(parent, position, size, Dock::Anchor::Right)
	{
		_tabs = new HexEngine::TabView(this, HexEngine::Point(0, 10), HexEngine::Point(size.x, size.y - 20));

		auto entityTab = _tabs->AddTab(L"Entity");		
		{
			_entityName = new HexEngine::LineEdit(entityTab, HexEngine::Point(5, HexEngine::g_pEnv->GetUIManager().GetRenderer()->_style.tab_height), HexEngine::Point(size.x - 10, 25), L"");
			_entityName->SetOnInputFn(std::bind(&Inspector::OnChangeEntityName, this, std::placeholders::_2));

			_deleteBtn = new HexEngine::Button(entityTab, HexEngine::Point(size.x - 75, 50), HexEngine::Point(70, 25), L"Delete", std::bind(&Inspector::OnDeleteEntity, this, std::placeholders::_1));
			_deleteBtn->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(237, 28, 36, 255)));

			_visBtnTextures[0] = HexEngine::ITexture2D::Create("EngineData.Textures/UI/ui_invisible.png");
			_visBtnTextures[1] = HexEngine::ITexture2D::Create("EngineData.Textures/UI/ui_visible.png");

			_toggleVisibilityBtn = new HexEngine::Button(entityTab, HexEngine::Point(5, 50), HexEngine::Point(30, 25), _visBtnTextures[1], std::bind(&Inspector::OnToggleEntityVisible, this, std::placeholders::_1));

			_addComponentBtn = new HexEngine::Button(entityTab, HexEngine::Point(5, 80), HexEngine::Point(size.x - 10, 25), L"Add Component...", std::bind(&Inspector::OnAddComponent, this, std::placeholders::_1));
		}

		auto resourceTab = _tabs->AddTab(L"Resource");

		_canvas.Create(size.x, size.y);
		
	}

	Inspector::~Inspector()
	{
		_canvas.Destroy();
	}

	bool Inspector::IsInspectingEntity() const
	{
		return _inspecting != nullptr;
	}

	bool Inspector::IsInspectingResource() const
	{
		return _tabs->GetCurrentTabIndex() == 1;
	}

	HexEngine::Entity* Inspector::GetInspectingEntity() const
	{
		return _inspecting;
	}

	bool Inspector::OnDeleteEntity(HexEngine::Button* button)
	{
		if (_inspecting)
		{
			g_pUIManager->RecordEntityDeleted(_inspecting);
			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->DestroyEntity(_inspecting);

			ClearInspectorWidgets();

			_inspecting = nullptr;

			_entityName->SetValue(L"");

			_canvas.Redraw();

			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->ForceRebuildPVS();
		}

		return true;
	}

	bool Inspector::OnToggleEntityVisible(HexEngine::Button* button)
	{
		if (_inspecting)
		{
			const bool beforeHidden = _inspecting->HasFlag(HexEngine::EntityFlags::DoNotRender);

			_inspecting->ToggleVisibility();
			HexEngine::g_pEnv->_sceneManager->GetCurrentScene()->ForceRebuildPVS();

			const bool afterHidden = _inspecting->HasFlag(HexEngine::EntityFlags::DoNotRender);
			_toggleVisibilityBtn->SetIcon(afterHidden ? _visBtnTextures[0] : _visBtnTextures[1]);

			if (g_pUIManager != nullptr)
			{
				g_pUIManager->RecordEntityVisibilityChange(_inspecting, beforeHidden, afterHidden);
			}
		}

		return true;
	}

	void Inspector::ClearInspectorWidgets()
	{
		// free any component widgets from the last inspected entity
		for (auto& widget : _componentWidgets)
		{
			widget->DeleteMe();
		}
		_componentWidgets.clear();

		_canvas.Redraw();
	}

	void Inspector::OnChangeEntityName(const std::wstring& name)
	{
		if (_inspecting)
		{
			std::string requestedName(name.begin(), name.end());
			if (requestedName.empty())
			{
				_entityName->SetValue(std::wstring(_inspecting->GetName().begin(), _inspecting->GetName().end()));
				return;
			}

			auto* scene = _inspecting->GetScene();
			if (scene == nullptr)
				return;

			const std::string beforeName = _inspecting->GetName();
			std::string finalName;
			if (!scene->RenameEntity(_inspecting, requestedName, &finalName))
			{
				_entityName->SetValue(std::wstring(beforeName.begin(), beforeName.end()));
				return;
			}

			if (beforeName != finalName && g_pUIManager != nullptr)
			{
				g_pUIManager->RecordEntityRename(_inspecting, beforeName, finalName);
				g_pUIManager->GetEntityTreeList()->RefreshList();
			}

			if (finalName != requestedName)
			{
				_entityName->SetValue(std::wstring(finalName.begin(), finalName.end()));
			}
		}			
	}

	void Inspector::InspectEntity(HexEngine::Entity* entity)
	{
		if (_inspecting && _inspecting != entity)
			_inspecting->ClearFlags(HexEngine::EntityFlags::SelectedInEditor);

		if (!entity)
		{
			_inspecting = nullptr;
			ClearInspectorWidgets();
			return;
		}
		if (_addComponentContextMenu)
		{
			_addComponentContextMenu->Disable();
			_addComponentContextMenu->DeleteMe();
			_addComponentContextMenu = nullptr;
		}

		_tabs->SetActiveTab(0);
		_entityName->SetValue(std::wstring(entity->GetName().begin(), entity->GetName().end()));
		_entityName->SetHasInputFocus(false);
		_toggleVisibilityBtn->SetIcon(entity->HasFlag(HexEngine::EntityFlags::DoNotRender) ? _visBtnTextures[0] : _visBtnTextures[1]);

		if (entity != _inspecting)
		{
			auto size = GetSize();

			size.x -= 10;
			size.y -= 20;

			ClearInspectorWidgets();

			_inspecting = entity;

			_inspecting->SetFlag(HexEngine::EntityFlags::SelectedInEditor);

			int32_t y = 130;

			for (auto& component : entity->GetAllComponents())
			{
				auto componentName = component->GetComponentName();

				HexEngine::ComponentWidget* widget = new HexEngine::ComponentWidget(this, HexEngine::Point(5, y), HexEngine::Point(size.x, size.y), s2ws(componentName));

				if (component->CreateWidget(widget) == false)
				{
					widget->DeleteMe();
					continue;
				}

				widget->CalculateLargestLabelWidth();

				if (component->GetComponentId() != HexEngine::Transform::_GetComponentId())
				{
					auto* removeBtn = new HexEngine::Button(
						widget,
						HexEngine::Point(widget->GetSize().x - 26, 2),
						HexEngine::Point(20, 16),
						L"X",
						[this, component](HexEngine::Button*) { return OnRemoveComponent(component); });
					removeBtn->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(237, 28, 36, 255)));
				}

				_componentWidgets.push_back(widget);

				y += widget->GetTotalHeight() + 10;
			}
		}

		_canvas.Redraw();
	}

	void Inspector::InspectResource(const fs::path& path)
	{
		_tabs->SetActiveTab(1);

		ClearInspectorWidgets();

		_inspecting = nullptr;

		_canvas.Redraw();
	}

	bool Inspector::OnAddComponent(HexEngine::Button* button)
	{
		if (_addComponentContextMenu == nullptr)
		{
			int32_t mx, my;
			HexEngine::g_pEnv->_inputSystem->GetMousePosition(mx, my);

			mx -= _addComponentBtn->GetAbsolutePosition().x;
			my -= _addComponentBtn->GetAbsolutePosition().y;

			_addComponentContextMenu = new HexEngine::ContextMenu(
				_addComponentBtn,
				HexEngine::Point(button->GetPosition().x, /*button->GetPosition().y +*/ button->GetSize().y));

			const auto& classes = HexEngine::g_pEnv->_classRegistry->GetAllClasses();

			for (auto& cls : classes)
			{
				std::wstring compName(cls.second.name.begin(), cls.second.name.end());

				_addComponentContextMenu->AddItem(new HexEngine::ContextItem(compName, std::bind(&Inspector::OnClickAddComponentItem, this, std::placeholders::_1, cls.second.compId)));
			}
			/*_addComponentContextMenu->AddItem({ L"Rigid Body", std::bind(&Inspector::OnClickAddComponentItem, this, std::placeholders::_1, RigidBody::_GetComponentId()) });
			_addComponentContextMenu->AddItem({ L"Hinge Joint", std::bind(&Inspector::OnClickAddComponentItem, this, std::placeholders::_1, HingeJoint::_GetComponentId()) });*/
			_addComponentContextMenu->BringToFront();
		}


		_addComponentContextMenu->Enable();

		_canvas.Redraw();

		//_addComponentBtn->EnableInput(false);

		return true;
	}

	void Inspector::OnClickAddComponentItem(const std::wstring& name, HexEngine::ComponentId compId)
	{
		_addComponentContextMenu->Disable();
		_addComponentContextMenu->DeleteMe();
		_addComponentContextMenu = nullptr;

		_addComponentBtn->EnableInput(true);

		CON_ECHO("Adding component name '%S' Id=%d", name.c_str(), compId);

		if (_inspecting)
		{
			auto cls = HexEngine::g_pEnv->_classRegistry->FindByComponentId(compId);
			if (cls == nullptr)
			{
				LOG_WARN("Could not find class metadata for component id %u", compId);
				return;
			}

			if (compId == HexEngine::Transform::_GetComponentId())
			{
				LOG_WARN("Adding a second Transform component is not supported");
				return;
			}

			HexEngine::BaseComponent* component = cls->newInstanceFn(_inspecting);
			if (component == nullptr)
			{
				LOG_WARN("Failed to create component instance for id %u", compId);
				return;
			}

			_inspecting->AddComponent(component);

			if (g_pUIManager != nullptr)
			{
				g_pUIManager->RecordComponentAdded(component);
			}
		}

		// inspect it again
		auto ent = _inspecting;
		_inspecting = nullptr;
		InspectEntity(ent);
	}

	bool Inspector::OnRemoveComponent(HexEngine::BaseComponent* component)
	{
		if (_inspecting == nullptr || component == nullptr)
			return true;

		if (component->GetEntity() != _inspecting)
			return true;

		if (component->GetComponentId() == HexEngine::Transform::_GetComponentId())
		{
			LOG_WARN("Cannot remove Transform component from entity '%s'", _inspecting->GetName().c_str());
			return true;
		}

		if (g_pUIManager != nullptr)
		{
			g_pUIManager->RecordComponentDeleted(component);
		}

		_inspecting->RemoveComponent(component);

		auto* entity = _inspecting;
		_inspecting = nullptr;
		InspectEntity(entity);

		return true;
	}

	void Inspector::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		//renderer->SetDrawList(&_drawList);

		//if (_canvas.BeginDraw(renderer, w, h))
		{
			//Dock::Render(renderer, w, h);

			renderer->FillQuad(_position.x, _position.y, _size.x, _size.y, renderer->_style.win_back);

			
		}

		
	}

	void Inspector::InspectComponent(HexEngine::Point& pos, HexEngine::BaseComponent* component, HexEngine::GuiRenderer* renderer)
	{
		
	}

	void Inspector::PostRenderChildren(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		//renderer->ListDraw(&_drawList);

		/*if (_canvas.NeedsRedrawing())
		{
			_canvas.EndDraw(renderer);
		}

		_canvas.Present(
			renderer,
			GetAbsolutePosition().x, GetAbsolutePosition().y,
			_size.x, _size.y);*/
	}
}
