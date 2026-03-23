
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <HexEngine.Core\GUI\Elements\Button.hpp>
#include "../PrefabController.hpp"

namespace HexEditor
{
	class Inspector : public HexEngine::Dock
	{
	public:
		Inspector(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);

		virtual ~Inspector();

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void InspectEntity(HexEngine::Entity* entity);
		void InspectEntity(HexEngine::Entity* entity, bool forceRebuild);
		void RequestForcedRefresh(HexEngine::Entity* entity);
		void InspectResource(const fs::path& path);

		bool IsInspectingEntity() const;
		bool IsInspectingResource() const;
		HexEngine::Entity* GetInspectingEntity() const;

	private:
		void InspectComponent(HexEngine::Point& pos, HexEngine::BaseComponent* component, HexEngine::GuiRenderer* renderer);
		bool OnAddComponent(HexEngine::Button* button);
		void OnClickAddComponentItem(const std::wstring& name, HexEngine::ComponentId compId);
		bool OnRemoveComponent(HexEngine::BaseComponent* component);
		bool OnRevertVariantComponent(const std::string& componentName);
		bool OnRevertPrefabInstance(HexEngine::Button* button);
		bool OnApplyPrefabInstance(HexEngine::Button* button);
		bool OnOpenPrefabOverrides(HexEngine::Button* button);
		bool OnRevertPrefabPropertyOverride(const std::string& componentName, const std::string& propertyPath);
		bool OnRevertPrefabComponentOverrides(const std::string& componentName);
		bool OnApplySelectedPrefabOverrides(HexEngine::Button* button);
		bool OnConfirmApplyPrefabInstance(HexEngine::Button* button);
		void OnChangeEntityName(const std::wstring& name);
		bool OnDeleteEntity(HexEngine::Button* button);
		bool OnToggleEntityVisible(HexEngine::Button* button);
		bool ReloadPrefabOverrideRows(bool preserveSelection);
		bool ReloadPrefabApplyPreviewRows();
		void RebuildPrefabOverrideDialogContents();
		void RebuildPrefabApplyPreviewDialogContents();
		void ClosePrefabOverrideDialog();
		void ClosePrefabApplyPreviewDialog();
		void AddInlinePrefabOverrideMarkers(HexEngine::ComponentWidget* widget, const std::string& componentName, const std::vector<PrefabController::PrefabPropertyOverride>& prefabOverrides);
		static std::string NormalizeOverrideToken(const std::string& text);
		static std::string NormalizeOverrideToken(const std::wstring& text);
		static bool IsOverridePathMatchOrParent(const std::string& patchPath, const std::string& selectedPath);
		static bool IsOverridePathMatchingLabel(const std::string& path, const std::wstring& label);
		static HexEngine::Element* FindFocusedElementRecursive(HexEngine::Element* root);
		static bool IsDescendantOfElement(const HexEngine::Element* element, const HexEngine::Element* ancestor);
		bool HasBlockingExternalDialogOpen() const;
		void ClearInspectorWidgets();
		virtual void PostRenderChildren(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

	private:
		struct PrefabOverrideRow
		{
			std::string componentName;
			std::string path;
			std::string op;
			bool selected = true;
		};

		HexEngine::Entity* _inspecting = nullptr;
		HexEngine::Button* _addComponentBtn = nullptr;
		HexEngine::Button* _toggleVisibilityBtn = nullptr;
		HexEngine::LineEdit* _entityName = nullptr;
		HexEngine::Button* _deleteBtn = nullptr;
		HexEngine::Button* _revertPrefabBtn = nullptr;
		HexEngine::Button* _applyPrefabBtn = nullptr;
		HexEngine::Button* _prefabOverridesBtn = nullptr;
		HexEngine::TabView* _tabs = nullptr;
		HexEngine::ContextMenu* _addComponentContextMenu = nullptr;
		HexEngine::Dialog* _prefabOverridesDialog = nullptr;
		HexEngine::ScrollView* _prefabOverridesScroll = nullptr;
		std::vector<PrefabOverrideRow> _prefabOverrideRows;
		HexEngine::Dialog* _prefabApplyPreviewDialog = nullptr;
		HexEngine::ScrollView* _prefabApplyPreviewScroll = nullptr;
		std::vector<PrefabOverrideRow> _prefabApplyPreviewRows;
		bool _pendingForcedRefresh = false;
		HexEngine::Entity* _pendingRefreshEntity = nullptr;
		std::vector<HexEngine::ComponentWidget*> _componentWidgets;
		std::shared_ptr<HexEngine::ITexture2D> _visBtnTextures[2];
		//DrawList _drawList;
		//Canvas _canvas;
	};
}
