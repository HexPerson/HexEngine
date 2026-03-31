

#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../GUI/Elements/ImmediateMode.hpp"
#include <algorithm>
#include <array>

namespace HexEngine
{
	HVar ed_translateSnap("ed_translateSnap", "Enable grid snapping while dragging the editor translation gizmo", false, false, true);
	HVar ed_translateSnapSize("ed_translateSnapSize", "Grid size used for editor translation gizmo snapping", 1.0f, 0.001f, 10000.0f);

	Transform::EditorTranslateCommitCallback Transform::_editorTranslateCommitCallback = {};

	namespace
	{
		struct EditorTranslateGizmoState
		{
			Transform* activeTransform = nullptr;
			int32_t activeAxis = -1;
			int32_t lastMouseX = 0;
			int32_t lastMouseY = 0;
			math::Vector3 dragStartPosition = math::Vector3::Zero;
			math::Vector3 accumulatedPosition = math::Vector3::Zero;
			bool wasLeftMouseDown = false;
		};

		EditorTranslateGizmoState g_translateGizmoState;

		float DistanceToScreenLineSegment(float mouseX, float mouseY, float lineStartX, float lineStartY, float lineEndX, float lineEndY, float* outSegmentT = nullptr)
		{
			const float lineDeltaX = lineEndX - lineStartX;
			const float lineDeltaY = lineEndY - lineStartY;
			const float lineLengthSq = lineDeltaX * lineDeltaX + lineDeltaY * lineDeltaY;

			if (lineLengthSq <= FLT_EPSILON)
			{
				if (outSegmentT != nullptr)
					*outSegmentT = 0.0f;

				const float dx = mouseX - lineStartX;
				const float dy = mouseY - lineStartY;
				return sqrtf(dx * dx + dy * dy);
			}

			float t = ((mouseX - lineStartX) * lineDeltaX + (mouseY - lineStartY) * lineDeltaY) / lineLengthSq;
			t = std::clamp(t, 0.0f, 1.0f);

			if (outSegmentT != nullptr)
				*outSegmentT = t;

			const float closestX = lineStartX + lineDeltaX * t;
			const float closestY = lineStartY + lineDeltaY * t;
			const float dx = mouseX - closestX;
			const float dy = mouseY - closestY;
			return sqrtf(dx * dx + dy * dy);
		}

		void DrawAxisArrow(const math::Vector3& origin, const math::Vector3& axisDirection, float axisLength, const math::Vector3& cameraPosition, const math::Color& colour)
		{
			const math::Vector3 axisEnd = origin + axisDirection * axisLength;
			g_pEnv->_debugRenderer->DrawLine(origin, axisEnd, colour);

			math::Vector3 toCamera = cameraPosition - axisEnd;
			if (toCamera.Length() <= FLT_EPSILON)
				toCamera = math::Vector3::Forward;
			else
				toCamera.Normalize();

			math::Vector3 arrowSide = axisDirection.Cross(toCamera);
			if (arrowSide.Length() <= FLT_EPSILON)
			{
				arrowSide = axisDirection.Cross(math::Vector3::Up);
				if (arrowSide.Length() <= FLT_EPSILON)
					arrowSide = axisDirection.Cross(math::Vector3::Right);
			}

			if (arrowSide.Length() > FLT_EPSILON)
				arrowSide.Normalize();

			const float headLength = axisLength * 0.18f;
			const float headWidth = axisLength * 0.06f;

			g_pEnv->_debugRenderer->DrawLine(axisEnd, axisEnd - axisDirection * headLength + arrowSide * headWidth, colour);
			g_pEnv->_debugRenderer->DrawLine(axisEnd, axisEnd - axisDirection * headLength - arrowSide * headWidth, colour);
		}

		float SnapToStep(float value, float step)
		{
			if (step <= FLT_EPSILON)
				return value;

			return roundf(value / step) * step;
		}
	}

	Transform::Transform(Entity* entity) :
		BaseComponent(entity)
	{
		_current.forward = math::Vector3::Forward;
		_current.right = math::Vector3::Right;
		_current.up = math::Vector3::Up;

		//_arrow = Mesh::Create("EngineData.Models/Primitives/Arrow.hmesh");
	}

