#include "AnimationViewport.hpp"
#include "AnimationEditor.hpp"

namespace HexEditor
{
	using namespace HexEngine;

	namespace
	{
		constexpr float kRingPixels = 70.0f;
		constexpr float kArrowPixels = 85.0f;
		constexpr float kPickPixels = 7.0f;
		constexpr int32_t kRingSegments = 48;
		constexpr float kNearZ = 0.02f;
		constexpr float kFarZ = 2000.0f;

		const math::Vector3 kAxes[3] = { math::Vector3::UnitX, math::Vector3::UnitY, math::Vector3::UnitZ };

		const math::Color& AxisColour(int32_t axis)
		{
			return axis == 0 ? AnimEditorColours::AxisX : (axis == 1 ? AnimEditorColours::AxisY : AnimEditorColours::AxisZ);
		}

		float DistanceToSegment(const math::Vector2& p, const math::Vector2& a, const math::Vector2& b)
		{
			const math::Vector2 ab = b - a;
			const float lengthSq = ab.LengthSquared();
			const float t = lengthSq > 1e-6f ? std::clamp((p - a).Dot(ab) / lengthSq, 0.0f, 1.0f) : 0.0f;
			return (p - (a + ab * t)).Length();
		}

		float WrapAngle(float radians)
		{
			while (radians > DirectX::XM_PI) radians -= DirectX::XM_2PI;
			while (radians < -DirectX::XM_PI) radians += DirectX::XM_2PI;
			return radians;
		}
	}

	AnimationViewport::AnimationViewport(Element* parent, const Point& position, const Point& size, AnimationEditor* editor) :
		Element(parent, position, size),
		_editor(editor)
	{
		_hdrPresentShader = IShader::Create("EngineData.Shaders/PresentHDR.hcs");
	}

	void AnimationViewport::SetGizmoMode(GizmoMode mode)
	{
		if (_drag == DragKind::Gizmo)
			EndGizmoDrag(false);
		_gizmoMode = mode;
	}

