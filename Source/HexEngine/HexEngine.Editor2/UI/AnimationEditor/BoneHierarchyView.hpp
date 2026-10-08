#pragma once

#include <HexEngine.Core/HexEngine.hpp>

namespace HexEditor
{
	class AnimationEditor;

	// Left panel of the animation editor: the clip's node tree, indented by depth.
	// Skinning bones are bright, plain scene nodes dim; a dot marks nodes keyed in
	// the current clip, an orange dot unkeyed pose changes. Click selects; wheel scrolls.
	class BoneHierarchyView : public HexEngine::Element
	{
	public:
		BoneHierarchyView(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size, AnimationEditor* editor);

		void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		void ScrollToSelection();

	private:
		static constexpr int32_t kHeaderHeight = 22;
		static constexpr int32_t kRowHeight = 18;

		int32_t VisibleRows() const;
		int32_t RowAt(int32_t my) const;

		AnimationEditor* _editor = nullptr;
		int32_t _scroll = 0;
		bool _bonesOnly = false;
		std::vector<int32_t> _rows;	// node indices shown, in order
	};
}