	void Transform::SetEditorTranslateCommitCallback(EditorTranslateCommitCallback callback)
	{
		_editorTranslateCommitCallback = callback;
	}

	void Transform::UpdateRotation()
	{
		/*if (_eulerAnglesDeg.x != _eulerAngles.x || _eulerAnglesDeg.y != _eulerAngles.y || _eulerAnglesDeg.z != _eulerAngles.z)
		{
			SetEulerAngles(math::Vector3(ToRadian(_eulerAnglesDeg.x), ToRadian(_eulerAnglesDeg.y), ToRadian(_eulerAnglesDeg.z)));
		}*/

		if (_needsRotationMatrixUpdate)
		{
			//_rotationMatrix = math::Matrix::CreateFromQuaternion(GetRotation());

			_previous.forward = _current.forward;
			_previous.right = _current.right;
			_previous.up = _current.up;

			_current.forward = math::Vector3::Transform(math::Vector3::Forward, _current.rotation);
			_current.right = math::Vector3::Transform(math::Vector3::Right, _current.rotation);
			_current.up = math::Vector3::Transform(math::Vector3::Up, _current.rotation);

			_current.forward.Normalize();
			_current.right.Normalize();
			_current.up.Normalize();

			

			_needsRotationMatrixUpdate = false;
		}		
	}

	void Transform::UpdateInterpolatedPosition(bool interpolationEnabled)
	{
		if (interpolationEnabled)
		{
			// calculate the interpolated data
			//
			if (_enableInterpolation && g_pEnv->_timeManager->_frameTime != 0.0f && g_pEnv->_timeManager->_frameCount != _lastInterpolationTick)
			{
				bool cacheNeedsClearing = false;

				_interpolated.position = math::Vector3::Lerp(_previous.position, _current.position, (float)g_pEnv->_timeManager->_interpolationFactor);

				if (_interpolated.position != _previous.position)
				{
					/*if (GetEntity()->GetName() == "Player")
					{
						bool a = false;
					}
					LOG_DEBUG("Frame%03d: Position was interpolated between [%.3f %.3f %.3f] and [%.3f %.3f %.3f] to [%.3f %.3f %.3f] using factor %f",
						g_pEnv->_timeManager->_frameCount,
						_previousPosition.x, _previousPosition.y, _previousPosition.z,
						_position.x, _position.y, _position.z,
						_interpolatedPosition.x, _interpolatedPosition.y, _interpolatedPosition.z,
						g_pEnv->_timeManager->_interpolationFactor);*/

					cacheNeedsClearing = true;
				}

				//_previousPosition = _position;
				_previous.position = _interpolated.position;

				_interpolated.rotation = math::Quaternion::Slerp(_previous.rotation, _current.rotation, (float)g_pEnv->_timeManager->_interpolationFactor);

				if (_interpolated.rotation != _previous.rotation)
				{
					cacheNeedsClearing = true;
				}

				//_previousRotation = _rotation;
				_previous.rotation = _interpolated.rotation;

				if (cacheNeedsClearing)
				{
					GetEntity()->ClearTransformCache();
				}

				_lastInterpolationTick = (uint32_t)g_pEnv->_timeManager->_frameCount;

			}
		}
		else
		{
			_interpolated.position = _current.position;
			_interpolated.rotation = _current.rotation;
		}
	}

	void Transform::EnableInterpolation(bool enable)
	{
		_enableInterpolation = enable;
	}

	const math::Vector3& Transform::GetPosition(TransformState state)  const
	{
		switch (state)
		{
		case TransformState::Previous:
			return _previous.position;
		case TransformState::Current:
		default:
			return _current.position;
		case TransformState::Interpolated:
			return _enableInterpolation ? _interpolated.position : _current.position;
		}
	}

	//const math::Vector3& Transform::GetInterpolatedPosition() const
	//{		
	//	//if (auto rb = GetEntity()->GetComponent<RigidBody>(); rb != nullptr && rb->GetIRigidBody() && rb->GetIRigidBody()->GetBodyType() != IRigidBody::BodyType::Static)
	//	if(_enableInterpolation)
	//	{
	//		return _interpolated.position;
	//	}
	//	return GetPosition();
	//}

