

#pragma once

#include "BaseComponent.hpp"
#include <functional>

namespace HexEngine
{
	enum class TransformState
	{
		Previous,
		Current,
		Interpolated
	};

	class Mesh;
	class Camera;

	// Which manipulator the scene view shows for the selected entity. Shared
	// across all Transforms - the editor switches it via hotkey (W/E/R).
	enum class EditorGizmoMode
	{
		Translate,
		Rotate,
		Scale
	};

	class HEX_API Transform : public BaseComponent
	{
	public:	

		Transform(Entity* entity);

		Transform(Entity* entity, Transform* copy) : BaseComponent(entity) {}

		CREATE_COMPONENT_ID(Transform);

		//virtual void Update(float frameTime) override;

		const math::Vector3& GetPosition(TransformState state = TransformState::Current) const;
		const math::Quaternion& GetRotation(TransformState state = TransformState::Current) const;
		const math::Vector3& GetScale(TransformState state = TransformState::Current) const;
		//const math::Matrix& GetRotationMatrix() const;
		const math::Vector3& GetForward(TransformState state = TransformState::Current) const;
		const math::Vector3& GetRight(TransformState state = TransformState::Current) const;
		const math::Vector3& GetUp(TransformState state = TransformState::Current) const;

		//const math::Vector3& GetInterpolatedPosition() const;
		//const math::Quaternion& GetInterpolatedRotation() const;

		//const math::Vector3 GetRenderPosition() const;
		//const math::Quaternion GetRenderRotation() const;

		void UpdateInterpolatedPosition(bool interpolationEnabled);

		float GetYaw();
		float GetPitch();
		float GetRoll();

		void SetYaw(float yaw);
		void SetPitch(float pitch);
		void SetRoll(float roll);

		math::Vector3 ToEulerAngles();

		void EnableInterpolation(bool enable);

		// Collapse the interpolation history (previous/interpolated) onto the
		// current state so GetPosition(Interpolated) - and therefore GetWorldTM -
		// reflects the current position immediately instead of lerping up from the
		// stale previous value over the next frame. Call after teleporting an
		// entity (spawn/respawn) so anything reading the interpolated world matrix
		// that same frame (e.g. character-controller creation/placement) doesn't
		// pick up the pre-teleport location.
		void SnapInterpolation();

		void SetPosition(const math::Vector3& position);
		void SetPositionNoNotify(const math::Vector3& position);
		void SetRotation(const math::Quaternion& rotation);
		void SetRotationNoNotify(const math::Quaternion& rotation);
		void SetScaleNoNotify(const math::Vector3& scale);

		void SetEulerYawPitchRoll(float yaw, float pitch, float roll);
		void SetEulerYawPitchRollDeg(float yaw, float pitch, float roll);
		void SetEulerAngles(const math::Vector3& angles);
		void SetEulerAnglesDeg(const math::Vector3& angles);
		void SetScale(const math::Vector3& scale);
		//void SetRotationMatrix(const math::Matrix& rotation);

		using EditorTranslateCommitCallback = std::function<void(Entity* entity, const math::Vector3& before, const math::Vector3& after)>;
		static void SetEditorTranslateCommitCallback(EditorTranslateCommitCallback callback);

		using EditorRotateCommitCallback = std::function<void(Entity* entity, const math::Quaternion& before, const math::Quaternion& after)>;
		static void SetEditorRotateCommitCallback(EditorRotateCommitCallback callback);

		using EditorScaleCommitCallback = std::function<void(Entity* entity, const math::Vector3& before, const math::Vector3& after)>;
		static void SetEditorScaleCommitCallback(EditorScaleCommitCallback callback);

		// Switching modes cancels any in-flight gizmo drag (the entity snaps back
		// to its pre-drag value; nothing is recorded for undo).
		static void SetEditorGizmoMode(EditorGizmoMode mode);
		static EditorGizmoMode GetEditorGizmoMode();

		virtual void OnMessage(Message* message, MessageListener* sender) override;
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	private:
		void UpdateRotation();

		void RenderTranslateGizmo(Camera* camera, const math::Vector3& origin, const math::Vector3& cameraPosition, float gizmoSize, bool& isHovering);
		void RenderRotateGizmo(Camera* camera, const math::Vector3& origin, const math::Vector3& cameraPosition, float gizmoSize, bool& isHovering);
		void RenderScaleGizmo(Camera* camera, const math::Vector3& origin, const math::Vector3& cameraPosition, float gizmoSize, bool& isHovering);

	private:
		struct State
		{
			math::Vector3 position;
			math::Quaternion rotation;
			math::Vector3 forward = math::Vector3::Forward;
			math::Vector3 right = math::Vector3::Right;
			math::Vector3 up = math::Vector3::Up;
			math::Vector3 scale = math::Vector3(1.0f);
		} _current, _previous, _interpolated, _cached;

		math::Vector3 _eulerAngles;
		math::Vector3 _eulerAnglesDeg;
		bool _needsRotationMatrixUpdate = true;

		uint32_t _lastInterpolationTick = 0;
		bool _enableInterpolation = false;

		std::shared_ptr<Mesh> _arrow;

		static EditorTranslateCommitCallback _editorTranslateCommitCallback;
		static EditorRotateCommitCallback _editorRotateCommitCallback;
		static EditorScaleCommitCallback _editorScaleCommitCallback;
	};
}
