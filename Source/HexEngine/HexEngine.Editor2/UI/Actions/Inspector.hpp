
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <HexEngine.Core\GUI\Elements\Button.hpp>

namespace HexEditor
{
	class Inspector : public HexEngine::Dock
	{
	public:
		Inspector(Element* parent, const HexEngine::Point& position, const HexEngine::Point& size);

		virtual ~Inspector();

		virtual void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void InspectEntity(HexEngine::Entity* entity);
		void InspectResource(const fs::path& path);

		bool IsInspectingEntity() const;
		bool IsInspectingResource() const;
		HexEngine::Entity* GetInspectingEntity() const;

	private:
		void InspectComponent(HexEngine::Point& pos, HexEngine::BaseComponent* component, HexEngine::GuiRenderer* renderer);
		bool OnAddComponent(HexEngine::Button* button);
		void OnClickAddComponentItem(const std::wstring& name, HexEngine::ComponentId compId);
		void OnChangeEntityName(const std::wstring& name);
		bool OnDeleteEntity(HexEngine::Button* button);
		bool OnToggleEntityVisible(HexEngine::Button* button);
		void ClearInspectorWidgets();
		virtual void PostRenderChildren(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;

	private:
		HexEngine::Entity* _inspecting = nullptr;
		HexEngine::Button* _addComponentBtn = nullptr;
		HexEngine::Button* _toggleVisibilityBtn = nullptr;
		HexEngine::LineEdit* _entityName = nullptr;
		HexEngine::Button* _deleteBtn = nullptr;
		HexEngine::TabView* _tabs = nullptr;
		HexEngine::ContextMenu* _addComponentContextMenu = nullptr;
		std::vector<HexEngine::ComponentWidget*> _componentWidgets;
		std::shared_ptr<HexEngine::ITexture2D> _visBtnTextures[2];
		//DrawList _drawList;
		//Canvas _canvas;
	};
}