	//const math::Quaternion& Transform::GetInterpolatedRotation() const
	//{
	//	//if (auto rb = GetEntity()->GetComponent<RigidBody>(); rb != nullptr && rb->GetIRigidBody() && rb->GetIRigidBody()->GetBodyType() != IRigidBody::BodyType::Static)
	//	if(_enableInterpolation)
	//	{
	//		return _interpolated.rotation;
	//	}
	//	return GetRotation();
	//}

	//const math::Vector3 Transform::GetRenderPosition() const
	//{
	//	return GetInterpolatedPosition();// -GetEntity()->GetAABB().Center;
	//}

	//const math::Quaternion Transform::GetRenderRotation() const
	//{
	//	return GetInterpolatedRotation();
	//}

	const math::Quaternion& Transform::GetRotation(TransformState state) const
	{
		switch (state)
		{
		case TransformState::Previous:
			return _previous.rotation;
		case TransformState::Current:
		default:
			return _current.rotation;
		case TransformState::Interpolated:
			return _enableInterpolation ? _interpolated.rotation : _current.rotation;
		}
	}

	const math::Vector3& Transform::GetScale(TransformState state) const
	{
		switch (state)
		{
		case TransformState::Previous:
			return _previous.scale;
		case TransformState::Current:
		default:
			return _current.scale;
		case TransformState::Interpolated:
			return _interpolated.scale;
		}
	}

	const math::Vector3& Transform::GetForward(TransformState state) const
	{
		switch (state)
		{
		case TransformState::Previous:
			return _previous.forward;
		case TransformState::Current:
		default:
			return _current.forward;
		case TransformState::Interpolated:
			return _interpolated.forward;
		}
	}

	const math::Vector3& Transform::GetRight(TransformState state) const
	{
		switch (state)
		{
		case TransformState::Previous:
			return _previous.right;
		case TransformState::Current:
		default:
			return _current.right;
		case TransformState::Interpolated:
			return _interpolated.right;
		}
	}

	const math::Vector3& Transform::GetUp(TransformState state) const
	{
		switch (state)
		{
		case TransformState::Previous:
			return _previous.up;
		case TransformState::Current:
		default:
			return _current.up;
		case TransformState::Interpolated:
			return _interpolated.up;
		}
	}

	math::Vector3 Transform::ToEulerAngles()
	{
		return _current.rotation.ToEuler();
		//math::Vector3 angles;

		//// pitch
		//float sinr_cosp = 2.0f * (_rotation.x * _rotation.x + _rotation.y * _rotation.z);
		//float cosr_cosp = 1.0f - 2.0f * (_rotation.x * _rotation.x + _rotation.y * _rotation.y);
		//angles.z = std::atan2(sinr_cosp, cosr_cosp);

		//// yaw
		//float sinp = 2.0f * (_rotation.x * _rotation.y - _rotation.z * _rotation.x);
		//if (std::abs(sinp) >= 1)
		//	angles.y = std::copysign(dx::g_XMHalfPi.f[0], sinp); // use 90 degrees if out of range
		//else
		//	angles.y = std::asin(sinp);

		//// roll
		//float siny_cosp = 2.0f * (_rotation.x * _rotation.z + _rotation.x * _rotation.y);
		//float cosy_cosp = 1.0f - 2.0f * (_rotation.y * _rotation.y + _rotation.z * _rotation.z);
		//angles.x = std::atan2(siny_cosp, cosy_cosp);

		//return angles;
	}

	float Transform::GetYaw()
	{
		return _eulerAngles.y;
	}

	float Transform::GetPitch()
	{
		return _eulerAngles.x;
	}

	float Transform::GetRoll()
	{
		return _eulerAngles.z;
	}

	void Transform::SetYaw(float yaw)
	{
		SetEulerYawPitchRoll(yaw, GetPitch(), GetRoll());
	}

	void Transform::SetPitch(float pitch)
	{
		SetEulerYawPitchRoll(GetYaw(), pitch, GetRoll());
	}

	void Transform::SetRoll(float roll)
	{
		SetEulerYawPitchRoll(GetYaw(), GetPitch(), roll);
	}