	math::Vector3 AnimationViewport::CameraPosition() const
	{
		const float yaw = ToRadian(_yaw);
		const float pitch = ToRadian(_pitch);
		// Matches Camera's yaw/pitch convention (see IconService framing):
		// yaw = atan2(-dir.x, -dir.z), pitch = asin(dir.y).
		const math::Vector3 dir(-sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch));
		return _target - dir * _distance;
	}

	void AnimationViewport::ApplyCamera(Camera* camera)
	{
		if (camera == nullptr || camera->GetEntity() == nullptr)
			return;

		const float aspect = _size.y > 0 ? (float)_size.x / (float)_size.y : 1.0f;
		camera->SetPespectiveParameters(_fov, aspect, kNearZ, kFarZ);
		camera->GetEntity()->SetPosition(CameraPosition());
		camera->SetYaw(_yaw);
		camera->SetPitch(_pitch);
		camera->SetRoll(0.0f);
		camera->Update(0.0f);
	}

	void AnimationViewport::Frame(const math::Vector3& centre, float radius)
	{
		radius = std::max(0.1f, radius);
		_target = centre;
		_distance = radius / tanf(ToRadian(_fov) * 0.5f) * 1.2f;
	}

	bool AnimationViewport::Project(const math::Vector3& world, math::Vector2& out) const
	{
		const math::Vector4 clip = math::Vector4::Transform(math::Vector4(world.x, world.y, world.z, 1.0f), _viewProj);
		if (clip.w <= 1e-4f)
			return false;

		const auto abs = GetAbsolutePosition();
		out.x = (float)abs.x + (clip.x / clip.w * 0.5f + 0.5f) * (float)_size.x;
		out.y = (float)abs.y + (0.5f - clip.y / clip.w * 0.5f) * (float)_size.y;
		return true;
	}

	float AnimationViewport::PixelsToWorldAt(const math::Vector3& world, float pixels) const
	{
		const float distance = std::max(0.01f, (world - CameraPosition()).Length());
		const float worldPerPixel = 2.0f * distance * tanf(ToRadian(_fov) * 0.5f) / std::max(1, _size.y);
		return pixels * worldPerPixel;
	}

	// ---- picking -------------------------------------------------------------------

	int32_t AnimationViewport::PickJoint(int32_t mx, int32_t my) const
	{
		const auto& nodes = _editor->Nodes();
		const math::Vector2 mouse((float)mx, (float)my);
		int32_t best = -1;
		float bestDistance = kPickPixels;

		for (int32_t i = 0; i < (int32_t)nodes.size(); ++i)
		{
			if (nodes[i].bone < 0)
				continue;

			math::Vector2 screen;
			if (!Project(nodes[i].world.Translation(), screen))
				continue;

			const float distance = (screen - mouse).Length();
			if (distance < bestDistance)
			{
				bestDistance = distance;
				best = i;
			}
		}
		return best;
	}

	void AnimationViewport::GizmoGeometry(const math::Vector3& pivot, int32_t axis, std::vector<math::Vector2>& outLine) const
	{
		outLine.clear();

		if (_gizmoMode == GizmoMode::Translate)
		{
			math::Vector2 a, b;
			if (Project(pivot, a) && Project(pivot + kAxes[axis] * PixelsToWorldAt(pivot, kArrowPixels), b))
			{
				outLine.push_back(a);
				outLine.push_back(b);
			}
			return;
		}

		const float radius = PixelsToWorldAt(pivot, kRingPixels);
		const math::Vector3& u = kAxes[(axis + 1) % 3];
		const math::Vector3& v = kAxes[(axis + 2) % 3];
		for (int32_t i = 0; i <= kRingSegments; ++i)
		{
			const float t = DirectX::XM_2PI * (float)i / (float)kRingSegments;
			math::Vector2 screen;
			if (Project(pivot + (u * cosf(t) + v * sinf(t)) * radius, screen))
				outLine.push_back(screen);
		}
	}

	int32_t AnimationViewport::HoveredGizmoAxis(int32_t mx, int32_t my) const
	{
		const int32_t node = _editor->FindNode(_editor->GetSelectedNode());
		if (node < 0)
			return -1;

		const math::Vector3 pivot = _editor->Nodes()[node].world.Translation();
		const math::Vector2 mouse((float)mx, (float)my);
		std::vector<math::Vector2> line;
		int32_t best = -1;
		float bestDistance = kPickPixels;

		for (int32_t axis = 0; axis < 3; ++axis)
		{
			GizmoGeometry(pivot, axis, line);
			for (size_t i = 1; i < line.size(); ++i)
			{
				const float distance = DistanceToSegment(mouse, line[i - 1], line[i]);
				if (distance < bestDistance)
				{
					bestDistance = distance;
					best = axis;
				}
			}
		}
		return best;
	}

	// ---- gizmo dragging ---------------------------------------------------------------

	void AnimationViewport::BeginGizmoDrag(int32_t axis, int32_t mx, int32_t my)
	{
		const std::string& name = _editor->GetSelectedNode();
		const int32_t node = _editor->FindNode(name);
		if (node < 0 || !_editor->GetDisplayedPose(name, _dragStartPose))
			return;

		const auto& view = _editor->Nodes()[node];
		_dragHadOverride = _editor->HasPoseOverride(name);
		_drag = DragKind::Gizmo;
		_dragAxis = axis;
		_dragNode = name;
		_dragStartWorld = view.world;
		_dragParentWorld = view.parentWorld;
		_dragPivot = view.world.Translation();
		_dragStartMouse = Point(mx, my);
		Project(_dragPivot, _dragPivotScreen);
		_dragStartAngle = atan2f((float)my - _dragPivotScreen.y, (float)mx - _dragPivotScreen.x);
		_editor->SetPlaying(false);
	}

	void AnimationViewport::UpdateGizmoDrag(int32_t mx, int32_t my)
	{
		const math::Vector3& axis = kAxes[_dragAxis];
		math::Matrix newWorld;

		if (_gizmoMode == GizmoMode::Rotate)
		{
			const float screenAngle = WrapAngle(atan2f((float)my - _dragPivotScreen.y, (float)mx - _dragPivotScreen.x) - _dragStartAngle);

			// Which way a positive world rotation turns on screen depends on the axis'
			// facing and the projection's handedness; measure it instead of assuming.
			const float probeRadius = PixelsToWorldAt(_dragPivot, kRingPixels);
			const math::Vector3 probe = kAxes[(_dragAxis + 1) % 3] * probeRadius;
			const math::Vector3 probeTurned = math::Vector3::Transform(probe, math::Quaternion::CreateFromAxisAngle(axis, 0.1f));
			math::Vector2 p0, p1;
			float sign = 1.0f;
			if (Project(_dragPivot + probe, p0) && Project(_dragPivot + probeTurned, p1))
			{
				const float a0 = atan2f(p0.y - _dragPivotScreen.y, p0.x - _dragPivotScreen.x);
				const float a1 = atan2f(p1.y - _dragPivotScreen.y, p1.x - _dragPivotScreen.x);
				sign = WrapAngle(a1 - a0) >= 0.0f ? 1.0f : -1.0f;
			}

			const math::Quaternion turn = math::Quaternion::CreateFromAxisAngle(axis, screenAngle * sign);
			newWorld = _dragStartWorld
				* math::Matrix::CreateTranslation(-_dragPivot)
				* math::Matrix::CreateFromQuaternion(turn)
				* math::Matrix::CreateTranslation(_dragPivot);
		}
		else
		{
			const float arrowWorld = PixelsToWorldAt(_dragPivot, kArrowPixels);
			math::Vector2 tip;
			if (!Project(_dragPivot + axis * arrowWorld, tip))
				return;

			const math::Vector2 screenAxis = tip - _dragPivotScreen;
			const float screenLength = screenAxis.Length();
			if (screenLength < 1.0f)
				return;

			const math::Vector2 mouseDelta((float)(mx - _dragStartMouse.x), (float)(my - _dragStartMouse.y));
			const float pixels = mouseDelta.Dot(screenAxis / screenLength);
			newWorld = _dragStartWorld * math::Matrix::CreateTranslation(axis * (pixels * arrowWorld / screenLength));
		}

		// Back into the parent's space: local = world * parentWorld^-1.
		math::Matrix local = newWorld * _dragParentWorld.Invert();
		math::Vector3 scale, translation;
		math::Quaternion rotation;
		if (!local.Decompose(scale, rotation, translation))
			return;

		AnimationUtils::NodePose pose = _dragStartPose;
		if (_gizmoMode == GizmoMode::Rotate)
			pose.rotation = rotation;
		else
			pose.translation = translation;

		_editor->SetPoseOverride(_dragNode, pose);
	}

	void AnimationViewport::EndGizmoDrag(bool commit)
	{
		if (_drag != DragKind::Gizmo)
			return;

		_drag = DragKind::None;

		if (!commit)
		{
			if (_dragHadOverride)
				_editor->SetPoseOverride(_dragNode, _dragStartPose);
			else
				_editor->ClearPoseOverride(_dragNode);
			return;
		}

		if (_editor->IsAutoKey())
		{
			const bool rotate = _gizmoMode == GizmoMode::Rotate;
			_editor->KeyNodes({ _dragNode }, !rotate, rotate, false, rotate ? L"Rotate bone" : L"Move bone");
		}
	}

	// ---- drawing -------------------------------------------------------------------------

	void AnimationViewport::DrawPolyline(GuiRenderer* renderer, const std::vector<math::Vector2>& points, const math::Color& colour, int32_t thickness) const
	{
		for (size_t i = 1; i < points.size(); ++i)
		{
			for (int32_t t = 0; t < thickness; ++t)
			{
				const int32_t o = t - thickness / 2;
				renderer->Line((int32_t)points[i - 1].x + o, (int32_t)points[i - 1].y, (int32_t)points[i].x + o, (int32_t)points[i].y, colour);
				renderer->Line((int32_t)points[i - 1].x, (int32_t)points[i - 1].y + o, (int32_t)points[i].x, (int32_t)points[i].y + o, colour);
			}
		}
	}

	void AnimationViewport::DrawGrid(GuiRenderer* renderer)
	{
		const float step = 0.5f;
		const int32_t lines = 10;
		const float extent = step * lines;
		const math::Color minor(1.0f, 1.0f, 1.0f, 0.08f);

		for (int32_t i = -lines; i <= lines; ++i)
		{
			const float o = step * (float)i;
			math::Vector2 a, b;
			if (Project(math::Vector3(o, 0.0f, -extent), a) && Project(math::Vector3(o, 0.0f, extent), b))
				renderer->Line((int32_t)a.x, (int32_t)a.y, (int32_t)b.x, (int32_t)b.y, i == 0 ? math::Color(AnimEditorColours::AxisZ.x, AnimEditorColours::AxisZ.y, AnimEditorColours::AxisZ.z, 0.45f) : minor);
			if (Project(math::Vector3(-extent, 0.0f, o), a) && Project(math::Vector3(extent, 0.0f, o), b))
				renderer->Line((int32_t)a.x, (int32_t)a.y, (int32_t)b.x, (int32_t)b.y, i == 0 ? math::Color(AnimEditorColours::AxisX.x, AnimEditorColours::AxisX.y, AnimEditorColours::AxisX.z, 0.45f) : minor);
		}
	}

	void AnimationViewport::DrawSkeleton(GuiRenderer* renderer, int32_t hovered)
	{
		const auto& nodes = _editor->Nodes();
		const int32_t selected = _editor->FindNode(_editor->GetSelectedNode());

		for (int32_t i = 0; i < (int32_t)nodes.size(); ++i)
		{
			const auto& node = nodes[i];
			if (node.bone < 0 || node.boneParent < 0)
				continue;

			math::Vector2 a, b;
			if (Project(node.world.Translation(), a) && Project(nodes[node.boneParent].world.Translation(), b))
			{
				const bool highlight = (i == selected || node.boneParent == selected);
				DrawPolyline(renderer, { a, b }, highlight ? AnimEditorColours::BoneSelected : AnimEditorColours::Bone, highlight ? 2 : 1);
			}
		}

		for (int32_t i = 0; i < (int32_t)nodes.size(); ++i)
		{
			const auto& node = nodes[i];
			if (node.bone < 0 && i != selected)
				continue;

			math::Vector2 p;
			if (!Project(node.world.Translation(), p))
				continue;

			const bool unkeyed = _editor->HasPoseOverride(node.name);
			const math::Color& colour = i == selected ? AnimEditorColours::BoneSelected
				: (i == hovered ? AnimEditorColours::BoneHover
				: (unkeyed ? AnimEditorColours::Unkeyed : AnimEditorColours::Bone));
			const int32_t half = (i == selected || i == hovered) ? 4 : 3;
			renderer->FillQuad((int32_t)p.x - half, (int32_t)p.y - half, half * 2, half * 2, colour);
		}

		if (hovered >= 0)
		{
			math::Vector2 p;
			if (Project(nodes[hovered].world.Translation(), p))
			{
				renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, (int32_t)p.x + 8, (int32_t)p.y - 8,
					AnimEditorColours::Text, FontAlign::CentreUD, s2ws(nodes[hovered].name));
			}
		}
	}

	void AnimationViewport::DrawGizmo(GuiRenderer* renderer, int32_t hoveredAxis)
	{
		const int32_t node = _editor->FindNode(_editor->GetSelectedNode());
		if (node < 0)
			return;

		const math::Vector3 pivot = _editor->Nodes()[node].world.Translation();
		std::vector<math::Vector2> line;
		for (int32_t axis = 0; axis < 3; ++axis)
		{
			GizmoGeometry(pivot, axis, line);
			const bool active = (_drag == DragKind::Gizmo && _dragAxis == axis) || (_drag == DragKind::None && hoveredAxis == axis);
			DrawPolyline(renderer, line, active ? AnimEditorColours::Highlight : AxisColour(axis), active ? 3 : 2);

			if (_gizmoMode == GizmoMode::Translate && line.size() == 2)
				renderer->FillQuad((int32_t)line[1].x - 4, (int32_t)line[1].y - 4, 8, 8, active ? AnimEditorColours::Highlight : AxisColour(axis));
		}
	}

	void AnimationViewport::Render(GuiRenderer* renderer, uint32_t w, uint32_t h)
	{
		const auto abs = GetAbsolutePosition();
		renderer->FillQuad(abs.x, abs.y, _size.x, _size.y, AnimEditorColours::Background);

		Camera* camera = _editor->GetPreviewCamera();
		if (camera != nullptr)
			_viewProj = camera->GetViewProjectionMatrix();

		if (camera != nullptr && camera->GetRenderTarget() != nullptr && _editor->ShowMesh())
		{
			// Draw the preview OPAQUE. A non-post render leaves the frame's alpha as
			// whatever the G-buffer held (the tonemap passes it straight through), so
			// the UI's alpha blending would make the whole image transparent - which
			// is why only the skeleton overlay used to be visible.
			auto* graphics = g_pEnv->_graphicsDevice;
			graphics->SetBlendState(BlendState::Opaque);

			// Same present path as the Scene tab's surface: HDR backbuffers need the
			// scene-RT present shader rather than the sRGB UI one.
			auto* backBuffer = g_pEnv->_graphicsDevice->GetBackBuffer();
			const bool hdr = backBuffer != nullptr && backBuffer->GetFormat() == DXGI_FORMAT_R16G16B16A16_FLOAT;
			if (hdr && _hdrPresentShader != nullptr)
				renderer->FillTexturedQuadWithShader(camera->GetRenderTarget(), abs.x, abs.y, _size.x, _size.y, math::Color(1, 1, 1, 1), _hdrPresentShader.get());
			else
				renderer->FillTexturedQuad(camera->GetRenderTarget(), abs.x, abs.y, _size.x, _size.y, math::Color(1, 1, 1, 1));

			graphics->SetBlendState(BlendState::Transparency);
		}

		int32_t mx = 0, my = 0;
		g_pEnv->_inputSystem->GetMousePosition(mx, my);
		const bool mouseOver = Element::IsMouseOver(abs.x, abs.y, _size.x, _size.y);

		const int32_t hoveredAxis = (mouseOver && _drag == DragKind::None) ? HoveredGizmoAxis(mx, my) : -1;
		const int32_t hoveredJoint = (mouseOver && _drag == DragKind::None && hoveredAxis < 0) ? PickJoint(mx, my) : -1;

		DrawGrid(renderer);
		if (_editor->ShowSkeleton())
			DrawSkeleton(renderer, hoveredJoint);
		DrawGizmo(renderer, hoveredAxis);

		const std::wstring mode = _gizmoMode == GizmoMode::Rotate ? L"Rotate (E)" : L"Move (W)";
		renderer->PrintText(renderer->_style.font.get(), (uint8_t)Style::FontSize::Tiny, abs.x + 8, abs.y + 12, AnimEditorColours::TextDim, FontAlign::CentreUD,
			mode + L"   LMB select/drag  RMB orbit  MMB pan  Wheel zoom  F frame");

		renderer->Frame(abs.x, abs.y, _size.x, _size.y, 1, AnimEditorColours::PanelBorder);
	}

	// ---- input -------------------------------------------------------------------------

	bool AnimationViewport::OnInputEvent(InputEvent event, InputData* data)
	{
		const auto abs = GetAbsolutePosition();

		switch (event)
		{
		case InputEvent::MouseDown:
		{
			const int32_t mx = data->MouseDown.xpos;
			const int32_t my = data->MouseDown.ypos;
			if (!Element::IsMouseOver(abs.x, abs.y, _size.x, _size.y))
				return false;

			g_pEnv->GetUIManager().SetInputFocus(this);
			_dragLast = Point(mx, my);

			if (data->MouseDown.button == VK_LBUTTON)
			{
				if (const int32_t axis = HoveredGizmoAxis(mx, my); axis >= 0)
				{
					BeginGizmoDrag(axis, mx, my);
				}
				else if (const int32_t joint = PickJoint(mx, my); joint >= 0)
				{
					_editor->SelectNode(_editor->Nodes()[joint].name);
				}
				return true;
			}

			if (data->MouseDown.button == VK_RBUTTON)
			{
				if (_drag == DragKind::Gizmo)
					EndGizmoDrag(false);	// right click cancels a drag
				else
					_drag = DragKind::Orbit;
				return true;
			}

			if (data->MouseDown.button == VK_MBUTTON)
			{
				_drag = DragKind::Pan;
				return true;
			}
			return false;
		}

		case InputEvent::MouseMove:
		{
			if (_drag == DragKind::None)
				return false;

			int32_t mx = 0, my = 0;
			g_pEnv->_inputSystem->GetMousePosition(mx, my);
			const int32_t dx = mx - _dragLast.x;
			const int32_t dy = my - _dragLast.y;
			_dragLast = Point(mx, my);

			if (_drag == DragKind::Orbit)
			{
				_yaw += dx * 0.4f;
				_pitch = std::clamp(_pitch - dy * 0.4f, -89.0f, 89.0f);
			}
			else if (_drag == DragKind::Pan)
			{
				const math::Vector3 forward = _target - CameraPosition();
				math::Vector3 right = forward.Cross(math::Vector3::Up);
				if (right.LengthSquared() < 1e-6f)
					right = math::Vector3::Right;
				right.Normalize();
				math::Vector3 up = right.Cross(forward);
				up.Normalize();
				const float perPixel = PixelsToWorldAt(_target, 1.0f);
				_target += (-right * (float)dx + up * (float)dy) * perPixel;
			}
			else if (_drag == DragKind::Gizmo)
			{
				UpdateGizmoDrag(mx, my);
			}
			return true;
		}

		case InputEvent::MouseUp:
		{
			if (_drag == DragKind::None)
				return false;

			if (_drag == DragKind::Gizmo && data->MouseUp.button == VK_LBUTTON)
				EndGizmoDrag(true);
			else if ((_drag == DragKind::Orbit && data->MouseUp.button == VK_RBUTTON) || (_drag == DragKind::Pan && data->MouseUp.button == VK_MBUTTON))
				_drag = DragKind::None;
			return true;
		}

		case InputEvent::MouseWheel:
			if (!Element::IsMouseOver(abs.x, abs.y, _size.x, _size.y))
				return false;
			_distance = std::clamp(_distance * (data->MouseWheel.delta > 0 ? 0.9f : 1.1f), 0.05f, kFarZ * 0.5f);
			return true;

		case InputEvent::KeyDown:
		{
			if (!Element::IsMouseOver(abs.x, abs.y, _size.x, _size.y) || g_pEnv->_inputSystem->IsCtrlDown())
				return false;

			switch (data->KeyDown.key)
			{
			case 'W': SetGizmoMode(GizmoMode::Translate); return true;
			case 'E': SetGizmoMode(GizmoMode::Rotate); return true;
			case 'F': _editor->FrameCharacter(); return true;
			case VK_ESCAPE:
				if (_drag == DragKind::Gizmo) { EndGizmoDrag(false); return true; }
				return false;
			default: return false;
			}
		}

		default:
			return false;
		}
	}
}
