#include "AnimationTimeline.hpp"
#include "AnimationEditor.hpp"

namespace HexEditor
{
	using namespace HexEngine;
	using AnimationUtils::Track;

	namespace
	{
		bool ShiftDown() { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; }

		const wchar_t* TrackName(Track track)
		{
			switch (track)
			{
			case Track::Position: return L"Position";
			case Track::Rotation: return L"Rotation";
			default: return L"Scale";
			}
		}

		template<typename T>
		void AppendTimes(const std::vector<std::pair<float, T>>& keys, std::vector<float>& out)
		{
			for (const auto& key : keys)
				out.push_back(key.first);
		}

		void UniqueTimes(std::vector<float>& times)
		{
			std::sort(times.begin(), times.end());
			times.erase(std::unique(times.begin(), times.end(),
				[](float a, float b) { return std::abs(a - b) <= AnimationUtils::kKeyTimeEpsilon; }), times.end());
		}

		std::vector<float> TrackTimes(const AnimChannel& channel, Track track)
		{
			std::vector<float> times;
			switch (track)
			{
			case Track::Position: AppendTimes(channel.positionKeys, times); break;
			case Track::Rotation: AppendTimes(channel.rotationKeys, times); break;
			case Track::Scale: AppendTimes(channel.scaleKeys, times); break;
			}
			return times;
		}

		math::Vector3 EulerDegrees(const math::Quaternion& q)
		{
			const math::Vector3 e = q.ToEuler();
			return math::Vector3(ToDegree(e.x), ToDegree(e.y), ToDegree(e.z));
		}

		// Shifts `value` by whole turns to land as close as possible to `reference`.
		float Unwrap(float value, float reference)
		{
			while (value - reference > 180.0f) value -= 360.0f;
			while (value - reference < -180.0f) value += 360.0f;
			return value;
		}
	}

	AnimationTimeline::AnimationTimeline(Element* parent, const Point& position, const Point& size, AnimationEditor* editor) :
		Element(parent, position, size),
		_editor(editor)
	{
	}

	void AnimationTimeline::SetMode(Mode mode)
	{
		_mode = mode;
		_drag = DragKind::None;
	}

	void AnimationTimeline::FitView()
	{
		const float length = std::max(1.0f, _editor->GetClipLength());
		const float margin = length * 0.03f;
		_viewStart = -margin;
		_viewEnd = length + margin;
		_fittedClip = _editor->Doc().GetClipIndex();
	}

	// ---- mapping -----------------------------------------------------------------------

	int32_t AnimationTimeline::GraphLeft() const { return GetAbsolutePosition().x + kLabelWidth; }
	int32_t AnimationTimeline::GraphRight() const { return GetAbsolutePosition().x + _size.x - 6; }
	int32_t AnimationTimeline::RowsTop() const { return GetAbsolutePosition().y + kRulerHeight; }

	int32_t AnimationTimeline::TimeToX(float ticks) const
	{
		const float span = std::max(1e-3f, _viewEnd - _viewStart);
		return GraphLeft() + (int32_t)std::round((ticks - _viewStart) / span * (float)(GraphRight() - GraphLeft()));
	}

	float AnimationTimeline::XToTime(int32_t x) const
	{
		const float width = (float)std::max(1, GraphRight() - GraphLeft());
		return _viewStart + (float)(x - GraphLeft()) / width * (_viewEnd - _viewStart);
	}

	float AnimationTimeline::Snap(float ticks) const
	{
		return ShiftDown() ? ticks : std::round(ticks);
	}

	// ---- dopesheet data ------------------------------------------------------------------

	int32_t AnimationTimeline::RowCount() const
	{
		return _editor->GetSelectedNode().empty() ? 1 : 4;
	}

	void AnimationTimeline::GatherRowKeys(int32_t row, std::vector<RowKey>& out) const
	{
		out.clear();
		const Animation* clip = _editor->Doc().GetClip();
		if (clip == nullptr)
			return;

		auto& selection = _editor->SelectedKeys();
		std::vector<float> times;

		if (row == 0)
		{
			for (const auto& channel : clip->channels)
			{
				AppendTimes(channel.positionKeys, times);
				AppendTimes(channel.rotationKeys, times);
				AppendTimes(channel.scaleKeys, times);
			}
			UniqueTimes(times);

			std::vector<float> selectedTimes;
			selectedTimes.reserve(selection.size());
			for (const auto& key : selection)
				selectedTimes.push_back(key.time);
			UniqueTimes(selectedTimes);

			for (float t : times)
			{
				const auto it = std::lower_bound(selectedTimes.begin(), selectedTimes.end(), t - AnimationUtils::kKeyTimeEpsilon);
				const bool selected = it != selectedTimes.end() && std::abs(*it - t) <= AnimationUtils::kKeyTimeEpsilon;
				out.push_back({ t, selected });
			}
			return;
		}

		const AnimChannel* channel = AnimationUtils::FindChannel(*clip, _editor->GetSelectedNode());
		if (channel == nullptr)
			return;

		const Track track = (Track)(row - 1);
		for (float t : TrackTimes(*channel, track))
			out.push_back({ t, selection.count(AnimKeyRef{ channel->nodeName, track, t }) > 0 });
	}

