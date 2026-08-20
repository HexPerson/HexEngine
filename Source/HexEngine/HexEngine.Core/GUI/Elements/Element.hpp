
#pragma once

#include "../../Required.hpp"
#include "Style.hpp"
#include "Point.hpp"
#include "../../Input/InputSystem.hpp"
#include "Canvas.hpp"

namespace HexEngine
{
	class GuiRenderer;
	class UIManager;

	class HEX_API Element
	{
	public:
		using OnClickFn = std::function<void(Element*, int32_t, int32_t, int32_t)>;

		Element(Element* parent, const Point& position);
		Element(Element* parent, const Point& position, const Point& size);

		virtual ~Element();

		Element* GetParent() const;

		const Point& GetPosition() const;
		virtual Point GetAbsolutePosition() const;
		const Point& GetSize() const;
		virtual const Point& GetNextPos() const;

		void SetPosition(const Point& position);
		void SetSize(const Point& size);
		void Reparent(Element* newParent, bool preserveAbsolutePosition = true);

		virtual void OnAddChild(Element* child);
		virtual void OnRemoveChild(Element* child);
		
		virtual void Render(GuiRenderer* renderer, uint32_t w, uint32_t h) {}

		const std::vector<Element*>& GetChildren() const;

		bool IsMouseOver(bool absolute = false);
		static bool IsMouseOver(const Point& position, const Point& size);
		static bool IsMouseOver(int32_t x, int32_t y, int32_t w, int32_t h);

		/**
		 * @brief Containers that visually clip their children (ScrollView)
		 * override this to return true. Descendant hit-tests then fail when
		 * the mouse is outside the container's on-screen rect, so scrolled-
		 * away controls stop reacting to a mouse that is really over
		 * whatever UI sits beyond the viewport.
		 */
		virtual bool ClipsInput() const { return false; }
		/**
		 * @brief True when a ClipsInput() ancestor's on-screen rect excludes
		 * the point. The instance IsMouseOver() applies this automatically;
		 * elements hit-testing sub-rects with the STATIC IsMouseOver should
		 * use IsMouseOverClipped instead to get the same behaviour.
		 */
		bool IsMousePointClipped(int32_t mx, int32_t my) const;
		bool IsMouseOverClipped(int32_t x, int32_t y, int32_t w, int32_t h) const;

		virtual bool OnInputEvent(InputEvent event, InputData* data);

		virtual void PreRender(GuiRenderer* renderer, uint32_t w, uint32_t h) {}
		virtual void PostRender(GuiRenderer* renderer, uint32_t w, uint32_t h) {}
		virtual void PostRenderChildren(GuiRenderer* renderer, uint32_t w, uint32_t h) {}

		void BringToFront();
		void SetHasInputFocus(bool focus);
		bool IsInputFocus() const;

		void DeleteMe();
		bool WantsDeletion() const;

		void EnableInput(bool enable);
		bool IsInputEnabled() const;

		virtual void SetLabelMinSize(int32_t minSize);
		int32_t GetLabelMinSize() const;

		void Enable();
		void Disable();
		void EnableRecursive();
		void DisableRecursive();
		bool IsEnabled() const;
		void Toggle();

		virtual int32_t GetLabelWidth() const { return 0; }
		virtual std::wstring GetLabelText() const { return L""; }
		virtual bool ContributesToParentAutoLayout() const { return _contributesToParentLayout; }
		void SetContributesToParentAutoLayout(bool contributes) { _contributesToParentLayout = contributes; }
		void SetPrefabOverrideBinding(const std::string& componentName, const std::string& propertyPath)
		{
			_prefabOverrideComponentName = componentName;
			_prefabOverridePath = propertyPath;
		}
		const std::string& GetPrefabOverrideComponentName() const { return _prefabOverrideComponentName; }
		const std::string& GetPrefabOverridePath() const { return _prefabOverridePath; }

	protected:
		bool _enabled = true;
		Element* _parent = nullptr;
		Point _position;
		Point _size;
		Point _nextPos;
		bool _hasInputFocus = false;

		int32_t _labelMinSize = 0;

		Canvas _canvas;

	protected:
		std::vector<Element*> _children;
		bool _wantsDeletion = false;
		bool _inputEnabled = true;
		bool _contributesToParentLayout = true;
		std::string _prefabOverrideComponentName;
		std::string _prefabOverridePath;

	public:
		//static Style style;
		OnClickFn _onClick;
	};
}
