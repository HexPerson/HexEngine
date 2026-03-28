

#include "Camera.hpp"
#include "Transform.hpp"
#include "../../HexEngine.hpp"
#include "../../Scene/PVS.hpp"

namespace HexEngine
{
	const float gCameraDefaultFov = 70.0f;
	const float gViewMatrixBehindDistance = 20.0f;
	const float gDefaultMaxViewDistance = 350.0f;

	extern HVar r_lodPartition;

	Camera::Camera(Entity* entity) :
		UpdateComponent(entity)
	{
		SetPespectiveParameters(gCameraDefaultFov, g_pEnv->GetAspectRatio(), 0.1f, gDefaultMaxViewDistance);

		uint32_t width, height;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(width, height);

		SetViewport(math::Viewport(0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f));

		//_fullScreenRenderTarget = g_pEnv->_graphicsDevice->CreateTexture(_renderTarget);

		_dlssViewport = _viewport;

		SetPitchLimits(math::Vector2(-89.0f, 89.0f), true);

		_pvs = new PVS;
	}

	Camera::Camera(Entity* entity, Camera* clone) :
		UpdateComponent(entity)
	{
		SetViewport(clone->GetViewport());

		//_fullScreenRenderTarget = g_pEnv->_graphicsDevice->CreateTexture(_renderTarget);

		_dlssViewport = _viewport;

		SetPitchLimits(clone->_pitchLimits, clone->_hasPitchLimit);

		_projectionMatrix = clone->_projectionMatrix;

		_pvs = new PVS;
	}

	Camera::~Camera()
	{
		SAFE_DELETE(_pvs);
		//SAFE_DELETE(_fullScreenRenderTarget);
		SAFE_DELETE(_renderTarget);
	}

	void Camera::CreateRenderTarget(int32_t width, int32_t height)
	{
		auto format = g_pEnv->_graphicsDevice->GetBackBuffer()->GetFormat();

		SAFE_DELETE(_renderTarget);

		if (width > 0 && height > 0)
		{
			_renderTarget = g_pEnv->_graphicsDevice->CreateTexture2D(
				width,
				height,
				(DXGI_FORMAT)g_pEnv->_graphicsDevice->GetDesiredBackBufferFormat(),
				1,
				D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
				0,
				1,
				0,
				nullptr,
				(D3D11_CPU_ACCESS_FLAG)0,
				D3D11_RTV_DIMENSION_TEXTURE2D,
				D3D11_UAV_DIMENSION_UNKNOWN,
				D3D11_SRV_DIMENSION_TEXTURE2D);
		}
	}

	void Camera::EnableDLSS(bool enable)
	{
		_dlssEnabled = enable;

		if (enable)
		{
			// Check for DLSS support
			if (auto streamline = g_pEnv->_streamlineProvider; streamline != nullptr && streamline->IsEnabled())
			{
				auto featureMask = streamline->GetSupportedFeaturesMask();

				if ((featureMask & StreamlineFeature::DLSS) != 0)
				{
					int32_t optimalWidth, optimalHeight;

					if (streamline->QueryOptimalDLSSSettings(
						(int32_t)_viewport.width, (int32_t)_viewport.height,
						DLSSMode::MaxQuality,
						optimalWidth, optimalHeight) == true)
					{
						LOG_INFO("DLSS determined an optimum render size of %dx%d (from %dx%d)", optimalWidth, optimalHeight, _viewport.width, _viewport.height);

						_dlssViewport.width = (float)optimalWidth;
						_dlssViewport.height = (float)optimalHeight;

						streamline->SetDLSSOptions(1.0f, true, true, DLSSMode::MaxQuality, (int32_t)_viewport.width, (int32_t)_viewport.height);

						//CreateRenderTarget(_dlssViewport.width, _dlssViewport.height);

						SetPespectiveParameters(_fov, _dlssViewport.width / _dlssViewport.height, _screenNear, _screenFar);

						g_pEnv->_sceneRenderer->Resize((int32_t)_dlssViewport.width, (int32_t)_dlssViewport.height);
					}
				}
			}
		}
		else
		{
			CreateRenderTarget((int32_t)_viewport.width, (int32_t)_viewport.height);
			
			g_pEnv->_sceneRenderer->Resize((int32_t)_viewport.width, (int32_t)_viewport.height);
		}
	}

	bool Camera::IsDLSSEnabled() const
	{
		return _dlssEnabled && !_dlssValueChanged;
	}