	void AnimationTimeline::KeysAt(int32_t row, float time, std::vector<AnimKeyRef>& out) const
	{
		out.clear();
		const Animation* clip = _editor->Doc().GetClip();
		if (clip == nullptr)
			return;

		const auto addChannel = [&](const AnimChannel& channel, Track track)
		{
			const auto times = TrackTimes(channel, track);
			for (float t : times)
			{
				if (std::abs(t - time) <= AnimationUtils::kKeyTimeEpsilon)
					out.push_back({ channel.nodeName, track, t });
			}
		};

		if (row == 0)
		{
			for (const auto& channel : clip->channels)
			{
				addChannel(channel, Track::Position);
				addChannel(channel, Track::Rotation);
				addChannel(channel, Track::Scale);
			}
		}
		else if (const AnimChannel* channel = AnimationUtils::FindChannel(*clip, _editor->GetSelectedNode()); channel != nullptr)
		{
			addChannel(*channel, (Track)(row - 1));
		}
	}

	bool AnimationTimeline::HitKey(int32_t mx, int32_t my, int32_t& outRow, float& outTime) const
	{
		const int32_t row = (my - RowsTop()) / kRowHeight;
		if (my < RowsTop() || row < 0 || row >= RowCount())
			return false;

		std::vector<RowKey> keys;
		GatherRowKeys(row, keys);
		int32_t bestDistance = 6;
		bool found = false;
		for (const auto& key : keys)
		{
			const int32_t distance = std::abs(TimeToX(key.time) - mx);
			if (distance <= bestDistance)
			{
				bestDistance = distance;
				outTime = key.time;
				found = true;
			}
		}
		outRow = row;
		return found;
	}

	void AnimationTimeline::SelectBox()
	{
		auto& selection = _editor->SelectedKeys();
		const int32_t x0 = std::min(_dragStart.x, _dragLast.x), x1 = std::max(_dragStart.x, _dragLast.x);
		const int32_t y0 = std::min(_dragStart.y, _dragLast.y), y1 = std::max(_dragStart.y, _dragLast.y);

		std::vector<RowKey> keys;
		std::vector<AnimKeyRef> refs;
		for (int32_t row = 0; row < RowCount(); ++row)
		{
			const int32_t rowY = RowsTop() + row * kRowHeight + kRowHeight / 2;
			if (rowY < y0 || rowY > y1)
				continue;

			GatherRowKeys(row, keys);
			for (const auto& key : keys)
			{
				const int32_t x = TimeToX(key.time);
				if (x < x0 || x > x1)
					continue;
				KeysAt(row, key.time, refs);
				selection.insert(refs.begin(), refs.end());
			}
		}
	}

	// ---- curves data ---------------------------------------------------------------------

	math::Vector3 AnimationTimeline::TrackValue(Track track, const AnimationUtils::NodePose& pose)
	{
		switch (track)
		{
		case Track::Position: return pose.translation;
		case Track::Rotation: return EulerDegrees(pose.rotation);
		default: return pose.scale;
		}
	}

	float AnimationTimeline::CurveValueToY(float value) const
	{
		const int32_t top = RowsTop() + 6;
		const int32_t bottom = GetAbsolutePosition().y + _size.y - 6;
		const float span = std::max(1e-4f, _curveMax - _curveMin);
		return (float)bottom - (value - _curveMin) / span * (float)(bottom - top);
	}

	float AnimationTimeline::CurveYToValue(int32_t y) const
	{
		const int32_t top = RowsTop() + 6;
		const int32_t bottom = GetAbsolutePosition().y + _size.y - 6;
		return _curveMin + (float)(bottom - y) / (float)std::max(1, bottom - top) * (_curveMax - _curveMin);
	}

