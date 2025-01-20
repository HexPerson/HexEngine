
#pragma once

#include "../../Required.hpp"
#include "Point.hpp"
#include "ContextMenu.hpp"

namespace HexEngine
{
	class ITexture2D;
	class GuiRenderer;
	class TreeList;
	class Scene;

	inline const int32_t TreeListLineHeight = 20;

	class ListNode
	{
		friend class TreeList;

	public:
		using OnClickNode = std::function<void(ListNode*, int32_t mouseButton)>;
		using OnDragAndDropNode = std::function<void(ListNode* dragSource, ListNode* dropTarget)>;

		ListNode(TreeList* list, const std::wstring& label, const std::vector<ITexture2D*>& icons, void* userData=nullptr);

		virtual const std::wstring& GetLabel() const;
		virtual int32_t				GetHeight() const;
		virtual void				Render(GuiRenderer* renderer, const Point& position, const Point& size);
		virtual void				OnClick(int32_t button, int32_t x, int32_t y);
		virtual void				OnDragAndDrop(ListNode* target);
		virtual ITexture2D*			GetIcon() const;


		void						SetIcon(uint32_t iconId);
		ListNode*					GetParent() const;
		void						SetLabel(const std::wstring& label);

	protected:
		TreeList* _list;
		std::wstring _label;
		uint32_t _iconId = 0;
		std::vector<ITexture2D*> _icons;
		//ITexture2D* _openIcon = nullptr;
		//ITexture2D* _closeIcon = nullptr;
		std::shared_ptr<ITexture2D> _arrowIcon;
		ListNode* _child = nullptr;
		ListNode* _parent = nullptr;
		bool _isOpen = false;
		std::vector<ListNode*> _items;
		void* _objectPtr = nullptr;

	public:
		OnClickNode _onClick;
		OnDragAndDropNode _onDragAndDrop;
		void* _userData;
	};

	class SceneListNode : public ListNode
	{
	public:
		SceneListNode(TreeList* list, const std::wstring& label, const std::vector<ITexture2D*>& icons, Scene* scene) :
			ListNode(list, label, icons),
			_scene(scene)
		{}

		virtual void OnClick(int32_t button, int32_t x, int32_t y) override;

		Scene* GetScene() const {
			return _scene;
		}

	protected:
		Scene* _scene;
	};
}
