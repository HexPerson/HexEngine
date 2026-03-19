#pragma once

#include "Element.hpp"

namespace HexEngine
{
	/**
	 * @brief Scrollable container for editor UI content.
	 *
	 * Child elements should be parented to the content root returned by
	 * GetContentRoot(). The control captures its subtree into a Canvas and
	 * presents only the viewport rectangle to clip overflowing content.
	 */
	class HEX_API ScrollView : public Element
	{
	public:
		ScrollView(Element* parent, const Point& position, const Point& size, const std::wstring& label = L"");
		virtual ~ScrollView() = default;

		Element* GetContentRoot() const;

		void SetScrollOffset(float offset);
		float GetScrollOffset() const;

		void SetScrollSpeed(float speed);
		float GetScrollSpeed() const;

		void SetManualContentHeight(int32_t height);
		int32_t GetManualContentHeight() const;
		int32_t GetContentHeight() const;

		virtual void OnAddChild(Element* child) override;
		virtual void PreRender(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual void PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		virtual bool OnInputEvent(InputEvent event, InputData* data) override;

	private:
		RECT GetViewportRect() const;
		int32_t ComputeContentHeight() const;
		float ComputeMaxScroll() const;
		RECT ComputeScrollbarRect() const;
		void UpdateScrollbarDrag(int32_t mouseY);

	private:
		std::wstring _label;
		Element* _contentRoot = nullptr;
		float _scrollOffset = 0.0f;
		float _scrollSpeed = 28.0f;
		int32_t _manualContentHeight = 0;
		bool _isCapturing = false;
		bool _isDraggingScrollbar = false;
		int32_t _scrollbarDragStartY = 0;
		float _scrollbarDragStartOffset = 0.0f;
	};
}
