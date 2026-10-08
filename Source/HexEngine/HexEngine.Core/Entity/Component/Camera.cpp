

#include "Camera.hpp"
#include "Transform.hpp"
#include "../../HexEngine.hpp"
#include "../../Scene/PVS.hpp"
#include "../../Scene/Scene.hpp"   // SceneFlags (forward-declared in Camera.hpp)
#include "../../Graphics/IUpscalerProvider.hpp"

namespace HexEngine
{
	const float gCameraDefaultFov = 70.0f;
	// Hysteresis margin for the enlarged PVS frustum: the PVS only rebuilds once
	// the real frustum pokes outside the enlarged one, so this is "how far the
	// camera can travel between full PVS rebuilds". Generous on purpose - the
	// per-renderable fine frustum test in the draw loop (r_pvsFineCull) culls
	// the over-included set every frame, so a bigger margin costs almost
	// nothing at draw time but slashes rebuild frequency during camera motion.
	const float gViewMatrixBehindDistance = 60.0f;
	// The translation slack has to scale with the scene: 60 absolute units is
	// less than one frame of editor fly speed in a cm-scale world, so the real
	// frustum's far plane poked out the back of the enlarged one EVERY frame
	// (per-frame rebuilds with forced=0 in the perf log). Use a fraction of
	// the view distance, with the absolute value as the floor.
	const float gLargerFrustumBehindFraction = 0.1f;

	static float LargerFrustumBehindDistance(float screenFar)
	{
		const float scaled = screenFar * gLargerFrustumBehindFraction;
		return scaled > gViewMatrixBehindDistance ? scaled : gViewMatrixBehindDistance;
	}
	// Extra field of view (degrees) for the enlarged frustum: ~15 degrees of
	// rotation slack per side before a rebuild (was +10 total, i.e. ~5/side -
	// mouse-look blew through that every 2-3 frames).
	const float gLargerFrustumExtraFovDegrees = 30.0f;

	HVar r_cameraViewDistance("r_cameraViewDistance", "The maximum view depth of the camera", 1000.0f, 1.0f, 20000.0f);

	extern HVar r_lodPartition;

