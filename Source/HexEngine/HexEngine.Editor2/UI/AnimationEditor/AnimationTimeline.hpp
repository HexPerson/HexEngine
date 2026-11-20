#pragma once

#include "AnimationDocument.hpp"

namespace HexEditor
{
	class AnimationEditor;

	// Bottom timeline of the animation editor.
	//
	// Dopesheet: a summary row of every key in the clip plus Position / Rotation /
	// Scale rows for the selected bone. Click or box-select keys (Ctrl adds), drag
	// to move them in time (snapped to whole frames; hold Shift for sub-frame).
	//
	// Curves: the selected bone's position, rotation (Euler degrees) or scale over
	// time; drag a key point vertically to change that component.
	//
	// Both: drag in the ruler to scrub, wheel zooms, middle-drag pans.
	class AnimationTimeline : public HexEngine::Element
	{
	public:
		enum class Mode { Dopesheet, Curves };

		AnimationTimeline(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size, AnimationEditor* editor);

		void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		Mode GetMode() const { return _mode; }
		void SetMode(Mode mode);
		HexEngine::AnimationUtils::Track GetCurveTrack() const { return _curveTrack; }
		void SetCurveTrack(HexEngine::AnimationUtils::Track track) { _curveTrack = track; }
		void FitView();

	private:
		enum class DragKind { None, Scrub, MoveKeys, BoxSelect, Pan, CurveValue };

		struct RowKey
		{
			float time;
			bool selected;
		};

		static constexpr int32_t kRulerHeight = 22;
		static constexpr int32_t kLabelWidth = 112;
		static constexpr int32_t kRowHeight = 20;

		int32_t TimeToX(float ticks) const;
		float XToTime(int32_t x) const;
		float Snap(float ticks) const;
		int32_t GraphLeft() const;
		int32_t GraphRight() const;
		int32_t RowsTop() const;

		// Dopesheet rows: 0 = summary, 1..3 = position/rotation/scale of the selected bone.
		int32_t RowCount() const;
		void GatherRowKeys(int32_t row, std::vector<RowKey>& out) const;
		void KeysAt(int32_t row, float time, std::vector<AnimKeyRef>& out) const;
		bool HitKey(int32_t mx, int32_t my, int32_t& outRow, float& outTime) const;

		// Curves
		struct CurvePoint
		{
			AnimKeyRef key;
			int32_t component;
			float value;
		};
		float CurveValueToY(float value) const;
		float CurveYToValue(int32_t y) const;
		void GatherCurve(std::vector<std::vector<math::Vector2>>& lines, std::vector<CurvePoint>& points);
		static math::Vector3 TrackValue(HexEngine::AnimationUtils::Track track, const HexEngine::AnimationUtils::NodePose& pose);

		void RenderRuler(HexEngine::GuiRenderer* renderer);
		void RenderDopesheet(HexEngine::GuiRenderer* renderer);
		void RenderCurves(HexEngine::GuiRenderer* renderer);
		void SelectBox();

		AnimationEditor* _editor = nullptr;
		Mode _mode = Mode::Dopesheet;
		HexEngine::AnimationUtils::Track _curveTrack = HexEngine::AnimationUtils::Track::Rotation;

		float _viewStart = 0.0f;
		float _viewEnd = 30.0f;
		int32_t _fittedClip = -1;

		DragKind _drag = DragKind::None;
		HexEngine::Point _dragStart;
		HexEngine::Point _dragLast;
		float _dragDeltaTicks = 0.0f;

		// Curves state
		float _curveMin = -1.0f;
		float _curveMax = 1.0f;
		std::vector<CurvePoint> _curvePoints;
		CurvePoint _dragPoint;
		math::Vector4 _dragPointValue;	// the key's full value when the drag started
	};
}