	void AnimationTimeline::GatherCurve(std::vector<std::vector<math::Vector2>>& lines, std::vector<CurvePoint>& points)
	{
		lines.assign(3, {});
		points.clear();

		const Animation* clip = _editor->Doc().GetClip();
		const AnimChannel* channel = clip ? AnimationUtils::FindChannel(*clip, _editor->GetSelectedNode()) : nullptr;
		if (channel == nullptr)
			return;

		// Sample the curve every few pixels, unwrapping Euler angles so a rotation
		// passing +-180 degrees draws as a continuous line.
		std::vector<std::pair<float, math::Vector3>> samples;
		math::Vector3 previous;
		for (int32_t x = GraphLeft(); x <= GraphRight(); x += 3)
		{
			const float t = XToTime(x);
			math::Vector3 v = TrackValue(_curveTrack, AnimationUtils::SampleChannel(*channel, std::max(0.0f, t)));
			if (_curveTrack == Track::Rotation && !samples.empty())
			{
				v.x = Unwrap(v.x, previous.x);
				v.y = Unwrap(v.y, previous.y);
				v.z = Unwrap(v.z, previous.z);
			}
			previous = v;
			samples.push_back({ t, v });
		}

		const auto sampleAt = [&](float t) -> math::Vector3
		{
			const float width = (float)std::max(1, GraphRight() - GraphLeft());
			const size_t index = (size_t)std::clamp((t - _viewStart) / std::max(1e-3f, _viewEnd - _viewStart) * width / 3.0f, 0.0f, (float)samples.size() - 1.0f);
			return samples.empty() ? math::Vector3::Zero : samples[index].second;
		};

		const auto addPoint = [&](float t, const math::Vector3& rawValue)
		{
			math::Vector3 v = rawValue;
			if (_curveTrack == Track::Rotation)
			{
				const math::Vector3 reference = sampleAt(t);
				v.x = Unwrap(v.x, reference.x);
				v.y = Unwrap(v.y, reference.y);
				v.z = Unwrap(v.z, reference.z);
			}
			for (int32_t c = 0; c < 3; ++c)
				points.push_back({ AnimKeyRef{ channel->nodeName, _curveTrack, t }, c, (&v.x)[c] });
		};

		switch (_curveTrack)
		{
		case Track::Position: for (const auto& k : channel->positionKeys) addPoint(k.first, k.second); break;
		case Track::Rotation: for (const auto& k : channel->rotationKeys) addPoint(k.first, EulerDegrees(k.second)); break;
		case Track::Scale: for (const auto& k : channel->scaleKeys) addPoint(k.first, k.second); break;
		}

		// Vertical range: fit everything (frozen while dragging so the curve doesn't
		// slide out from under the cursor).
		if (_drag != DragKind::CurveValue)
		{
			float lo = FLT_MAX, hi = -FLT_MAX;
			for (const auto& s : samples)
			{
				lo = std::min({ lo, s.second.x, s.second.y, s.second.z });
				hi = std::max({ hi, s.second.x, s.second.y, s.second.z });
			}
			for (const auto& p : points)
			{
				lo = std::min(lo, p.value);
				hi = std::max(hi, p.value);
			}
			if (lo > hi) { lo = -1.0f; hi = 1.0f; }
			const float pad = std::max(0.05f, (hi - lo) * 0.1f);
			_curveMin = lo - pad;
			_curveMax = hi + pad;
		}

		for (const auto& s : samples)
		{
			const float x = (float)TimeToX(s.first);
			for (int32_t c = 0; c < 3; ++c)
				lines[c].push_back(math::Vector2(x, CurveValueToY((&s.second.x)[c])));
		}
	}

	// ---- rendering -------------------------------------------------------------------------

