
#include "Inspector.hpp"
#include "../EditorUI.hpp"
#include <HexEngine.Core\GUI\Elements\Dialog.hpp>
#include <HexEngine.Core\GUI\Elements\ContextMenu.hpp>
#include "../Gadgets/Gadget.hpp"
#include <algorithm>
#include <cwctype>

namespace HexEditor
{
	namespace
	{
		std::wstring ToLowerCopy(const std::wstring& text)
		{
			std::wstring lowered = text;
			std::transform(lowered.begin(), lowered.end(), lowered.begin(),
				[](wchar_t c) { return (wchar_t)std::towlower(c); });
			return lowered;
		}

		bool ElementTreeMatchesFilter(HexEngine::Element* element, const std::wstring& filterLower)
		{
			if (element == nullptr || filterLower.empty())
				return true;

			const std::wstring labelLower = ToLowerCopy(element->GetLabelText());
			if (!labelLower.empty() && labelLower.find(filterLower) != std::wstring::npos)
				return true;

			for (auto* child : element->GetChildren())
			{
				if (ElementTreeMatchesFilter(child, filterLower))
					return true;
			}

			return false;
		}
	}

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
			_propertyFilter = new HexEngine::LineEdit(entityTab, HexEngine::Point(40, 50), HexEngine::Point(std::max(100, size.x - 120), 25), L"");
			_propertyFilter->SetUneditableText(L"Filter ");
			_propertyFilter->SetDoesCallbackWaitForReturn(false);
			_propertyFilter->SetOnInputFn(std::bind(&Inspector::OnInspectorFilterChanged, this, std::placeholders::_1, std::placeholders::_2));

			_addComponentBtn = new HexEngine::Button(entityTab, HexEngine::Point(5, 80), HexEngine::Point(size.x - 10, 25), L"Add Component...", std::bind(&Inspector::OnAddComponent, this, std::placeholders::_1));