	ITexture2D* Camera::GetRenderTarget() const
	{
		return _renderTarget;
	}

	/*ITexture2D* Camera::GetFullScreenRenderTarget() const
	{
		return _fullScreenRenderTarget;
	}*/

	float Camera::GetNearZ() const
	{
		return _screenNear;
	}

	float Camera::GetFarZ() const
	{
		return _screenFar;
	}

	float Camera::GetFov() const
	{
		return _fov;
	}

	float Camera::GetAspectRatio() const
	{
		return _aspectRatio;
	}

	const math::Viewport& Camera::GetViewport() const
	{
		return _dlssEnabled ? _dlssViewport : _viewport;
	}

	void Camera::SetViewport(const math::Viewport& vp)
	{
		_viewport = vp;

		CreateRenderTarget((int32_t)vp.width, (int32_t)vp.height);
	}

	void Camera::Update(float frameTime)
	{
		if (_dlssValueChanged)
		{
			EnableDLSS(_dlssEnabled);
			_dlssValueChanged = false;
		}

		UpdateRotation();
		ConstructViewMatrix();

		if (_projectionMatrixNeedsUpdate)
		{
			ConstructProjectionMatrix();
			_projectionMatrixNeedsUpdate = false;
		}

		if (_hasMovedThisFrame || _pvs->NeedsRebuild())
		{
			PVSParams pvsParams;
			pvsParams.lodPartition = r_lodPartition._val.f32;
			pvsParams.shapeType = PVSParams::ShapeType::Frustum2;
			pvsParams.shape.frustum.sm = _frustum;
			pvsParams.shape.frustum.lg = _largerFrustum;
			pvsParams.camera = this;

			_pvs->CalculateVisibility(g_pEnv->_sceneManager->GetCurrentScene().get(), pvsParams);
		}
	}

	void Camera::LateUpdate(float frameTime)
	{
		_projectionMatrixPrev = _projectionMatrix;
		_viewMatrixPrev = _viewMatrix;
	}

	void Camera::ResetHasMovedThisFrame()
	{
		_hasMovedThisFrame = false;
	}

	void Camera::SetPespectiveParameters(float fov, float aspectRatio, float screenNear, float screenFar)
	{
		_fov = fov;
		_aspectRatio = aspectRatio;
		_screenNear = screenNear;
		_screenFar = screenFar;
		_projectionMode = CameraProjectionMode::PerspectiveProjection;		

		ConstructProjectionMatrix();

		_projectionMatrixNeedsUpdate = false;
	}

	void Camera::SetOrthographicOffScreenParameters(float minX, float maxX, float minY, float maxY, float minZ, float maxZ)
	{
		_projectionMatrix = math::Matrix::CreateOrthographicOffCenter(minX, maxX, minY, maxY, minZ, maxZ);
	}

	void Camera::SetYaw(float yawDegrees)
	{
		if (yawDegrees > 180.0f)
			yawDegrees -= 360.0f;
		else if (yawDegrees < -180.0f)
			yawDegrees += 360.0f;

		_cameraAngles.y = yawDegrees;
	}

	void Camera::SetPitch(float pitchDegrees)
	{
		_cameraAngles.x = pitchDegrees;
	}

	void Camera::SetRoll(float rollDegrees)
	{
		_cameraAngles.z = rollDegrees;
	}

	float Camera::GetYaw()
	{
		return _cameraAngles.y;
	}

	float Camera::GetPitch()
	{
		return _cameraAngles.x;
	}

	float Camera::GetRoll()
	{
		return _cameraAngles.z;
	}

	void Camera::SetLookDirection(const math::Vector3& forward, const math::Vector3& up)
	{
		auto transform = GetEntity()->GetComponent<Transform>();

		/*auto right = forward.Cross(up);

		auto correctUp = right.Cross(up);

		auto basis = math::Matrix(right, correctUp, -forward);

		_cameraToWorld.CreateFromQuaternion(math::Quaternion::CreateFromRotationMatrix(basis));*/

		auto rot = math::Quaternion::LookRotation(forward, up);

		transform->SetRotation(rot);

		auto euler = rot.ToEuler();

		SetYaw(euler.x);
		SetPitch(euler.y);
		SetRoll(euler.z);

		//transform->SetRotation(math::Quaternion::CreateFromRotationMatrix(basis));
	}

