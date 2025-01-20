
#pragma once

#include "LineEdit.hpp"
#include "ContextMenu.hpp"

namespace HexEngine
{
	class DropDown : public LineEdit
	{
	public:
		DropDown(Element* parent, const Point& position, const Point& size, const std::wstring& label);
		~DropDown();

		ContextMenu* GetContextMenu() const;

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;;

	private:
		void OnSelectedItem(ContextItem* item);

	private:
		ContextMenu* _context;
		bool _hasAdjustedContextPosition = false;
		std::shared_ptr<ITexture2D> _arrowIcon;
	};
}
