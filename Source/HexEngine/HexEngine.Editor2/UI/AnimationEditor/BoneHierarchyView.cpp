#include "BoneHierarchyView.hpp"
#include "AnimationEditor.hpp"

namespace HexEditor
{
	using namespace HexEngine;

	BoneHierarchyView::BoneHierarchyView(Element* parent, const Point& position, const Point& size, AnimationEditor* editor) :
		Element(parent, position, size),
		_editor(editor)
	{
	}

	int32_t BoneHierarchyView::VisibleRows() const
	{
		return std::max(1, (_size.y - kHeaderHeight) / kRowHeight);
	}

	int32_t BoneHierarchyView::RowAt(int32_t my) const
	{
		const int32_t local = my - GetAbsolutePosition().y - kHeaderHeight;
		if (local < 0)
			return -1;
		const int32_t row = _scroll + local / kRowHeight;
		return row < (int32_t)_rows.size() ? row : -1;
	}

	void BoneHierarchyView::ScrollToSelection()
	{
		const int32_t selected = _editor->FindNode(_editor->GetSelectedNode());
		for (int32_t row = 0; row < (int32_t)_rows.size(); ++row)
		{
			if (_rows[row] != selected)
				continue;
			if (row < _scroll)
				_scroll = row;
			else if (row >= _scroll + VisibleRows())
				_scroll = row - VisibleRows() + 1;
			return;
		}
	}

	void BoneHierarchyView::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const auto abs = GetAbsolutePosition();
		const auto& nodes = _editor->Nodes();

		_rows.clear();
		for (int32_t i = 0; i < (int32_t)nodes.size(); ++i)
		{
			if (!_bonesOnly || nodes[i].bone >= 0)
				_rows.push_back(i);
		}
		_scroll = std::clamp(_scroll, 0, std::max(0, (int32_t)_rows.size() - VisibleRows()));

		renderer->FillQuad(abs.x, abs.y, _size.x, _size.y, AnimEditorColours::Panel);
		renderer->FillQuad(abs.x, abs.y, _size.x, kHeaderHeight, AnimEditorColours::Row);
		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + 6, abs.y + kHeaderHeight / 2, AnimEditorColours::Text, FontAlign::CentreUD,
			std::format(L"Hierarchy ({} nodes)", nodes.size()));

		// "Bones only" toggle in the header.
		const int32_t toggleX = abs.x + _size.x - 78;
		renderer->FillQuad(toggleX, abs.y + 5, 10, 10, _bonesOnly ? AnimEditorColours::Selection : AnimEditorColours::RowAlt);
		renderer->Frame(toggleX, abs.y + 5, 10, 10, 1, AnimEditorColours::PanelBorder);
		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, toggleX + 14, abs.y + kHeaderHeight / 2, AnimEditorColours::TextDim, FontAlign::CentreUD, L"Bones only");

		const int32_t selected = _editor->FindNode(_editor->GetSelectedNode());
		int32_t mx = 0, my = 0;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);
		const int32_t hovered = Element::IsMouseOver(abs.x, abs.y, _size.x, _size.y) ? RowAt(my) : -1;

		const int32_t visible = VisibleRows();
		for (int32_t r = 0; r < visible && _scroll + r < (int32_t)_rows.size(); ++r)
		{
			const int32_t row = _scroll + r;
			const auto& node = nodes[_rows[row]];
			const int32_t y = abs.y + kHeaderHeight + r * kRowHeight;

			if (_rows[row] == selected)
				renderer->FillQuad(abs.x + 1, y, _size.x - 2, kRowHeight, AnimEditorColours::Selection);
			else if (row == hovered)
				renderer->FillQuad(abs.x + 1, y, _size.x - 2, kRowHeight, AnimEditorColours::Row);

			const int32_t indent = _bonesOnly ? 0 : std::min(node.depth, 24) * 10;
			const int32_t x = abs.x + 8 + indent;

			if (_editor->HasPoseOverride(node.name))
				renderer->FillQuad(x, y + kRowHeight / 2 - 2, 5, 5, AnimEditorColours::Unkeyed);
			else if (node.keyed)
				renderer->FillQuad(x, y + kRowHeight / 2 - 2, 5, 5, AnimEditorColours::Key);

			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, x + 9, y + kRowHeight / 2,
				node.bone >= 0 ? AnimEditorColours::Text : AnimEditorColours::TextDim, FontAlign::CentreUD, s2ws(node.name));
		}

		// Scrollbar
		if ((int32_t)_rows.size() > visible)
		{
			const int32_t trackH = _size.y - kHeaderHeight;
			const int32_t thumbH = std::max(16, trackH * visible / (int32_t)_rows.size());
			const int32_t thumbY = abs.y + kHeaderHeight + (trackH - thumbH) * _scroll / std::max(1, (int32_t)_rows.size() - visible);
			renderer->FillQuad(abs.x + _size.x - 5, thumbY, 4, thumbH, AnimEditorColours::PanelBorder);
		}

		renderer->Frame(abs.x, abs.y, _size.x, _size.y, 1, AnimEditorColours::PanelBorder);
	}

	bool BoneHierarchyView::OnInputEvent(InputEvent event, InputData* data)
	{
		const auto abs = GetAbsolutePosition();

		if (event == InputEvent::MouseWheel && Element::IsMouseOver(abs.x, abs.y, _size.x, _size.y))
		{
			_scroll += data->MouseWheel.delta > 0 ? -3 : 3;
			return true;
		}

		if (event == InputEvent::MouseDown && data->MouseDown.button == VK_LBUTTON &&
			Element::IsMouseOver(abs.x, abs.y, _size.x, _size.y))
		{
			const int32_t mx = data->MouseDown.xpos, my = data->MouseDown.ypos;
			if (my < abs.y + kHeaderHeight)
			{
				if (mx >= abs.x + _size.x - 80)
					_bonesOnly = !_bonesOnly;
				return true;
			}

			if (const int32_t row = RowAt(my); row >= 0)
				_editor->SelectNode(_editor->Nodes()[_rows[row]].name);
			return true;
		}

		return false;
	}
}
