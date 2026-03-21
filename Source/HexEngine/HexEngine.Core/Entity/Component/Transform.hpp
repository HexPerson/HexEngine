

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

		void SetPosition(const math::Vector3& position);
		void SetPositionNoNotify(const math::Vector3& position);
		void SetRotation(const math::Quaternion& rotation);
		void SetRotationNoNotify(const math::Quaternion& rotation);

		void SetEulerYawPitchRoll(float yaw, float pitch, float roll);
		void SetEulerYawPitchRollDeg(float yaw, float pitch, float roll);
		void SetEulerAngles(const math::Vector3& angles);
		void SetEulerAnglesDeg(const math::Vector3& angles);
		void SetScale(const math::Vector3& scale);
		//void SetRotationMatrix(const math::Matrix& rotation);

		using EditorTranslateCommitCallback = std::function<void(Entity* entity, const math::Vector3& before, const math::Vector3& after)>;
		static void SetEditorTranslateCommitCallback(EditorTranslateCommitCallback callback);

		virtual void OnMessage(Message* message, MessageListener* sender) override;
		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(ComponentWidget* widget) override;
		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

	private:
		void UpdateRotation();

	private:
		struct State
		{
			math::Vector3 position;
			math::Quaternion rotation;
			math::Vector3 forward = math::Vector3::Forward;
			math::Vector3 right = math::Vector3::Right;
			math::Vector3 up = math::Vector3::Up;
			math::Vector3 scale = math::Vector3(1.0f);
		} _current, _previous, _interpolated;

		//math::Vector3 _position;
		//math::Quaternion _rotation;
		//math::Quaternion _previousRotation;
		//
		//math::Vector3 _scale = math::Vector3(1.0f);
		////math::Matrix _rotationMatrix;
		//math::Vector3 _forward;
		//math::Vector3 _right;
		//math::Vector3 _up;
		//

		//math::Vector3 _previousPosition;
		//math::Vector3 _interpolatedPosition;

		math::Vector3 _eulerAngles;
		math::Vector3 _eulerAnglesDeg;
		bool _needsRotationMatrixUpdate = true;

		//math::Quaternion _interpolatedRotation;
		uint32_t _lastInterpolationTick = 0;
		bool _enableInterpolation = false;

		std::shared_ptr<Mesh> _arrow;

		static EditorTranslateCommitCallback _editorTranslateCommitCallback;
	};
}
