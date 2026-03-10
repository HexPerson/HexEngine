

#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"
#include "../../GUI/Elements/ImmediateMode.hpp"

namespace HexEngine
{
	Transform::Transform(Entity* entity) :
		BaseComponent(entity)
	{
		_current.forward = math::Vector3::Forward;
		_current.right = math::Vector3::Right;
		_current.up = math::Vector3::Up;

		//_arrow = Mesh::Create("EngineData.Models/Primitives/Arrow.hmesh");
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
		math::Vector3 position;
		math::Quaternion rotation;
		math::Vector3 scale;

		file->Deserialize(data, "_position", position);
		file->Deserialize(data, "_rotation", rotation);
		file->Deserialize(data, "_scale", scale);

		SetPosition(position);
		SetRotation(rotation);
		SetScale(scale);

		// Without this the interpolation will be incorrect
		//
		_previous.position = position;
		_previous,rotation = rotation;

		LOG_DEBUG("Position %.3f %.3f %.3f", position.x, position.y, position.z);
		LOG_DEBUG("Rotation %.3f %.3f %.3f", rotation.x, rotation.y, rotation.z);
		LOG_DEBUG("Scale %.3f %.3f %.3f", scale.x, scale.y, scale.z);
	}

	bool Transform::CreateWidget(ComponentWidget* widget)
	{
		Vector3Edit* position = new Vector3Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Position", &_current.position, std::bind(&Entity::ForcePosition, GetEntity(), std::placeholders::_1));
		Vector3Edit* rotation = new Vector3Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Rotation", &_eulerAnglesDeg, std::bind(&Transform::SetEulerAnglesDeg, this, std::placeholders::_1));
		Vector3Edit* scale = new Vector3Edit(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Scale", &_current.scale, std::bind(&Transform::SetScale, this, std::placeholders::_1));

		return true;
	}

	void Transform::OnMessage(Message* message, MessageListener* sender)
	{

	}

	void Transform::OnRenderEditorGizmo(bool isSelected)
	{
		
	}
}