	void Camera::ConstructProjectionMatrix()
	{
		//_projectionMatrixPrev = _projectionMatrix;
		_projectionMatrix = math::Matrix::CreatePerspectiveFieldOfView(ToRadian(_fov), _aspectRatio, _screenNear, _screenFar);

		if (_projectionMatrixPrev == math::Matrix::Identity)
			_projectionMatrixPrev = _projectionMatrix;

		_largerProjectionMatrix = math::Matrix::CreatePerspectiveFieldOfView(ToRadian(_fov+10.0f), _aspectRatio, _screenNear, _screenFar + (gViewMatrixBehindDistance * 2.0f));
	}

	void Camera::UpdateRotation()
	{
		if (_cameraAngles != _previousCameraAngles || _lookDir.Length() == 0.0f)
		{
			auto transform = GetEntity()->GetComponent<Transform>();

			if (_hasYawLimit)
			{
				_cameraAngles.y = std::clamp(_cameraAngles.y, _yawLimits.x, _yawLimits.y);
			}
			if (_hasPitchLimit)
			{
				_cameraAngles.x = std::clamp(_cameraAngles.x, _pitchLimits.x, _pitchLimits.y);
			}
			if (_hasRollLimit)
			{
				_cameraAngles.z = std::clamp(_cameraAngles.z, _rollLimits.x, _rollLimits.y);
			}

			auto rotation = math::Quaternion::CreateFromYawPitchRoll(ToRadian(_cameraAngles.y), ToRadian(_cameraAngles.x), ToRadian(_cameraAngles.z));

			transform->SetRotation(rotation);

			// Update our rotation matrix
			//
			//_rotationMatrix = transform->GetRotationMatrix();

			// Update the look dir
			//
			_lookDir = math::Vector3::Transform(math::Vector3::Forward, rotation);
			_lookDir.Normalize();

			_previousCameraAngles = _cameraAngles;

			_hasMovedThisFrame = true;
		}
	}

	void Camera::OnMessage(Message* message, MessageListener* sender)
	{
		if (message->_id == MessageId::TransformChanged && sender != this)
		{
			auto transformMessage = message->CastAs<TransformChangedMessage>();

			if ((transformMessage->_flags & TransformChangedMessage::ChangeFlags::PositionChanged) != 0)
			{
				_hasMovedThisFrame = true;

				Update(0.0f);
			}
		}
	}

	//void Camera::OnTransformChanged(bool scaleChanged, bool rotationChanged, bool translationChanged)
	//{
	//	Entity::OnTransformChanged(scaleChanged, rotationChanged, translationChanged);

	//	//if (rotationChanged || translationChanged)
	//		_hasMovedThisFrame = true;
	//}

	void Camera::SetViewMatrix(const math::Matrix& viewMatrix)
	{
		_viewMatrix = viewMatrix;
	}

	void Camera::SetViewOffset(const math::Vector3& offset)
	{
		_viewOffset = offset;
	}

	void Camera::ConstructViewMatrix()
	{
		auto transform = GetEntity()->GetComponent<Transform>();

		math::Vector3 up = _rotationMatrix.Up();		

		_viewMatrix = math::Matrix::CreateLookAt(transform->GetPosition() + GetViewOffset(), _lookDir + transform->GetPosition() + GetViewOffset(), up);
		_viewMatrixBehind = math::Matrix::CreateLookAt(transform->GetPosition() - (_lookDir * gViewMatrixBehindDistance) + GetViewOffset(), _lookDir + transform->GetPosition() + GetViewOffset(), up);

		BuildFrustum();

		/*_frustum.Origin = transform->GetPosition();

		auto orientation = math::Vector4::Transform(math::Vector3::Forward, transform->GetRotation());
		orientation.Normalize();

		_frustum.Orientation = orientation;*/
	}
	

	void Camera::BuildFrustum()
	{
		// Update the frustum
		//
		dx::BoundingFrustum::CreateFromMatrix(_frustum, _projectionMatrix, true);
		dx::BoundingFrustum::CreateFromMatrix(_largerFrustum, _largerProjectionMatrix, true);

		_frustum.Transform(_frustum, _viewMatrix.Invert());
		_largerFrustum.Transform(_largerFrustum, _viewMatrixBehind.Invert());

		_boundingSphere.CreateFromFrustum(_boundingSphere, _frustum);		
	}

	const math::Vector3& Camera::GetLookDir() const
	{
		return _lookDir;
	}