	void Transform::SetPosition(const math::Vector3& position)
	{
		if (_current.position == position)
		{
			// it didn't change, so don't waste time updating things that don't need updating
			return;
		}

		_previous.position = _current.position;
		_current.position = position;

		TransformChangedMessage message;
		message._flags = TransformChangedMessage::ChangeFlags::PositionChanged;
		message._position = position;

		GetEntity()->OnMessage(&message, this);

		// inform the chunk manager of our new position
		if (g_pEnv->_chunkManager->HasActiveChunks(GetEntity()->GetScene()))
		{
			g_pEnv->_chunkManager->OnEntityPositionChanged(GetEntity(), _previous.position, _current.position);
		}
	}

	void Transform::SetPositionNoNotify(const math::Vector3& position)
	{
		if (_current.position == position)
		{
			// it didn't change, so don't waste time updating things that don't need updating
			return;
		}

		_previous.position = _current.position;
		_current.position = position;

		// inform the chunk manager of our new position
		if (g_pEnv->_chunkManager->HasActiveChunks(GetEntity()->GetScene()))
		{
			g_pEnv->_chunkManager->OnEntityPositionChanged(GetEntity(), _previous.position, _current.position);
		}

		GetEntity()->ClearTransformCache();
	}

	void Transform::SetRotation(const math::Quaternion& rotation)
	{
		if (_current.rotation == rotation)
		{
			// it didn't change, so don't waste time updating things that don't need updating
			return;
		}
		_needsRotationMatrixUpdate = true;

		math::Quaternion newRot = rotation;
		//newRot.Conjugate();		

		//float angle = ToDegree(math::Quaternion::Angle(_rotation, newRot));

		//if (angle >= 180.0f)
		//	newRot.Conjugate();

		_previous.rotation = _current.rotation;

		_current.rotation.RotateTowards(newRot, dx::g_XMTwoPi.f[0]);
		_current.rotation.Normalize();

		UpdateRotation();

		_eulerAngles = ToEulerAngles();

		_eulerAnglesDeg.x = ToDegree(_eulerAngles.x);
		_eulerAnglesDeg.y = ToDegree(_eulerAngles.y);
		_eulerAnglesDeg.z = ToDegree(_eulerAngles.z);

		TransformChangedMessage message;
		message._flags = TransformChangedMessage::ChangeFlags::RotationChanged;
		message._rotation = _current.rotation;

		GetEntity()->OnMessage(&message, this);

		//GetEntity()->OnTransformChanged(false, true, false);
	}

	void Transform::SetRotationNoNotify(const math::Quaternion& rotation)
	{
		if (_current.rotation == rotation)
		{
			// it didn't change, so don't waste time updating things that don't need updating
			return;
		}
		_needsRotationMatrixUpdate = true;

		math::Quaternion newRot = rotation;
		//newRot.Conjugate();		

		float angle = ToDegree(math::Quaternion::Angle(_current.rotation, newRot));

		//if (angle >= 180.0f)
		//	newRot.Conjugate();

		_current.rotation.RotateTowards(newRot, dx::g_XMTwoPi.f[0]);
		_current.rotation.Normalize();

		UpdateRotation();

		_eulerAngles = ToEulerAngles();

		_eulerAnglesDeg.x = ToDegree(_eulerAngles.x);
		_eulerAnglesDeg.y = ToDegree(_eulerAngles.y);
		_eulerAnglesDeg.z = ToDegree(_eulerAngles.z);

		GetEntity()->ClearTransformCache();
	}

	void Transform::SetEulerYawPitchRoll(float yaw, float pitch, float roll)
	{		
		//SetEulerAngles(math::Vector3(pitch, yaw, roll));

		if (pitch > dx::g_XMTwoPi.f[0])
			pitch -= dx::g_XMTwoPi.f[0];
		else if (pitch < 0.0f)
			pitch += dx::g_XMTwoPi.f[0];

		if (yaw > dx::g_XMTwoPi.f[0])
			yaw -= dx::g_XMTwoPi.f[0];
		else if (yaw < 0.0f)
			yaw += dx::g_XMTwoPi.f[0];

		if (roll > dx::g_XMTwoPi.f[0])
			roll -= dx::g_XMTwoPi.f[0];
		else if (roll < 0.0f)
			roll += dx::g_XMTwoPi.f[0];

		SetRotation(math::Quaternion::CreateFromYawPitchRoll(yaw, pitch, roll));
	}

