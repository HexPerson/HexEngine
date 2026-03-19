
#pragma once

#include "ScrollView.hpp"

namespace HexEngine
{
	class HEX_API ListBox : public ScrollView
	{
	public:
		struct Item
		{
			std::wstring label;
			ITexture2D* icon;
		};

		ListBox(Element* parent, const Point& position, const Point& size);

		virtual ~ListBox();

		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;

		void AddItem(const std::wstring& label, ITexture2D* icon = nullptr);

		std::function<bool(ListBox*, Item*)> OnClickItem;

		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

	private:
		void RenderItems(GuiRenderer* renderer, const Point& position);

	private:
		std::vector<Item> _items;
		ITexture2D* _oldDepthStenchil = nullptr;
		D3D11_VIEWPORT _oldViewport;
		int32_t _hoverIdx = -1;
	};
}
