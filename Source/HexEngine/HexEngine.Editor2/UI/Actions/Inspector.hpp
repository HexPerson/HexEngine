
#pragma once

#include <HexEngine.Core\HexEngine.hpp>
#include <HexEngine.Core\GUI\Elements\Button.hpp>

namespace HexEditor
{
	class Inspector : public Dock
	{
	public:
		Inspector(Element* parent, const Point& position, const Point& size);

		virtual ~Inspector();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void InspectEntity(Entity* entity);
		void InspectResource(const fs::path& path);

		bool IsInspectingEntity() const;
		bool IsInspectingResource() const;
		Entity* GetInspectingEntity() const;

	private:
		void InspectComponent(Point& pos, BaseComponent* component, GuiRenderer* renderer);
		bool OnAddComponent(Button* button);
		void OnClickAddComponentItem(const std::wstring& name, ComponentId compId);
		void OnChangeEntityName(const std::wstring& name);
		bool OnDeleteEntity(Button* button);
		bool OnToggleEntityVisible(Button* button);
		void ClearInspectorWidgets();
		virtual void PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

	private:
		Entity* _inspecting = nullptr;
		Button* _addComponentBtn = nullptr;
		Button* _toggleVisibilityBtn = nullptr;
		LineEdit* _entityName = nullptr;
		Button* _deleteBtn = nullptr;
		TabView* _tabs = nullptr;
		ContextMenu* _addComponentContextMenu = nullptr;
		std::vector<ComponentWidget*> _componentWidgets;
		std::shared_ptr<ITexture2D> _visBtnTextures[2];
		//DrawList _drawList;
		//Canvas _canvas;
	};
}