	void AnimationTimeline::RenderRuler(GuiRenderer* renderer)
	{
		const auto abs = GetAbsolutePosition();
		renderer->FillQuad(abs.x, abs.y, _size.x, kRulerHeight, AnimEditorColours::Panel);

		// Tick spacing: a power-of-ten-ish step that keeps labels ~60px apart.
		const float pixelsPerTick = (float)(GraphRight() - GraphLeft()) / std::max(1e-3f, _viewEnd - _viewStart);
		float step = 1.0f;
		const float candidates[] = { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
		for (float c : candidates)
		{
			step = c;
			if (c * pixelsPerTick >= 60.0f)
				break;
		}

		const float first = std::ceil(_viewStart / step) * step;
		for (float t = first; t <= _viewEnd; t += step)
		{
			const int32_t x = TimeToX(t);
			if (x < GraphLeft() || x > GraphRight())
				continue;
			renderer->FillQuad(x, abs.y + kRulerHeight - 7, 1, 7, AnimEditorColours::TextDim);
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, x + 3, abs.y + 8, AnimEditorColours::TextDim, FontAlign::CentreUD, std::format(L"{}", (int32_t)t));
		}

		// Clip end.
		const int32_t endX = TimeToX(_editor->GetClipLength());
		if (endX >= GraphLeft() && endX <= GraphRight())
			renderer->FillQuad(endX, abs.y, 2, _size.y, math::Color(1.0f, 1.0f, 1.0f, 0.25f));

		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + 6, abs.y + kRulerHeight / 2, AnimEditorColours::Text, FontAlign::CentreUD,
			_mode == Mode::Dopesheet ? L"Dopesheet" : std::wstring(L"Curves: ") + TrackName(_curveTrack));
	}

	void AnimationTimeline::RenderDopesheet(GuiRenderer* renderer)
	{
		const auto abs = GetAbsolutePosition();
		std::vector<RowKey> keys;
		const std::string& selectedNode = _editor->GetSelectedNode();

		for (int32_t row = 0; row < RowCount(); ++row)
		{
			const int32_t y = RowsTop() + row * kRowHeight;
			renderer->FillQuad(abs.x, y, _size.x, kRowHeight, (row % 2) ? AnimEditorColours::RowAlt : AnimEditorColours::Row);

			const std::wstring label = row == 0 ? L"All keys" : TrackName((Track)(row - 1));
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + (row == 0 ? 6 : 14), y + kRowHeight / 2,
				row == 0 ? AnimEditorColours::Text : AnimEditorColours::TextDim, FontAlign::CentreUD, label);

			GatherRowKeys(row, keys);
			for (const auto& key : keys)
			{
				float t = key.time;
				if (key.selected && _drag == DragKind::MoveKeys)
					t = std::max(0.0f, t + _dragDeltaTicks);

				const int32_t x = TimeToX(t);
				if (x < GraphLeft() - 4 || x > GraphRight() + 4)
					continue;

				const int32_t half = row == 0 ? 4 : 3;
				renderer->FillQuad(x - half, y + kRowHeight / 2 - half, half * 2 + 1, half * 2 + 1, key.selected ? AnimEditorColours::KeySelected : AnimEditorColours::Key);
			}
		}

		if (!selectedNode.empty())
		{
			const int32_t y = RowsTop() + RowCount() * kRowHeight + 4;
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + 6, y + 6, AnimEditorColours::TextDim, FontAlign::CentreUD,
				L"Bone: " + s2ws(selectedNode) + (_editor->HasPoseOverride(selectedNode) ? L"   (unkeyed changes - press K to key)" : L""));
		}
		else
		{
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + 6, RowsTop() + kRowHeight + 10, AnimEditorColours::TextDim, FontAlign::CentreUD,
				L"Select a bone (viewport or hierarchy) to see and edit its tracks.");
		}

		if (_drag == DragKind::BoxSelect)
		{
			const int32_t x0 = std::min(_dragStart.x, _dragLast.x), y0 = std::min(_dragStart.y, _dragLast.y);
			renderer->FillQuad(x0, y0, std::abs(_dragLast.x - _dragStart.x), std::abs(_dragLast.y - _dragStart.y), math::Color(0.3f, 0.5f, 0.9f, 0.18f));
			renderer->Frame(x0, y0, std::abs(_dragLast.x - _dragStart.x), std::abs(_dragLast.y - _dragStart.y), 1, AnimEditorColours::Selection);
		}
	}

	void AnimationTimeline::RenderCurves(GuiRenderer* renderer)
	{
		const auto abs = GetAbsolutePosition();
		renderer->FillQuad(abs.x, RowsTop(), _size.x, _size.y - kRulerHeight, AnimEditorColours::Row);

		if (_editor->GetSelectedNode().empty())
		{
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + 6, RowsTop() + 14, AnimEditorColours::TextDim, FontAlign::CentreUD,
				L"Select a bone to see its curves.");
			return;
		}

		std::vector<std::vector<math::Vector2>> lines;
		GatherCurve(lines, _curvePoints);

		// Zero line and range labels.
		const float zeroY = CurveValueToY(0.0f);
		if (zeroY > RowsTop() && zeroY < abs.y + _size.y)
			renderer->FillQuad(GraphLeft(), (int32_t)zeroY, GraphRight() - GraphLeft(), 1, math::Color(1.0f, 1.0f, 1.0f, 0.12f));
		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + 6, RowsTop() + 10, AnimEditorColours::TextDim, FontAlign::CentreUD, std::format(L"{:.2f}", _curveMax));
		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + 6, abs.y + _size.y - 12, AnimEditorColours::TextDim, FontAlign::CentreUD, std::format(L"{:.2f}", _curveMin));

		const math::Color colours[3] = { AnimEditorColours::AxisX, AnimEditorColours::AxisY, AnimEditorColours::AxisZ };
		const wchar_t* names[3] = { L"X", L"Y", L"Z" };
		for (int32_t c = 0; c < 3; ++c)
		{
			for (size_t i = 1; i < lines[c].size(); ++i)
				renderer->Line((int32_t)lines[c][i - 1].x, (int32_t)lines[c][i - 1].y, (int32_t)lines[c][i].x, (int32_t)lines[c][i].y, colours[c]);
			renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + 6 + c * 18, RowsTop() + 30, colours[c], FontAlign::CentreUD, names[c]);
		}

		auto& selection = _editor->SelectedKeys();
		for (const auto& point : _curvePoints)
		{
			const int32_t x = TimeToX(point.key.time);
			if (x < GraphLeft() - 4 || x > GraphRight() + 4)
				continue;
			const bool selected = selection.count(point.key) > 0;
			const int32_t y = (int32_t)CurveValueToY(point.value);
			renderer->FillQuad(x - 3, y - 3, 7, 7, selected ? AnimEditorColours::KeySelected : colours[point.component]);
		}
	}

	void AnimationTimeline::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const auto abs = GetAbsolutePosition();

		if (_fittedClip != _editor->Doc().GetClipIndex())
			FitView();

		renderer->FillQuad(abs.x, abs.y, _size.x, _size.y, AnimEditorColours::Background);
		RenderRuler(renderer);

		if (_mode == Mode::Dopesheet)
			RenderDopesheet(renderer);
		else
			RenderCurves(renderer);

		// Playhead
		const int32_t x = TimeToX(_editor->GetTime());
		if (x >= GraphLeft() && x <= GraphRight())
		{
			renderer->FillQuad(x, abs.y, 1, _size.y, AnimEditorColours::Playhead);
			renderer->FillQuad(x - 5, abs.y, 11, 8, AnimEditorColours::Playhead);
		}

		renderer->FillQuad(GraphLeft() - 1, abs.y, 1, _size.y, AnimEditorColours::PanelBorder);
		renderer->Frame(abs.x, abs.y, _size.x, _size.y, 1, AnimEditorColours::PanelBorder);
	}

	// ---- input ---------------------------------------------------------------------------

	bool AnimationTimeline::OnInputEvent(InputEvent event, InputData* data)
	{
		const auto abs = GetAbsolutePosition();
		const bool over = Element::IsMouseOver(abs.x, abs.y, _size.x, _size.y);

		switch (event)
		{
		case InputEvent::MouseDown:
		{
			const int32_t mx = data->MouseDown.xpos, my = data->MouseDown.ypos;
			if (!over)
				return false;

			g_pEnv->GetUIManager().SetInputFocus(this);
			_dragStart = _dragLast = Point(mx, my);

			if (data->MouseDown.button == VK_MBUTTON)
			{
				_drag = DragKind::Pan;
				return true;
			}
			if (data->MouseDown.button != VK_LBUTTON)
				return true;

			if (my < RowsTop())
			{
				_drag = DragKind::Scrub;
				_editor->SetTime(std::max(0.0f, Snap(XToTime(mx))));
				return true;
			}

			auto& selection = _editor->SelectedKeys();
			const bool ctrl = g_pEnv->_inputSystem->IsCtrlDown();

			if (_mode == Mode::Curves)
			{
				for (const auto& point : _curvePoints)
				{
					if (std::abs(TimeToX(point.key.time) - mx) <= 5 && std::abs((int32_t)CurveValueToY(point.value) - my) <= 5)
					{
						if (!ctrl)
							selection.clear();
						selection.insert(point.key);
						_dragPoint = point;

						const Animation* clip = _editor->Doc().GetClip();
						const AnimChannel* channel = clip ? AnimationUtils::FindChannel(*clip, point.key.node) : nullptr;
						_dragPointValue = math::Vector4::Zero;
						if (channel != nullptr)
						{
							if (point.key.track == Track::Rotation)
							{
								if (const int32_t i = AnimationUtils::FindKeyAt(channel->rotationKeys, point.key.time); i >= 0)
									_dragPointValue = math::Vector4(channel->rotationKeys[i].second);
							}
							else
							{
								const auto& keys = point.key.track == Track::Position ? channel->positionKeys : channel->scaleKeys;
								if (const int32_t i = AnimationUtils::FindKeyAt(keys, point.key.time); i >= 0)
									_dragPointValue = math::Vector4(keys[i].second.x, keys[i].second.y, keys[i].second.z, 0.0f);
							}
						}
						_drag = DragKind::CurveValue;
						return true;
					}
				}
				if (!ctrl)
					selection.clear();
				return true;
			}

			int32_t row = 0;
			float time = 0.0f;
			if (HitKey(mx, my, row, time))
			{
				std::vector<AnimKeyRef> refs;
				KeysAt(row, time, refs);
				const bool alreadySelected = !refs.empty() && selection.count(refs.front()) > 0;

				if (ctrl && alreadySelected)
				{
					for (const auto& ref : refs)
						selection.erase(ref);
					return true;
				}
				if (!ctrl && !alreadySelected)
					selection.clear();
				selection.insert(refs.begin(), refs.end());

				_drag = DragKind::MoveKeys;
				_dragDeltaTicks = 0.0f;
				return true;
			}

			if (!ctrl)
				selection.clear();
			_drag = DragKind::BoxSelect;
			return true;
		}

		case InputEvent::MouseMove:
		{
			if (_drag == DragKind::None)
				return false;

			int32_t mx = 0, my = 0;
			g_pEnv->_inputSystem->GetMousePosition(mx, my);
			const int32_t dx = mx - _dragLast.x;
			_dragLast = Point(mx, my);

			switch (_drag)
			{
			case DragKind::Scrub:
				_editor->SetTime(std::clamp(Snap(XToTime(mx)), 0.0f, std::max(_editor->GetClipLength(), XToTime(GraphRight()))));
				break;

			case DragKind::Pan:
			{
				const float ticksPerPixel = (_viewEnd - _viewStart) / (float)std::max(1, GraphRight() - GraphLeft());
				_viewStart -= dx * ticksPerPixel;
				_viewEnd -= dx * ticksPerPixel;
				break;
			}

			case DragKind::MoveKeys:
			{
				const float ticksPerPixel = (_viewEnd - _viewStart) / (float)std::max(1, GraphRight() - GraphLeft());
				_dragDeltaTicks = Snap((float)(mx - _dragStart.x) * ticksPerPixel);
				break;
			}

			case DragKind::CurveValue:
			{
				const float delta = CurveYToValue(my) - CurveYToValue(_dragStart.y);
				math::Vector4 value = _dragPointValue;
				if (_dragPoint.key.track == Track::Rotation)
				{
					math::Vector3 euler = math::Quaternion(_dragPointValue).ToEuler();
					(&euler.x)[_dragPoint.component] += ToRadian(delta);
					value = math::Vector4(math::Quaternion::CreateFromYawPitchRoll(euler));
				}
				else
				{
					(&value.x)[_dragPoint.component] += delta;
				}
				_editor->SetKeyValue(_dragPoint.key, value, true);
				break;
			}

			default:
				break;
			}
			return true;
		}

		case InputEvent::MouseUp:
		{
			if (_drag == DragKind::None)
				return false;

			const DragKind finished = _drag;
			_drag = DragKind::None;

			if (finished == DragKind::MoveKeys && std::abs(_dragDeltaTicks) > 1e-4f)
				_editor->MoveSelectedKeys(_dragDeltaTicks);
			else if (finished == DragKind::BoxSelect)
				SelectBox();
			else if (finished == DragKind::CurveValue)
				_editor->Doc().EndEdit();

			_dragDeltaTicks = 0.0f;
			return true;
		}

		case InputEvent::MouseWheel:
		{
			if (!over)
				return false;

			int32_t mx = 0, my = 0;
			g_pEnv->_inputSystem->GetMousePosition(mx, my);
			const float pivot = XToTime(std::clamp(mx, GraphLeft(), GraphRight()));
			const float factor = data->MouseWheel.delta > 0 ? 0.85f : 1.0f / 0.85f;
			_viewStart = pivot + (_viewStart - pivot) * factor;
			_viewEnd = pivot + (_viewEnd - pivot) * factor;
			if (_viewEnd - _viewStart < 4.0f)
				_viewEnd = _viewStart + 4.0f;
			return true;
		}

		default:
			return false;
		}
	}
}