	const math::Vector3& Camera::GetViewOffset() const
	{
		return _viewOffset;
	}

	const math::Matrix& Camera::GetViewMatrix() const
	{
		return _viewMatrix;
	}

	const math::Matrix& Camera::GetProjectionMatrix() const
	{
		return _projectionMatrix;
	}

	const math::Matrix& Camera::GetViewMatrixPrev() const
	{
		return _viewMatrixPrev;
	}

	const math::Matrix& Camera::GetProjectionMatrixPrev() const
	{
		return _projectionMatrixPrev;
	}

	bool Camera::IsVisibleInFrustum(const dx::BoundingBox& aabb)
	{
		return _frustum.Contains(aabb) != 0;// _frustum.Intersects(aabb);// || _frustum.Contains(aabb) != 0;
	}

	bool Camera::IsVisibleInFrustum(const dx::BoundingOrientedBox& obb)
	{
		return _frustum.Intersects(obb);
	}

	const dx::BoundingFrustum& Camera::GetFrustum() const
	{
		return _frustum;
	}

	const dx::BoundingFrustum& Camera::GetLargerFrustum() const
	{
		return _largerFrustum;
	}

	const dx::BoundingSphere& Camera::GetFrustumSphere() const
	{
		return _boundingSphere;
	}

	bool Camera::HasMovedThisFrame()
	{
		return _hasMovedThisFrame;
	}

	/*Camera* Camera::Load(DiskFile* file)
	{
		Camera* camera = new Camera;

		g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(camera);

		LOG_DEBUG("Loaded Camera entity [%p]", camera);

		LoadBasicEntityData(file, camera);

		for (auto& comp : camera->GetAllComponents())
		{
			LOG_DEBUG("component %s = %p", GUID_toString(comp->GetGUID()).c_str(), comp);
		}

		return camera;
	}*/

	void Camera::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_dlssEnabled);
		SERIALIZE_VALUE(_effects);
		SERIALIZE_VALUE(_cameraAngles);
	}

	void Camera::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		DESERIALIZE_VALUE(_dlssEnabled);
		DESERIALIZE_VALUE(_effects);
		DESERIALIZE_VALUE(_cameraAngles);

		_dlssValueChanged = true;
		
	}

	void Camera::SetYawLimits(const math::Vector2& limit, bool set)
	{
		_yawLimits = limit;
		_hasYawLimit = set;
	}

	void Camera::SetPitchLimits(const math::Vector2& limit, bool set)
	{
		_pitchLimits = limit;
		_hasPitchLimit = set;
	}

	void Camera::SetRollLimits(const math::Vector2& limit, bool set)
	{
		_rollLimits = limit;
		_hasRollLimit = set;
	}

	PVS* Camera::GetPVS() const
	{
		return _pvs;
	}

	bool Camera::CreateWidget(ComponentWidget* widget)
	{
		Checkbox* dlssEnabled = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"DLSS Enabled", &_dlssEnabled);
		dlssEnabled->SetOnCheckFn(std::bind(&Camera::EnableDLSS, this, std::placeholders::_2));
		dlssEnabled->SetPrefabOverrideBinding(GetComponentName(), "/_dlssEnabled");

		//Checkbox* ssrEnabled = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"SSR Enabled", [this]() { return HEX_HASFLAG(GetCameraEffects(), CameraEffect::SSR);});
		//ssrEnabled->SetOnCheckFn(std::bind(&Camera::ToggleEffect, this, CameraEffect::SSR));
		//dlssEnabled->SetPrefabOverrideBinding(GetComponentName(), "/_dlssEnabled");

		return true;
	}

	void Camera::AddEffect(CameraEffect effect)
	{
		_effects |= effect;
	}

	void Camera::RemoveEffect(CameraEffect effect)
	{
		_effects &= ~effect;
	}

	void Camera::ToggleEffect(CameraEffect effect)
	{
		if ((_effects & effect) == (CameraEffect)0)
		{
			AddEffect(effect);
		}
		else
		{
			RemoveEffect(effect);
		}
	}

	CameraEffect Camera::GetCameraEffects() const
	{
		return _effects;
	}

	void Camera::OnDebugRender()
	{
		//g_pEnv->_debugRenderer->DrawFrustum(_frustum, math::Color(1,0,0.1,1));
		//g_pEnv->_debugRenderer->DrawFrustum(_largerFrustum, math::Color(0, 1, 0.1, 1));
	}
}