	void Transform::SetEulerYawPitchRollDeg(float yaw, float pitch, float roll)
	{
		SetRotation(math::Quaternion::CreateFromYawPitchRoll(ToRadian(yaw), ToRadian(pitch), ToRadian(roll)));

		//SetEulerAngles(math::Vector3(ToRadian(pitch), ToRadian(yaw), ToRadian(roll)));
	}

	void Transform::SetEulerAngles(const math::Vector3& angles)
	{
		//_eulerAngles = angles;

		//// Clamp the angles into sane range
		////
		//if (_eulerAngles.x > dx::g_XMTwoPi.f[0])
		//	_eulerAngles.x -= dx::g_XMTwoPi.f[0];

		//if (_eulerAngles.x < 0.0f/*-dx::g_XMPi.f[0]*/)
		//	_eulerAngles.x += dx::g_XMTwoPi.f[0];

		//if (_eulerAngles.y > dx::g_XMTwoPi.f[0])
		//	_eulerAngles.y -= dx::g_XMTwoPi.f[0];

		//if (_eulerAngles.y < 0.0f/*-dx::g_XMPi.f[0]*/)
		//	_eulerAngles.y += dx::g_XMTwoPi.f[0];

		//_eulerAnglesDeg.x = ToDegree(_eulerAngles.x);
		//_eulerAnglesDeg.y = ToDegree(_eulerAngles.y);
		//_eulerAnglesDeg.z = ToDegree(_eulerAngles.z);

		SetRotation(math::Quaternion::CreateFromYawPitchRoll(angles.y, angles.x, angles.z));
	}

	void Transform::SetEulerAnglesDeg(const math::Vector3& angles)
	{
		SetEulerAngles(math::Vector3(ToRadian(angles.x), ToRadian(angles.y), ToRadian(angles.z)));
		GetEntity()->ForceRotation(GetRotation());
	}

	void Transform::SetScale(const math::Vector3& scale)
	{
		if (_current.scale == scale)
		{
			return;
		}

		_previous.scale = scale;
		_current.scale = scale;

		TransformChangedMessage message;
		message._flags = TransformChangedMessage::ChangeFlags::ScaleChanged;
		message._scale = _current.scale;

		GetEntity()->OnMessage(&message, this);

		//GetEntity()->OnTransformChanged(true, false, false);
	}

	void Transform::SetScaleNoNotify(const math::Vector3& scale)
	{
		if (_current.scale == scale)
			return;

		_previous.scale = _current.scale;
		_current.scale = scale;

		GetEntity()->ClearTransformCache();
	}

	/*void Transform::SetRotationMatrix(const math::Matrix& rotation)
	{
		_rotationMatrix = rotation;

		_forward = math::Vector3::Transform(math::Vector3::Forward, _rotationMatrix);
		_right = math::Vector3::Transform(math::Vector3::Right, _rotationMatrix);
		_up = math::Vector3::Transform(math::Vector3::Up, _rotationMatrix);

		_forward.Normalize();
		_right.Normalize();
		_up.Normalize();

		GetEntity()->OnTransformChanged(false, true, false);
	}*/

	/*const math::Matrix& Transform::GetRotationMatrix() const
	{
		return _rotationMatrix;
	}*/

	

	void Transform::Serialize(json& data, JsonFile* file)
	{
		math::Vector3 position = GetPosition();
		math::Quaternion rotation = GetRotation();
		math::Vector3 scale = GetScale();

		file->Serialize(data, "_position", position);
		file->Serialize(data, "_rotation", rotation);
		file->Serialize(data, "_scale", scale);
	}

	void Transform::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;

		math::Vector3 position;
		math::Quaternion rotation;
		math::Vector3 scale;

		file->Deserialize(data, "_position", position);
		file->Deserialize(data, "_rotation", rotation);
		file->Deserialize(data, "_scale", scale);

		// Avoid broadcasting transform-change messages during deserialization to
		// prevent cross-thread scene/PVS contention while scenes are loading.
		SetPositionNoNotify(position);
		SetRotationNoNotify(rotation);
		SetScaleNoNotify(scale);

		// Without this the interpolation will be incorrect
		//
		_previous.position = position;
		_previous.rotation = rotation;
		_previous.scale = scale;