	Camera::Camera(Entity* entity) :
		UpdateComponent(entity)
	{
		SetPespectiveParameters(gCameraDefaultFov, g_pEnv->GetAspectRatio(), 0.1f, r_cameraViewDistance._val.f32);

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
				HexEngine::detail::ShimToDxgiFormat(g_pEnv->_graphicsDevice->GetDesiredBackBufferFormat()),
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
			_fsrEnabled = false;

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

	void Camera::EnableFSR(bool enable)
	{
		_fsrEnabled = enable;

		if (enable)
		{
			_dlssEnabled = false;

			uint32_t renderWidth = 0, renderHeight = 0;
			auto* upscaler = g_pEnv->_upscalerProvider;
			if (upscaler != nullptr && upscaler->IsSupported() &&
				upscaler->GetRenderResolution((UpscalerQuality)_fsrQuality, (uint32_t)_viewport.width, (uint32_t)_viewport.height,
					renderWidth, renderHeight) && renderWidth > 0 && renderHeight > 0)
			{
				LOG_INFO("%s: rendering at %ux%u, upscaling to %dx%d", upscaler->GetName(), renderWidth, renderHeight,
					(int32_t)_viewport.width, (int32_t)_viewport.height);

				_dlssViewport = _viewport;
				_dlssViewport.width = (float)renderWidth;
				_dlssViewport.height = (float)renderHeight;

				SetPespectiveParameters(_fov, _dlssViewport.width / _dlssViewport.height, _screenNear, _screenFar);
				g_pEnv->_sceneRenderer->Resize((int32_t)renderWidth, (int32_t)renderHeight);
				return;
			}

			LOG_WARN("FSR requested but no supported upscaler plugin is loaded - rendering at native resolution");
			_fsrEnabled = false;
		}

		CreateRenderTarget((int32_t)_viewport.width, (int32_t)_viewport.height);
		g_pEnv->_sceneRenderer->Resize((int32_t)_viewport.width, (int32_t)_viewport.height);
	}

	bool Camera::IsFSREnabled() const
	{
		return _fsrEnabled && !_fsrValueChanged;
	}

	void Camera::SetFSRQuality(int32_t quality)
	{
		_fsrQuality = std::clamp(quality, (int32_t)UpscalerQuality::NativeAA, (int32_t)UpscalerQuality::UltraPerformance);
		if (_fsrEnabled)
			EnableFSR(true);	// new internal size
	}

	ITexture2D* Camera::GetRenderTarget() const
	{
		return _renderTarget;
	}

	void Camera::SetSceneFlagMask(SceneFlags mask)
	{
		_sceneFlagMask = (int32_t)mask;
	}

	SceneFlags Camera::GetSceneFlagMask() const
	{
		return (SceneFlags)_sceneFlagMask;
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
		return (_dlssEnabled || _fsrEnabled) ? _dlssViewport : _viewport;
	}

	void Camera::SetViewport(const math::Viewport& vp)
	{
		_viewport = vp;
		if (_fsrEnabled)
			_fsrValueChanged = true;	// re-derive the internal size on the next Update

		CreateRenderTarget((int32_t)vp.width, (int32_t)vp.height);
	}

	void Camera::SetViewportWithTargetSize(const math::Viewport& vp, int32_t targetWidth, int32_t targetHeight)
	{
		_viewport = vp;
		if (_fsrEnabled)
			_fsrValueChanged = true;

		CreateRenderTarget(targetWidth, targetHeight);
	}

	void Camera::Update(float frameTime)
	{
		if (_dlssValueChanged)
		{
			EnableDLSS(_dlssEnabled);
			_dlssValueChanged = false;
		}

		if (_fsrValueChanged)
		{
			_fsrValueChanged = false;
			EnableFSR(_fsrEnabled);
		}

		if (_screenFar != r_cameraViewDistance._val.f32)
		{
			SetPespectiveParameters(_fov, _aspectRatio, _screenNear, r_cameraViewDistance._val.f32);
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
			// The camera PVS is a ROTATION-INVARIANT coarse set: a sphere of the
			// view distance around the camera (the PVS inflates it 25% for
			// translation hysteresis). The exact per-frame frustum test happens
			// in the draw loops (r_pvsFineCull, 6 dot products per renderable).
			// The previous frustum-shaped coarse set rebuilt on every frame of
			// mouse-look in large scenes - any frustum margin is exhausted by a
			// fast turn, and the rebuild cost then drove the frame rate down,
			// which made the per-frame turn larger still.
			PVSParams pvsParams;
			pvsParams.lodPartition = r_lodPartition._val.f32;
			pvsParams.shapeType = PVSParams::ShapeType::Sphere;
			const math::Vector3 cullCentre = GetEntity()->GetPosition() + GetViewOffset();
			pvsParams.shape.sphere = dx::BoundingSphere(dx::XMFLOAT3(cullCentre.x, cullCentre.y, cullCentre.z), _screenFar);
			pvsParams.camera = this;

			_pvs->CalculateVisibility(g_pEnv->_sceneManager->GetCurrentScene().get(), pvsParams);
		}
	}

	void Camera::LateUpdate(float frameTime)
	{
		SnapshotPrevMatrices();
	}

	void Camera::SnapshotPrevMatrices()
	{
		_projectionMatrixPending = _projectionMatrix;
		_viewMatrixPending = _viewMatrix;
		_hasPendingPrev = true;
	}

	void Camera::PromotePrevMatrices(uint64_t frameCount)
	{
		if (frameCount == _prevPromoteFrame)
			return;

		// One-time proof-of-execution: the editor-viewport velocity bug was
		// "prev matrices frozen at identity", and the fastest way to rule a
		// stale binary in or out is a line in the log.
		if (_prevPromoteFrame == UINT64_MAX)
			LOG_INFO("Camera %p: motion-vector prev-matrix promotion active", (void*)this);

		_prevPromoteFrame = frameCount;

		// First-ever render: no pending matrices yet - seed prev with current
		// so the first frame's velocity is zero instead of current-vs-identity.
		if (!_hasPendingPrev)
		{
			_projectionMatrixPrev = _projectionMatrix;
			_viewMatrixPrev = _viewMatrix;
			return;
		}

		_projectionMatrixPrev = _projectionMatrixPending;
		_viewMatrixPrev = _viewMatrixPending;
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

		// Setting the transform's rotation is not enough on its own: UpdateRotation
		// runs every frame, rebuilds the rotation from _cameraAngles via
		// CreateFromYawPitchRoll, and derives _lookDir from that - so it overwrites
		// whatever was set here unless _cameraAngles agrees. Feed the angles back.
		//
		// This used to read the angles back with Quaternion::ToEuler, which was
		// wrong three times over: it passed euler.x to SetYaw and euler.y to
		// SetPitch (ToEuler returns x = pitch, y = yaw), it passed RADIANS into
		// angles that UpdateRotation feeds through ToRadian as degrees, and even
		// with both of those corrected ToEuler still disagrees with
		// CreateFromYawPitchRoll at the poles - measured, it hands back a pitch of
		// 45 degrees for a straight-up look, and a spurious roll of -180 for +Z.
		//
		// The symptom was that a reflection probe, which asks for the six axis
		// directions one per capture face, got (0, 0, -1) with a couple of degrees
		// of jitter for ALL SIX faces. Every probe captured the same wall six
		// times and prefiltered to a near-uniform atlas; because a probe REPLACES
		// the sky term inside its box, that atlas then zeroed environment lighting
		// for everything indoors.
		//
		// So invert UpdateRotation's own formula instead of trusting ToEuler.
		// UpdateRotation builds CreateFromYawPitchRoll(yaw, pitch, roll) and takes
		// _lookDir = Forward * R, with Forward = (0, 0, -1), which expands to
		//
		//     lookDir = (-cos(pitch) sin(yaw), sin(pitch), -cos(pitch) cos(yaw))
		//
		// and that inverts exactly.
		const math::Vector3 f = [&]
		{
			math::Vector3 v = forward;
			v.Normalize();
			return v;
		}();

		const float pitchRad = asinf(std::clamp(f.y, -1.0f, 1.0f));
		const float cosPitch = sqrtf(std::max(0.0f, 1.0f - f.y * f.y));

		float yawRad = 0.0f;
		float rollRad = 0.0f;

		if (cosPitch > 1e-4f)
		{
			yawRad = atan2f(-f.x, -f.z);

			// Roll is whatever twist about the view axis takes the zero-roll up
			// vector onto the requested one.
			const float sp = f.y;
			math::Vector3 zeroRollUp(sp * sinf(yawRad), cosPitch, sp * cosf(yawRad));
			zeroRollUp.Normalize();

			math::Vector3 wantUp = up;
			wantUp.Normalize();

			rollRad = atan2f(zeroRollUp.Cross(wantUp).Dot(f), zeroRollUp.Dot(wantUp));
		}
		else
		{
			// Looking straight up or down: pitch is +/-90, yaw and roll are the
			// same degree of freedom, so spend it all on yaw and pick the one that
			// lands the camera's up on the requested up. At pitch +90 the world-space
			// up is (sin yaw, 0, cos yaw); at -90 it is (-sin yaw, 0, -cos yaw).
			yawRad = (f.y > 0.0f) ? atan2f(up.x, up.z) : atan2f(-up.x, -up.z);
			rollRad = 0.0f;
		}

		SetYaw(ToDegree(yawRad));
		SetPitch(ToDegree(pitchRad));
		SetRoll(ToDegree(rollRad));

		//transform->SetRotation(math::Quaternion::CreateFromRotationMatrix(basis));
	}

	void Camera::ConstructProjectionMatrix()
	{
		//_projectionMatrixPrev = _projectionMatrix;
		_projectionMatrix = math::Matrix::CreatePerspectiveFieldOfView(ToRadian(_fov), _aspectRatio, _screenNear, _screenFar);

		if (_projectionMatrixPrev == math::Matrix::Identity)
			_projectionMatrixPrev = _projectionMatrix;

		const float largerFov = (_fov + gLargerFrustumExtraFovDegrees < 170.0f) ? (_fov + gLargerFrustumExtraFovDegrees) : 170.0f;
		_largerProjectionMatrix = math::Matrix::CreatePerspectiveFieldOfView(ToRadian(largerFov), _aspectRatio, _screenNear, _screenFar + (LargerFrustumBehindDistance(_screenFar) * 2.0f));
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

		// Up comes from the camera's actual rotation, not from _rotationMatrix.
		//
		// _rotationMatrix is declared and never assigned (its one assignment in
		// UpdateRotation is commented out), so it is the identity and this always
		// handed CreateLookAt a world up of (0, 1, 0). For any normal gameplay
		// camera that is harmless - it never looks straight up or down. For a
		// reflection probe's +Y / -Y capture faces it is fatal: the look direction
		// is then PARALLEL to up, CreateLookAt's cross product degenerates, and
		// the face renders garbage.
		//
		// Deriving up from the rotation keeps the old behaviour exactly for a
		// level camera (an unrotated camera's up IS (0, 1, 0)) while staying valid
		// at the poles. The final guard covers a roll of exactly +/-90 degrees,
		// where the derived up is parallel to the look direction instead.
		math::Vector3 up = math::Vector3::Transform(
			math::Vector3::Up, transform->GetRotation());
		up.Normalize();

		if (fabsf(up.Dot(_lookDir)) > 0.999f)
		{
			const math::Vector3 fallback = (fabsf(_lookDir.y) > 0.999f)
				? math::Vector3(0.0f, 0.0f, 1.0f)
				: math::Vector3::Up;
			up = fallback - _lookDir * fallback.Dot(_lookDir);
			up.Normalize();
		}

		_viewMatrix = math::Matrix::CreateLookAt(transform->GetPosition() + GetViewOffset(), _lookDir + transform->GetPosition() + GetViewOffset(), up);
		_viewMatrixBehind = math::Matrix::CreateLookAt(transform->GetPosition() - (_lookDir * LargerFrustumBehindDistance(_screenFar)) + GetViewOffset(), _lookDir + transform->GetPosition() + GetViewOffset(), up);

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

	math::Matrix Camera::GetViewProjectionMatrix() const
	{
		return _viewMatrix * _projectionMatrix;
	}

	math::Matrix Camera::GetViewProjectionMatrixPrev() const
	{
		return _viewMatrixPrev * _projectionMatrixPrev;
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
		SERIALIZE_VALUE(_fsrEnabled);
		SERIALIZE_VALUE(_fsrQuality);
		SERIALIZE_VALUE(_effects);
		SERIALIZE_VALUE(_cameraAngles);
	}

	void Camera::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		DESERIALIZE_VALUE(_dlssEnabled);
		DESERIALIZE_VALUE(_fsrEnabled);
		DESERIALIZE_VALUE(_fsrQuality);
		DESERIALIZE_VALUE(_effects);
		DESERIALIZE_VALUE(_cameraAngles);

		_dlssValueChanged = true;
		_fsrValueChanged = _fsrEnabled;
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

		Checkbox* fsrEnabled = new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"FSR 2 Enabled", &_fsrEnabled);
		fsrEnabled->SetOnCheckFn(std::bind(&Camera::EnableFSR, this, std::placeholders::_2));
		fsrEnabled->SetPrefabOverrideBinding(GetComponentName(), "/_fsrEnabled");

		static const wchar_t* kFsrQualityNames[] = { L"Native AA", L"Quality", L"Balanced", L"Performance", L"Ultra performance" };
		DropDown* fsrQuality = new DropDown(widget, widget->GetNextPos(), Point(widget->GetSize().x - 140, 18), L"FSR quality");
		fsrQuality->SetValue(kFsrQualityNames[std::clamp(_fsrQuality, 0, 4)]);
		fsrQuality->SetPrefabOverrideBinding(GetComponentName(), "/_fsrQuality");
		for (int32_t q = 0; q < 5; ++q)
			fsrQuality->GetContextMenu()->AddItem(new ContextItem(kFsrQualityNames[q], [this, q](const std::wstring&) { SetFSRQuality(q); }));

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