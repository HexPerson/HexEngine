#pragma once

#include <HexEngine.Core/HexEngine.hpp>

namespace HexEditor
{
	class AnimationEditor;

	// The animation editor's 3D view: shows the preview camera's render target with
	// the skeleton, ground grid and a move/rotate gizmo drawn over it in 2D.
	//
	// Mouse: left click picks a joint or drags the gizmo; right-drag orbits;
	// middle-drag pans; wheel zooms. Keys (while hovered): W move, E rotate, F frame.
	class AnimationViewport : public HexEngine::Element
	{
	public:
		enum class GizmoMode { Translate, Rotate };

		AnimationViewport(HexEngine::Element* parent, const HexEngine::Point& position, const HexEngine::Point& size, AnimationEditor* editor);

		void Render(HexEngine::GuiRenderer* renderer, uint32_t w, uint32_t h) override;
		bool OnInputEvent(HexEngine::InputEvent event, HexEngine::InputData* data) override;

		GizmoMode GetGizmoMode() const { return _gizmoMode; }
		void SetGizmoMode(GizmoMode mode);

		// Places the preview camera from the orbit state (call before rendering).
		void ApplyCamera(HexEngine::Camera* camera);
		void Frame(const math::Vector3& centre, float radius);

		bool IsDragging() const { return _drag != DragKind::None; }

	private:
		enum class DragKind { None, Orbit, Pan, Gizmo };

		// World -> pixel in this element; false when behind the camera.
		bool Project(const math::Vector3& world, math::Vector2& out) const;
		float PixelsToWorldAt(const math::Vector3& world, float pixels) const;
		math::Vector3 CameraPosition() const;

		int32_t PickJoint(int32_t mx, int32_t my) const;
		int32_t HoveredGizmoAxis(int32_t mx, int32_t my) const;	// 0..2, -1 = none
		void GizmoGeometry(const math::Vector3& pivot, int32_t axis, std::vector<math::Vector2>& outLine) const;

		void BeginGizmoDrag(int32_t axis, int32_t mx, int32_t my);
		void UpdateGizmoDrag(int32_t mx, int32_t my);
		void EndGizmoDrag(bool commit);

		void DrawGrid(HexEngine::GuiRenderer* renderer);
		void DrawSkeleton(HexEngine::GuiRenderer* renderer, int32_t hovered);
		void DrawGizmo(HexEngine::GuiRenderer* renderer, int32_t hoveredAxis);
		void DrawPolyline(HexEngine::GuiRenderer* renderer, const std::vector<math::Vector2>& points, const math::Color& colour, int32_t thickness) const;

		AnimationEditor* _editor = nullptr;
		std::shared_ptr<HexEngine::IShader> _hdrPresentShader;

		// Orbit camera
		math::Vector3 _target = math::Vector3(0.0f, 1.0f, 0.0f);
		float _yaw = 35.0f;		// degrees
		float _pitch = -15.0f;
		float _distance = 4.0f;
		float _fov = 45.0f;
		math::Matrix _viewProj;

		GizmoMode _gizmoMode = GizmoMode::Rotate;
		DragKind _drag = DragKind::None;
		HexEngine::Point _dragLast;

		// Gizmo drag state
		int32_t _dragAxis = -1;
		std::string _dragNode;
		HexEngine::AnimationUtils::NodePose _dragStartPose;
		bool _dragHadOverride = false;
		math::Matrix _dragStartWorld;
		math::Matrix _dragParentWorld;
		math::Vector3 _dragPivot;
		math::Vector2 _dragPivotScreen;
		float _dragStartAngle = 0.0f;
		HexEngine::Point _dragStartMouse;
	};
}
