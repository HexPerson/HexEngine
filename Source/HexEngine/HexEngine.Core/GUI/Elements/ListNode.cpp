
#include "ListNode.hpp"
#include "TreeList.hpp"
#include "../GuiRenderer.hpp"

namespace HexEngine
{
	ListNode::ListNode(TreeList* list, const std::wstring& label, const std::vector<ITexture2D*>& icons, void* userData) :
		_list(list),
		_label(label),
		_icons(icons),
		_userData(userData)
	{
		_arrowIcon = ITexture2D::Create("EngineData.Textures/UI/triangle.png");

		_id = list->GetCurrentNodeId();
	}

	const std::wstring&	ListNode::GetLabel() const
	{ 
		return _label;
	}

	int32_t ListNode::GetHeight() const
	{
		return TreeListLineHeight;
	}

	void ListNode::Render(GuiRenderer* renderer, const Point& position, const Point& size)
	{
		if (_child)
		{
			const int32_t arrowX = _list->GetAbsolutePosition().x + _list->GetSize().x - TreeListLineHeight * 2;
			renderer->FillTexturedQuad(_arrowIcon.get(), arrowX, position.y, TreeListLineHeight, TreeListLineHeight, math::Color(HEX_RGBA_TO_FLOAT4(22, 23, 255, 255)), _isOpen ? 0.0f : 90.0f);
		}
		
		renderer->FillTexturedQuad(GetIcon(), position.x + 4, position.y + 1, 16, 16, math::Color(1, 1, 1, 1));

		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, position.x + 24, position.y + TreeListLineHeight / 2, renderer->_style.text_regular, FontAlign::CentreUD, GetLabel());
	}

	void ListNode::OnClick(int32_t button, int32_t x, int32_t y)
	{
		if (_onClick)
		{
			_onClick(this, button);
		}
	}

	void ListNode::OnDragAndDrop(ListNode* target)
	{
		if (_onDragAndDrop)
		{
			_onDragAndDrop(this, target);
		}
	}

	ITexture2D* ListNode::GetIcon() const
	{
		return _icons.at(_iconId);
	}

	void ListNode::SetIcon(uint32_t iconId)
	{
		_iconId = iconId;
	}

	ListNode* ListNode::GetParent() const
	{
		return _parent;
	}

	void ListNode::SetLabel(const std::wstring& label)
	{
		_label = label;
	}

	void SceneListNode::OnClick(int32_t button, int32_t x, int32_t y)
	{
		if (button == VK_RBUTTON)
		{
			//ContextMenu* ctx = new ContextMenu(_list, Point(x, y).RelativeTo(_list->GetAbsolutePosition()));

			//ctx->AddItem({ L"Load Scene", nullptr/*std::bind(&EntityList::DuplicateEntity, this, entity)*/ });
			//ctx->AddItem({ L"Save as prefab...", nullptr/*std::bind(&EntityList::SaveAsPrefab, this, entity, g_pEnv->_fileSystem)*/ });
		}

		ListNode::OnClick(button, x, y);
	}
}