		LOG_DEBUG("Position %.3f %.3f %.3f", position.x, position.y, position.z);
		LOG_DEBUG("Rotation %.3f %.3f %.3f", rotation.x, rotation.y, rotation.z);
		LOG_DEBUG("Scale %.3f %.3f %.3f", scale.x, scale.y, scale.z);
	}

	bool Transform::CreateWidget(ComponentWidget* widget)
	{
		Vector3Edit* position = new Vector3Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Position", &_current.position, std::bind(&Entity::ForcePosition, GetEntity(), std::placeholders::_1));
		Vector3Edit* rotation = new Vector3Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Rotation", &_eulerAnglesDeg, std::bind(&Transform::SetEulerAnglesDeg, this, std::placeholders::_1));
		Vector3Edit* scale = new Vector3Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Scale", &_current.scale, std::bind(&Transform::SetScale, this, std::placeholders::_1));
		position->SetPrefabOverrideBinding(GetComponentName(), "/_position");
		rotation->SetPrefabOverrideBinding(GetComponentName(), "/_rotation");
		scale->SetPrefabOverrideBinding(GetComponentName(), "/_scale");

		return true;
	}

	void Transform::OnMessage(Message* message, MessageListener* sender)
	{

	}

	void Transform::OnRenderEditorGizmo(bool isSelected, bool& isHovering)
	{
		if (!isSelected || !g_pEnv->IsEditorMode())
		{
			if (g_translateGizmoState.activeTransform == this)
			{
				g_translateGizmoState.activeTransform = nullptr;
				g_translateGizmoState.activeAxis = -1;
				g_translateGizmoState.dragStartPosition = math::Vector3::Zero;
				g_translateGizmoState.accumulatedPosition = math::Vector3::Zero;
			}
			return;
		}

		auto scene = g_pEnv->_sceneManager->GetCurrentScene();
		if (scene == nullptr)
			return;

		auto camera = scene->GetMainCamera();
		if (camera == nullptr)
			return;

		const math::Vector3 origin = GetEntity()->GetWorldTM().Translation();
		const math::Vector3 cameraPosition = camera->GetEntity()->GetPosition();
		const float distanceToCamera = std::max((origin - cameraPosition).Length(), 1.0f);
		const float axisLength = std::clamp(distanceToCamera * 0.18f, 10.0f, 150.0f);

		const std::array<math::Vector3, 3> axisDirections = {
			math::Vector3::Right,
			math::Vector3::Up,
			math::Vector3::Forward
		};

		const std::array<math::Color, 3> axisColours = {
			math::Color(1.0f, 0.2f, 0.2f, 1.0f),
			math::Color(0.2f, 1.0f, 0.2f, 1.0f),
			math::Color(0.2f, 0.45f, 1.0f, 1.0f)
		};

		int32_t mouseX = 0;
		int32_t mouseY = 0;
		g_pEnv->_inputSystem->GetMousePosition(mouseX, mouseY);

		const bool leftMouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		int32_t hoveredAxis = -1;

		if (g_translateGizmoState.activeTransform == nullptr || g_translateGizmoState.activeTransform == this)
		{
			int32_t originScreenX = 0;
			int32_t originScreenY = 0;
			if (g_pEnv->_inputSystem->GetWorldToScreenPosition(camera, origin, originScreenX, originScreenY))
			{
				float closestAxisDistance = 14.0f;

				for (size_t axisIndex = 0; axisIndex < axisDirections.size(); ++axisIndex)
				{
					const math::Vector3 axisEnd = origin + axisDirections[axisIndex] * axisLength;

					int32_t axisScreenX = 0;
					int32_t axisScreenY = 0;
					if (!g_pEnv->_inputSystem->GetWorldToScreenPosition(camera, axisEnd, axisScreenX, axisScreenY))
						continue;

					float segmentT = 0.0f;
					const float axisDistance = DistanceToScreenLineSegment(
						(float)mouseX,
						(float)mouseY,
						(float)originScreenX,
						(float)originScreenY,
						(float)axisScreenX,
						(float)axisScreenY,
						&segmentT);

					if (axisDistance < closestAxisDistance && segmentT >= 0.1f)
					{
						closestAxisDistance = axisDistance;
						hoveredAxis = (int32_t)axisIndex;
						isHovering = true;
					}
				}
			}
		}

		if (leftMouseDown && !g_translateGizmoState.wasLeftMouseDown && hoveredAxis != -1 && g_translateGizmoState.activeTransform == nullptr)
		{
			g_translateGizmoState.activeTransform = this;
			g_translateGizmoState.activeAxis = hoveredAxis;
			g_translateGizmoState.lastMouseX = mouseX;
			g_translateGizmoState.lastMouseY = mouseY;
			g_translateGizmoState.dragStartPosition = GetEntity()->GetPosition();
			g_translateGizmoState.accumulatedPosition = g_translateGizmoState.dragStartPosition;
		}
		else if (!leftMouseDown && g_translateGizmoState.activeTransform == this)
		{
			const math::Vector3 finalPosition = GetEntity()->GetPosition();
			if (_editorTranslateCommitCallback &&
				g_translateGizmoState.dragStartPosition != finalPosition)
			{
				_editorTranslateCommitCallback(GetEntity(), g_translateGizmoState.dragStartPosition, finalPosition);
			}

			g_translateGizmoState.activeTransform = nullptr;
			g_translateGizmoState.activeAxis = -1;
			g_translateGizmoState.dragStartPosition = math::Vector3::Zero;
			g_translateGizmoState.accumulatedPosition = math::Vector3::Zero;
		}

		if (g_translateGizmoState.activeTransform == this && g_translateGizmoState.activeAxis >= 0)
		{
			const int32_t activeAxis = g_translateGizmoState.activeAxis;
			const math::Vector3 axisDirection = axisDirections[activeAxis];
			const math::Vector3 axisEnd = origin + axisDirection * axisLength;

			int32_t originScreenX = 0;
			int32_t originScreenY = 0;
			int32_t axisScreenX = 0;
			int32_t axisScreenY = 0;

			if (g_pEnv->_inputSystem->GetWorldToScreenPosition(camera, origin, originScreenX, originScreenY) &&
				g_pEnv->_inputSystem->GetWorldToScreenPosition(camera, axisEnd, axisScreenX, axisScreenY))
			{
				float axisScreenDirX = (float)(axisScreenX - originScreenX);
				float axisScreenDirY = (float)(axisScreenY - originScreenY);
				const float axisScreenLength = sqrtf(axisScreenDirX * axisScreenDirX + axisScreenDirY * axisScreenDirY);

				if (axisScreenLength > 1.0f)
				{
					axisScreenDirX /= axisScreenLength;
					axisScreenDirY /= axisScreenLength;

					const float mouseDeltaX = (float)(mouseX - g_translateGizmoState.lastMouseX);
					const float mouseDeltaY = (float)(mouseY - g_translateGizmoState.lastMouseY);
					const float projectedPixels = mouseDeltaX * axisScreenDirX + mouseDeltaY * axisScreenDirY;
					const float worldUnitsPerPixel = axisLength / axisScreenLength;

					if (fabsf(projectedPixels) > FLT_EPSILON)
					{
						g_translateGizmoState.accumulatedPosition += axisDirection * (projectedPixels * worldUnitsPerPixel);

						math::Vector3 targetPosition = g_translateGizmoState.accumulatedPosition;
						if (ed_translateSnap._val.b)
						{
							const float snapSize = std::max(ed_translateSnapSize._val.f32, 0.001f);

							switch (activeAxis)
							{
							case 0: targetPosition.x = SnapToStep(targetPosition.x, snapSize); break;
							case 1: targetPosition.y = SnapToStep(targetPosition.y, snapSize); break;
							case 2: targetPosition.z = SnapToStep(targetPosition.z, snapSize); break;
							default: break;
							}
						}

						GetEntity()->ForcePosition(targetPosition);
					}
				}
			}

			g_translateGizmoState.lastMouseX = mouseX;
			g_translateGizmoState.lastMouseY = mouseY;
			hoveredAxis = activeAxis;
		}

		for (size_t axisIndex = 0; axisIndex < axisDirections.size(); ++axisIndex)
		{
			math::Color colour = axisColours[axisIndex];

			if ((int32_t)axisIndex == hoveredAxis)
			{
				colour = math::Color(HEX_RGB_TO_FLOAT3(255, 201, 14));
			}

			DrawAxisArrow(origin, axisDirections[axisIndex], axisLength, cameraPosition, colour);
		}

		g_translateGizmoState.wasLeftMouseDown = leftMouseDown;
	}
}