			const int32_t prefabButtonWidth = std::max(40, (size.x - 15) / 2);
			_revertPrefabBtn = new HexEngine::Button(entityTab, HexEngine::Point(5, 110), HexEngine::Point(prefabButtonWidth, 22), L"Revert Prefab", std::bind(&Inspector::OnRevertPrefabInstance, this, std::placeholders::_1));
			_applyPrefabBtn = new HexEngine::Button(entityTab, HexEngine::Point(10 + prefabButtonWidth, 110), HexEngine::Point(prefabButtonWidth, 22), L"Apply Prefab", std::bind(&Inspector::OnApplyPrefabInstance, this, std::placeholders::_1));
			_prefabOverridesBtn = new HexEngine::Button(entityTab, HexEngine::Point(5, 136), HexEngine::Point(size.x - 10, 22), L"Overrides...", std::bind(&Inspector::OnOpenPrefabOverrides, this, std::placeholders::_1));
			_revertPrefabBtn->DisableRecursive();
			_applyPrefabBtn->DisableRecursive();
			_prefabOverridesBtn->DisableRecursive();
		}

		auto resourceTab = _tabs->AddTab(L"Resource");

		_canvas.Create(size.x, size.y);
		
	}

	Inspector::~Inspector()
	{
		ClosePrefabOverrideDialog();
		ClosePrefabApplyPreviewDialog();
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

		for (auto& gadget : g_pUIManager->GetAllGadgets())
		{
			gadget->StopGadget(GadgetAction::Confirm);
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

	void Inspector::OnInspectorFilterChanged(HexEngine::LineEdit* edit, const std::wstring& value)
	{
		(void)edit;
		(void)value;
		if (_inspecting != nullptr)
		{
			InspectEntity(_inspecting, true);
		}
	}

	void Inspector::InspectEntity(HexEngine::Entity* entity)
	{
		InspectEntity(entity, false);
	}

	void Inspector::RequestForcedRefresh(HexEngine::Entity* entity)
	{
		if (entity == nullptr)
		{
			_pendingForcedRefresh = false;
			_pendingRefreshEntity = nullptr;
			return;
		}

		_pendingForcedRefresh = true;
		_pendingRefreshEntity = entity;
	}

	HexEngine::Element* Inspector::FindFocusedElementRecursive(HexEngine::Element* root)
	{
		if (root == nullptr)
			return nullptr;

		if (root->IsInputFocus())
			return root;

		for (auto* child : root->GetChildren())
		{
			if (auto* focused = FindFocusedElementRecursive(child); focused != nullptr)
				return focused;
		}

		return nullptr;
	}

	bool Inspector::IsDescendantOfElement(const HexEngine::Element* element, const HexEngine::Element* ancestor)
	{
		if (element == nullptr || ancestor == nullptr)
			return false;

		for (auto* current = element; current != nullptr; current = current->GetParent())
		{
			if (current == ancestor)
				return true;
		}

		return false;
	}

	bool Inspector::HasBlockingExternalDialogOpen() const
	{
		auto* root = HexEngine::g_pEnv->GetUIManager().GetRootElement();
		if (root == nullptr)
			return false;

		std::vector<HexEngine::Element*> stack;
		stack.push_back(root);

		while (!stack.empty())
		{
			auto* current = stack.back();
			stack.pop_back();
			if (current == nullptr)
				continue;

			if (!current->WantsDeletion())
			{
				if (auto* contextMenu = dynamic_cast<HexEngine::ContextMenu*>(current); contextMenu != nullptr)
				{
					if (contextMenu->IsEnabled())
					{
						return true;
					}
				}
			}

			if (current != _prefabOverridesDialog &&
				current != _prefabApplyPreviewDialog &&
				!current->WantsDeletion() &&
				dynamic_cast<HexEngine::Dialog*>(current) != nullptr &&
				!IsDescendantOfElement(current, this))
			{
				return true;
			}

			for (auto* child : current->GetChildren())
			{
				stack.push_back(child);
			}
		}

		return false;
	}

	void Inspector::InspectEntity(HexEngine::Entity* entity, bool forceRebuild)
	{
		if (_inspecting && _inspecting != entity)
			_inspecting->ClearFlags(HexEngine::EntityFlags::SelectedInEditor);

		if (!entity)
		{
			_inspecting = nullptr;
			_pendingForcedRefresh = false;
			_pendingRefreshEntity = nullptr;
			ClosePrefabOverrideDialog();
			ClosePrefabApplyPreviewDialog();
			if (_revertPrefabBtn != nullptr)
				_revertPrefabBtn->DisableRecursive();
			if (_applyPrefabBtn != nullptr)
			{
				_applyPrefabBtn->DisableRecursive();
				_applyPrefabBtn->RemoveHighlightOverride();
			}
			if (_prefabOverridesBtn != nullptr)
			{
				_prefabOverridesBtn->DisableRecursive();
				_prefabOverrideRows.clear();
			}
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

		const bool showPrefabButtons =
			(g_pUIManager != nullptr) &&
			g_pUIManager->IsPrefabInstanceRootEntity(entity) &&
			!g_pUIManager->IsPrefabStageActive();
		const bool showPrefabFieldOverrides =
			(g_pUIManager != nullptr) &&
			g_pUIManager->IsPrefabInstanceEntity(entity) &&
			!g_pUIManager->IsPrefabStageActive();
		const bool hasPrefabOverrides =
			showPrefabButtons &&
			g_pUIManager->HasPrefabInstanceOverrides(entity);
		std::vector<PrefabController::PrefabPropertyOverride> prefabPropertyOverrides;
		if (showPrefabFieldOverrides)
		{
			g_pUIManager->GetPrefabInstancePropertyOverrides(entity, prefabPropertyOverrides);
		}
		const bool showVariantOverrideControls =
			(g_pUIManager != nullptr) &&
			g_pUIManager->IsVariantStageEntity(entity);
		std::unordered_set<std::string> variantOverriddenComponents;
		if (showVariantOverrideControls)
		{
			g_pUIManager->GetVariantStageEntityOverrideComponents(entity, variantOverriddenComponents);
		}

		if (_revertPrefabBtn != nullptr && _applyPrefabBtn != nullptr)
		{
			if (showPrefabButtons)
			{
				_revertPrefabBtn->EnableRecursive();
				if (hasPrefabOverrides)
				{
					_applyPrefabBtn->EnableRecursive();
					_applyPrefabBtn->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(30, 180, 90, 255)));
				}
				else
				{
					_applyPrefabBtn->DisableRecursive();
					_applyPrefabBtn->RemoveHighlightOverride();
				}
			}
			else
			{
				_revertPrefabBtn->DisableRecursive();
				_applyPrefabBtn->DisableRecursive();
				_applyPrefabBtn->RemoveHighlightOverride();
			}
		}

		if (_prefabOverridesBtn != nullptr)
		{
			if (showPrefabButtons && hasPrefabOverrides)
				_prefabOverridesBtn->EnableRecursive();
			else
				_prefabOverridesBtn->DisableRecursive();
		}

		if ((!showPrefabButtons || !hasPrefabOverrides) && _prefabOverridesDialog != nullptr)
		{
			ClosePrefabOverrideDialog();
		}

		const bool shouldRebuildWidgets = forceRebuild || (entity != _inspecting) || showVariantOverrideControls;
		if (shouldRebuildWidgets)
		{
			auto size = GetSize();

			size.x -= 10;
			size.y -= 20;

			if (entity != _inspecting)
			{
				ClosePrefabOverrideDialog();
				ClosePrefabApplyPreviewDialog();
			}

			ClearInspectorWidgets();

			_inspecting = entity;

			_inspecting->SetFlag(HexEngine::EntityFlags::SelectedInEditor);

			int32_t y = showPrefabButtons ? 165 : 130;

			if (hasPrefabOverrides)
				y += 10;

			for (auto& component : entity->GetAllComponents())
			{
				auto componentName = component->GetComponentName();

				HexEngine::ComponentWidget* widget = new HexEngine::ComponentWidget(this, HexEngine::Point(5, y), HexEngine::Point(size.x, size.y), s2ws(componentName));

				if (component->CreateWidget(widget) == false)
				{
					widget->DeleteMe();
					continue;
				}

				if (_propertyFilter != nullptr)
				{
					const std::wstring filterLower = ToLowerCopy(_propertyFilter->GetValue());
					if (!filterLower.empty() && !ElementTreeMatchesFilter(widget, filterLower))
					{
						widget->DeleteMe();
						continue;
					}
				}

				widget->CalculateLargestLabelWidth();
				const bool componentOverridden = showVariantOverrideControls &&
					(variantOverriddenComponents.find(componentName) != variantOverriddenComponents.end());
				widget->SetOverrideActive(componentOverridden);

				if (showPrefabFieldOverrides && !prefabPropertyOverrides.empty())
				{
					AddInlinePrefabOverrideMarkers(widget, componentName, prefabPropertyOverrides);
				}

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

				if (componentOverridden)
				{
					const int32_t revertButtonX = component->GetComponentId() != HexEngine::Transform::_GetComponentId()
						? widget->GetSize().x - 86
						: widget->GetSize().x - 60;

					auto* revertBtn = new HexEngine::Button(
						widget,
						HexEngine::Point(revertButtonX, 2),
						HexEngine::Point(56, 16),
						L"Revert",
						[this, componentName](HexEngine::Button*)
						{
							return OnRevertVariantComponent(componentName);
						});
					revertBtn->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(36, 122, 74, 255)));
				}

				_componentWidgets.push_back(widget);

				y += widget->GetTotalHeight() + 10;
			}
		}
		else
		{
			_inspecting = entity;
			_inspecting->SetFlag(HexEngine::EntityFlags::SelectedInEditor);
		}

		if (g_pUIManager != nullptr && g_pUIManager->GetEntityTreeList() != nullptr)
		{
			g_pUIManager->GetEntityTreeList()->FocusEntity(entity);
		}

		_canvas.Redraw();
	}

	void Inspector::InspectResource(const fs::path& path)
	{
		_tabs->SetActiveTab(1);

		_pendingForcedRefresh = false;
		_pendingRefreshEntity = nullptr;
		ClosePrefabOverrideDialog();
		ClosePrefabApplyPreviewDialog();
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

	bool Inspector::OnRevertPrefabInstance(HexEngine::Button* button)
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr)
			return true;

		auto* newRoot = g_pUIManager->RevertPrefabInstance(_inspecting);
		if (newRoot != nullptr)
		{
			_inspecting = nullptr;
			InspectEntity(newRoot);
		}

		return true;
	}

	bool Inspector::OnRevertVariantComponent(const std::string& componentName)
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr || componentName.empty())
			return true;

		if (!g_pUIManager->RevertVariantStageComponentToBase(_inspecting, componentName))
			return true;

		auto* entity = _inspecting;
		_inspecting = nullptr;
		InspectEntity(entity);
		return true;
	}

	bool Inspector::OnApplyPrefabInstance(HexEngine::Button* button)
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr)
			return true;

		if (!ReloadPrefabApplyPreviewRows())
		{
			if (g_pUIManager->ApplyPrefabInstanceToPrefabAsset(_inspecting))
			{
				auto* entity = _inspecting;
				_inspecting = nullptr;
				InspectEntity(entity);
			}
			return true;
		}

		if (_prefabApplyPreviewDialog == nullptr)
		{
			const int32_t dialogWidth = 760;
			const int32_t dialogHeight = 520;
			_prefabApplyPreviewDialog = new HexEngine::Dialog(
				HexEngine::g_pEnv->GetUIManager().GetRootElement(),
				HexEngine::Point::GetScreenCenterWithOffset(-dialogWidth / 2, -dialogHeight / 2),
				HexEngine::Point(dialogWidth, dialogHeight),
				L"Apply Prefab: Preview");

			_prefabApplyPreviewScroll = new HexEngine::ScrollView(
				_prefabApplyPreviewDialog,
				HexEngine::Point(10, 10),
				HexEngine::Point(dialogWidth - 20, dialogHeight - 62));

			auto* confirmApply = new HexEngine::Button(
				_prefabApplyPreviewDialog,
				HexEngine::Point(dialogWidth - 220, dialogHeight - 42),
				HexEngine::Point(100, 24),
				L"Apply",
				std::bind(&Inspector::OnConfirmApplyPrefabInstance, this, std::placeholders::_1));
			confirmApply->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(30, 180, 90, 255)));

			new HexEngine::Button(
				_prefabApplyPreviewDialog,
				HexEngine::Point(dialogWidth - 110, dialogHeight - 42),
				HexEngine::Point(90, 24),
				L"Cancel",
				[this](HexEngine::Button*)
				{
					ClosePrefabApplyPreviewDialog();
					return true;
				});
		}
		else
		{
			_prefabApplyPreviewDialog->BringToFront();
		}

		RebuildPrefabApplyPreviewDialogContents();
		return true;
	}

	bool Inspector::ReloadPrefabOverrideRows(bool preserveSelection)
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr)
			return false;

		std::unordered_map<std::string, bool> previousSelections;
		if (preserveSelection)
		{
			for (const auto& row : _prefabOverrideRows)
			{
				previousSelections[row.componentName + "\n" + row.path] = row.selected;
			}
		}
		_prefabOverrideRows.clear();

		std::vector<PrefabController::PrefabPropertyOverride> overrides;
		if (!g_pUIManager->GetPrefabInstancePropertyOverrides(_inspecting, overrides) || overrides.empty())
			return false;

		for (const auto& overrideEntry : overrides)
		{
			PrefabOverrideRow row;
			row.componentName = overrideEntry.componentName;
			row.path = overrideEntry.path;
			row.op = overrideEntry.op;
			row.selected = true;

			if (preserveSelection)
			{
				const auto key = row.componentName + "\n" + row.path;
				if (const auto it = previousSelections.find(key); it != previousSelections.end())
				{
					row.selected = it->second;
				}
			}

			_prefabOverrideRows.push_back(std::move(row));
		}

		std::sort(_prefabOverrideRows.begin(), _prefabOverrideRows.end(),
			[](const PrefabOverrideRow& a, const PrefabOverrideRow& b)
			{
				if (a.componentName != b.componentName)
					return a.componentName < b.componentName;
				return a.path < b.path;
			});

		return !_prefabOverrideRows.empty();
	}

	void Inspector::RebuildPrefabOverrideDialogContents()
	{
		if (_prefabOverridesDialog == nullptr || _prefabOverridesScroll == nullptr)
			return;

		auto* contentRoot = _prefabOverridesScroll->GetContentRoot();
		if (contentRoot == nullptr)
			return;

		std::vector<HexEngine::Element*> oldChildren = contentRoot->GetChildren();
		for (auto* child : oldChildren)
		{
			if (child != nullptr)
				child->DeleteMe();
		}

		const auto toWide = [](const std::string& value)
		{
			return std::wstring(value.begin(), value.end());
		};

		const int32_t contentWidth = std::max(200, _prefabOverridesScroll->GetSize().x - 20);
		const int32_t headerWidth = std::max(80, contentWidth - 116);
		int32_t y = 4;
		std::string currentComponent;

		for (size_t i = 0; i < _prefabOverrideRows.size(); ++i)
		{
			auto& row = _prefabOverrideRows[i];
			if (currentComponent != row.componentName)
			{
				currentComponent = row.componentName;
				auto* componentHeader = new HexEngine::Button(
					contentRoot,
					HexEngine::Point(0, y),
					HexEngine::Point(headerWidth, 20),
					toWide(currentComponent),
					[](HexEngine::Button*) { return true; });
				componentHeader->EnableInput(false);

				auto* revertComponentBtn = new HexEngine::Button(
					contentRoot,
					HexEngine::Point(headerWidth + 6, y),
					HexEngine::Point(110, 20),
					L"Revert Component",
					[this, componentName = currentComponent](HexEngine::Button*)
					{
						return OnRevertPrefabComponentOverrides(componentName);
					});
				revertComponentBtn->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(36, 122, 74, 255)));
				y += 24;
			}

			const std::wstring rowLabel = L"[" + toWide(row.op) + L"] " + toWide(row.path);
			auto* selectedCheck = new HexEngine::Checkbox(
				contentRoot,
				HexEngine::Point(2, y),
				HexEngine::Point(std::max(24, headerWidth), 18),
				rowLabel,
				&_prefabOverrideRows[i].selected);
			selectedCheck->SetLabelMinSize(std::max(120, headerWidth - 26));

			auto* revertPropertyBtn = new HexEngine::Button(
				contentRoot,
				HexEngine::Point(headerWidth + 6, y - 1),
				HexEngine::Point(110, 20),
				L"Revert",
				[this, componentName = row.componentName, propertyPath = row.path](HexEngine::Button*)
				{
					return OnRevertPrefabPropertyOverride(componentName, propertyPath);
				});
			revertPropertyBtn->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(36, 122, 74, 255)));
			y += 22;
		}

		_prefabOverridesScroll->SetManualContentHeight(y + 4);
	}

	void Inspector::ClosePrefabOverrideDialog()
	{
		if (_prefabOverridesDialog != nullptr)
		{
			_prefabOverridesDialog->DeleteMe();
			_prefabOverridesDialog = nullptr;
			_prefabOverridesScroll = nullptr;
		}
		_prefabOverrideRows.clear();
	}

	bool Inspector::ReloadPrefabApplyPreviewRows()
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr)
			return false;

		_prefabApplyPreviewRows.clear();

		std::vector<PrefabController::PrefabPropertyOverride> overrides;
		if (!g_pUIManager->GetPrefabInstancePropertyOverrides(_inspecting, overrides) || overrides.empty())
			return false;

		for (const auto& overrideEntry : overrides)
		{
			PrefabOverrideRow row;
			row.componentName = overrideEntry.componentName;
			row.path = overrideEntry.path;
			row.op = overrideEntry.op;
			_prefabApplyPreviewRows.push_back(std::move(row));
		}

		std::sort(_prefabApplyPreviewRows.begin(), _prefabApplyPreviewRows.end(),
			[](const PrefabOverrideRow& a, const PrefabOverrideRow& b)
			{
				if (a.componentName != b.componentName)
					return a.componentName < b.componentName;
				return a.path < b.path;
			});

		return !_prefabApplyPreviewRows.empty();
	}

	void Inspector::RebuildPrefabApplyPreviewDialogContents()
	{
		if (_prefabApplyPreviewDialog == nullptr || _prefabApplyPreviewScroll == nullptr)
			return;

		auto* contentRoot = _prefabApplyPreviewScroll->GetContentRoot();
		if (contentRoot == nullptr)
			return;

		std::vector<HexEngine::Element*> oldChildren = contentRoot->GetChildren();
		for (auto* child : oldChildren)
		{
			if (child != nullptr)
				child->DeleteMe();
		}

		const auto toWide = [](const std::string& value)
		{
			return std::wstring(value.begin(), value.end());
		};

		const int32_t contentWidth = std::max(200, _prefabApplyPreviewScroll->GetSize().x - 20);
		int32_t y = 4;

		std::wstring header = L"This will apply " + std::to_wstring(_prefabApplyPreviewRows.size()) + L" override(s) to the prefab asset:";
		auto* summary = new HexEngine::Button(
			contentRoot,
			HexEngine::Point(0, y),
			HexEngine::Point(contentWidth, 22),
			header,
			[](HexEngine::Button*) { return true; });
		summary->EnableInput(false);
		y += 26;

		std::string currentComponent;
		for (const auto& row : _prefabApplyPreviewRows)
		{
			if (currentComponent != row.componentName)
			{
				currentComponent = row.componentName;
				auto* componentHeader = new HexEngine::Button(
					contentRoot,
					HexEngine::Point(0, y),
					HexEngine::Point(contentWidth, 20),
					toWide(currentComponent),
					[](HexEngine::Button*) { return true; });
				componentHeader->EnableInput(false);
				componentHeader->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(72, 88, 110, 255)));
				y += 22;
			}

			const std::wstring rowText = L"[" + toWide(row.op) + L"] " + toWide(row.path);
			auto* rowBtn = new HexEngine::Button(
				contentRoot,
				HexEngine::Point(8, y),
				HexEngine::Point(contentWidth - 8, 18),
				rowText,
				[](HexEngine::Button*) { return true; });
			rowBtn->EnableInput(false);
			y += 20;
		}

		_prefabApplyPreviewScroll->SetManualContentHeight(y + 6);
	}

	void Inspector::ClosePrefabApplyPreviewDialog()
	{
		if (_prefabApplyPreviewDialog != nullptr)
		{
			_prefabApplyPreviewDialog->DeleteMe();
			_prefabApplyPreviewDialog = nullptr;
			_prefabApplyPreviewScroll = nullptr;
		}
		_prefabApplyPreviewRows.clear();
	}

	bool Inspector::OnOpenPrefabOverrides(HexEngine::Button* button)
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr)
			return true;

		if (!ReloadPrefabOverrideRows(true))
		{
			ClosePrefabOverrideDialog();
			return true;
		}

		if (_prefabOverridesDialog == nullptr)
		{
			const int32_t dialogWidth = 760;
			const int32_t dialogHeight = 520;
			_prefabOverridesDialog = new HexEngine::Dialog(
				HexEngine::g_pEnv->GetUIManager().GetRootElement(),
				HexEngine::Point::GetScreenCenterWithOffset(-dialogWidth / 2, -dialogHeight / 2),
				HexEngine::Point(dialogWidth, dialogHeight),
				L"Prefab Overrides");

			_prefabOverridesScroll = new HexEngine::ScrollView(
				_prefabOverridesDialog,
				HexEngine::Point(10, 10),
				HexEngine::Point(dialogWidth - 20, dialogHeight - 62));

			auto* applySelected = new HexEngine::Button(
				_prefabOverridesDialog,
				HexEngine::Point(dialogWidth - 220, dialogHeight - 42),
				HexEngine::Point(100, 24),
				L"Apply Selected",
				std::bind(&Inspector::OnApplySelectedPrefabOverrides, this, std::placeholders::_1));
			applySelected->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(30, 180, 90, 255)));

			new HexEngine::Button(
				_prefabOverridesDialog,
				HexEngine::Point(dialogWidth - 110, dialogHeight - 42),
				HexEngine::Point(90, 24),
				L"Close",
				[this](HexEngine::Button*)
				{
					ClosePrefabOverrideDialog();
					return true;
				});
		}
		else
		{
			_prefabOverridesDialog->BringToFront();
		}

		RebuildPrefabOverrideDialogContents();
		return true;
	}

	bool Inspector::OnRevertPrefabPropertyOverride(const std::string& componentName, const std::string& propertyPath)
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr)
			return true;

		Detail::EntityComponentStateSnapshot beforeSnapshot;
		const bool hasBefore = Detail::CaptureEntityComponentState(_inspecting, beforeSnapshot);

		if (!g_pUIManager->RevertPrefabInstancePropertyOverride(_inspecting, componentName, propertyPath))
			return true;

		if (hasBefore)
		{
			Detail::EntityComponentStateSnapshot afterSnapshot;
			if (Detail::CaptureEntityComponentState(_inspecting, afterSnapshot))
			{
				g_pUIManager->RecordComponentPropertyStateChange(_inspecting, beforeSnapshot, afterSnapshot);
			}
		}

		InspectEntity(_inspecting);
		if (!ReloadPrefabOverrideRows(true))
		{
			ClosePrefabOverrideDialog();
		}
		else
		{
			RebuildPrefabOverrideDialogContents();
		}

		return true;
	}

	bool Inspector::OnRevertPrefabComponentOverrides(const std::string& componentName)
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr)
			return true;

		Detail::EntityComponentStateSnapshot beforeSnapshot;
		const bool hasBefore = Detail::CaptureEntityComponentState(_inspecting, beforeSnapshot);

		if (!g_pUIManager->RevertPrefabInstanceComponentOverrides(_inspecting, componentName))
			return true;

		if (hasBefore)
		{
			Detail::EntityComponentStateSnapshot afterSnapshot;
			if (Detail::CaptureEntityComponentState(_inspecting, afterSnapshot))
			{
				g_pUIManager->RecordComponentPropertyStateChange(_inspecting, beforeSnapshot, afterSnapshot);
			}
		}

		InspectEntity(_inspecting);
		if (!ReloadPrefabOverrideRows(true))
		{
			ClosePrefabOverrideDialog();
		}
		else
		{
			RebuildPrefabOverrideDialogContents();
		}

		return true;
	}

	bool Inspector::OnApplySelectedPrefabOverrides(HexEngine::Button* button)
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr)
			return true;

		std::vector<PrefabController::PrefabPropertyOverride> selected;
		for (const auto& row : _prefabOverrideRows)
		{
			if (!row.selected)
				continue;

			PrefabController::PrefabPropertyOverride overrideEntry;
			overrideEntry.componentName = row.componentName;
			overrideEntry.path = row.path;
			overrideEntry.op = row.op;
			selected.push_back(std::move(overrideEntry));
		}

		if (selected.empty())
			return true;

		Detail::EntityComponentStateSnapshot beforeSnapshot;
		const bool hasBefore = Detail::CaptureEntityComponentState(_inspecting, beforeSnapshot);

		if (!g_pUIManager->ApplySelectedPrefabInstanceOverridesToAsset(_inspecting, selected))
			return true;

		if (hasBefore)
		{
			Detail::EntityComponentStateSnapshot afterSnapshot;
			if (Detail::CaptureEntityComponentState(_inspecting, afterSnapshot))
			{
				g_pUIManager->RecordComponentPropertyStateChange(_inspecting, beforeSnapshot, afterSnapshot);
			}
		}

		InspectEntity(_inspecting);
		if (!ReloadPrefabOverrideRows(true))
		{
			ClosePrefabOverrideDialog();
		}
		else
		{
			RebuildPrefabOverrideDialogContents();
		}

		return true;
	}

	bool Inspector::OnConfirmApplyPrefabInstance(HexEngine::Button* button)
	{
		if (_inspecting == nullptr || g_pUIManager == nullptr)
		{
			ClosePrefabApplyPreviewDialog();
			return true;
		}

		ClosePrefabApplyPreviewDialog();
		if (g_pUIManager->ApplyPrefabInstanceToPrefabAsset(_inspecting))
		{
			auto* entity = _inspecting;
			_inspecting = nullptr;
			InspectEntity(entity);
		}

		return true;
	}

	std::string Inspector::NormalizeOverrideToken(const std::string& text)
	{
		std::string normalized;
		normalized.reserve(text.size());

		for (const unsigned char ch : text)
		{
			if (std::isalnum(ch))
			{
				normalized.push_back((char)std::tolower(ch));
			}
		}

		return normalized;
	}

	std::string Inspector::NormalizeOverrideToken(const std::wstring& text)
	{
		std::string normalized;
		normalized.reserve(text.size());

		for (const wchar_t ch : text)
		{
			if (ch >= L'0' && ch <= L'9')
			{
				normalized.push_back((char)ch);
			}
			else if (ch >= L'a' && ch <= L'z')
			{
				normalized.push_back((char)ch);
			}
			else if (ch >= L'A' && ch <= L'Z')
			{
				normalized.push_back((char)(ch - L'A' + L'a'));
			}
		}

		return normalized;
	}

	bool Inspector::IsOverridePathMatchOrParent(const std::string& patchPath, const std::string& selectedPath)
	{
		if (patchPath == selectedPath)
			return true;

		if (selectedPath.empty())
			return false;

		const std::string selectedPrefix = selectedPath + "/";
		if (patchPath.rfind(selectedPrefix, 0) == 0)
			return true;

		const std::string patchPrefix = patchPath + "/";
		return selectedPath.rfind(patchPrefix, 0) == 0;
	}

	bool Inspector::IsOverridePathMatchingLabel(const std::string& path, const std::wstring& label)
	{
		const std::string normalizedLabel = NormalizeOverrideToken(label);
		if (normalizedLabel.empty())
			return false;

		std::vector<std::string> normalizedTokens;
		std::string token;
		for (char ch : path)
		{
			if (ch == '/')
			{
				if (!token.empty())
				{
					normalizedTokens.push_back(NormalizeOverrideToken(token));
					token.clear();
				}
			}
			else
			{
				token.push_back(ch);
			}
		}
		if (!token.empty())
		{
			normalizedTokens.push_back(NormalizeOverrideToken(token));
		}

		for (const auto& tokenNorm : normalizedTokens)
		{
			if (tokenNorm.empty())
				continue;

			if (tokenNorm == normalizedLabel)
				return true;

			if (tokenNorm.find(normalizedLabel) != std::string::npos ||
				normalizedLabel.find(tokenNorm) != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	void Inspector::AddInlinePrefabOverrideMarkers(
		HexEngine::ComponentWidget* widget,
		const std::string& componentName,
		const std::vector<PrefabController::PrefabPropertyOverride>& prefabOverrides)
	{
		if (widget == nullptr || prefabOverrides.empty())
			return;

		std::vector<PrefabController::PrefabPropertyOverride> componentOverrides;
		for (const auto& overrideEntry : prefabOverrides)
		{
			if (overrideEntry.componentName == componentName)
			{
				componentOverrides.push_back(overrideEntry);
			}
		}

		if (componentOverrides.empty())
			return;

		std::vector<HexEngine::Element*> fieldChildren = widget->GetChildren();
		for (auto* child : fieldChildren)
		{
			if (child == nullptr || child->GetPosition().y < 20)
				continue;

			const std::string& boundComponent = child->GetPrefabOverrideComponentName();
			const std::string& boundPath = child->GetPrefabOverridePath();

			auto matchIt = componentOverrides.end();
			if (!boundPath.empty() &&
				(boundComponent.empty() || boundComponent == componentName))
			{
				matchIt = std::find_if(componentOverrides.begin(), componentOverrides.end(),
					[&](const PrefabController::PrefabPropertyOverride& overrideEntry)
					{
						return IsOverridePathMatchOrParent(overrideEntry.path, boundPath);
					});
			}

			if (matchIt == componentOverrides.end())
			{
				const std::wstring label = child->GetLabelText();
				if (label.empty())
					continue;

				matchIt = std::find_if(componentOverrides.begin(), componentOverrides.end(),
					[&](const PrefabController::PrefabPropertyOverride& overrideEntry)
					{
						return IsOverridePathMatchingLabel(overrideEntry.path, label);
					});
			}
			if (matchIt == componentOverrides.end())
				continue;

			const int32_t rowY = child->GetPosition().y;
			const int32_t rowH = child->GetSize().y;
			const int32_t markerW = 42;
			const int32_t revertW = 48;
			const int32_t margin = 6;
			const int32_t markerX = std::max(8, widget->GetSize().x - markerW - revertW - margin - 8);
			const int32_t revertX = std::max(8, widget->GetSize().x - revertW - 8);

			auto* marker = new HexEngine::Button(
				widget,
				HexEngine::Point(markerX, rowY),
				HexEngine::Point(markerW, rowH),
				L"OVR",
				[](HexEngine::Button*) { return true; });
			widget->RemoveComponentChild(marker);
			marker->EnableInput(false);
			marker->SetContributesToParentAutoLayout(false);
			marker->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(80, 140, 90, 255)));

			auto* revertBtn = new HexEngine::Button(
				widget,
				HexEngine::Point(revertX, rowY),
				HexEngine::Point(revertW, rowH),
				L"Revert",
				[this, componentName, propertyPath = matchIt->path](HexEngine::Button*)
				{
					return OnRevertPrefabPropertyOverride(componentName, propertyPath);
				});
			widget->RemoveComponentChild(revertBtn);
			revertBtn->SetContributesToParentAutoLayout(false);
			revertBtn->SetHighlightOverride(math::Color(HEX_RGBA_TO_FLOAT4(36, 122, 74, 255)));
		}
	}

	void Inspector::Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		if (_pendingForcedRefresh && _pendingRefreshEntity != nullptr && _pendingRefreshEntity == _inspecting)
		{
			auto* uiRoot = HexEngine::g_pEnv->GetUIManager().GetRootElement();
			auto* focused = FindFocusedElementRecursive(uiRoot);
			if (!HasBlockingExternalDialogOpen() &&
				(focused == nullptr || IsDescendantOfElement(focused, this)))
			{
				_pendingForcedRefresh = false;
				_pendingRefreshEntity = nullptr;
				InspectEntity(_inspecting, true);
			}
		}

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
