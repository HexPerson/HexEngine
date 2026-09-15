

#pragma once

#include "UpdateComponent.hpp"
#include "../../Graphics/ITexture2D.hpp"
#include "Transform.hpp"

namespace HexEngine
{
	enum class CameraProjectionMode
	{
		PerspectiveProjection,
		OrthographicProjection,
		OrthographicOffCenterProjection,
	};

	class MeshInstance;
	class PVS;
	enum class SceneFlags;   // defined in Scene.hpp; masked per-camera at render time

	enum class CameraEffect
	{
		None,
		SSR = HEX_BITSET(0),
	};
	DEFINE_ENUM_FLAG_OPERATORS(CameraEffect);

	class HEX_API Camera : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(Camera);

		Camera(Entity* entity);

		Camera(Entity* entity, Camera* clone);

		virtual ~Camera();

		virtual void Update(float frameTime) override;

		virtual void LateUpdate(float frameTime) override;

		// Motion-vector reference bookkeeping. Two stages because the same
		// camera can be rendered through RenderScene MORE THAN ONCE per frame
		// (the editor does): a single latch at end-of-render made the second
		// render see prev == current, zeroing every world-static velocity -
		// no motion blur while panning, while camera-locked geometry (the sky
		// dome) showed huge phantom velocity instead.
		//
		// SnapshotPrevMatrices: latch this render's matrices into the PENDING
		// slot (called at the end of every RenderScene; the game loop's
		// LateUpdate lands here too - idempotent).
		// PromotePrevMatrices: move pending -> prev, once per frame, called at
		// the START of RenderScene before the per-frame cbuffer is filled -
		// so every render within a frame sees LAST frame's matrices.
		void SnapshotPrevMatrices();
		void PromotePrevMatrices(uint64_t frameCount);

		//virtual void Create() override;

		void SetLookDirection(const math::Vector3& forward, const math::Vector3& up);

		void SetPespectiveParameters(float fov, float aspectRatio, float screenNear, float screenFar);

		void SetOrthographicOffScreenParameters(float minX, float maxX, float minY, float maxY, float minZ, float maxZ);

		ITexture2D* GetRenderTarget() const;
		//ITexture2D* GetFullScreenRenderTarget() const;

		// Opt-in: when true, SceneManager renders this (non-main) camera into its
		// own render target each frame. Used for secondary views such as a
		// top-down map. Defaults false so ordinary cameras are not auto-rendered.
		void SetRendersToTarget(bool enable) { _rendersToTarget = enable; }
		bool RendersToTarget() const { return _rendersToTarget; }

		// Marks this camera as an offline environment capture (reflection probe
		// face). The renderer skips the temporally-accumulating passes for these:
		//
		//  - SSR/NRD: the denoiser's buffers are sized for the MAIN camera, but a
		//    capture camera has its own (much smaller) viewport. Jitter is handed
		//    to NRD in NDC and converted back to pixels using the denoiser's
		//    width, so a 256px capture against 3840px buffers scales a +/-0.5px
		//    jitter to +/-7.5 and trips NRD's assert. Capture faces have no
		//    history to denoise against anyway.
		//  - TAA jitter/resolve: a capture is a one-shot render with no history;
		//    jitter would just offset it by a sub-pixel and the resolve would
		//    blend against another camera's history.
		void SetEnvironmentCapture(bool enable) { _environmentCapture = enable; }
		bool IsEnvironmentCapture() const { return _environmentCapture; }

		// Per-camera scene-flag mask, AND-ed with the scene's flags at render time.
		// Defaults to all bits (no masking). Lets a secondary view (e.g. the map)
		// skip passes like PostProcessingEnabled it doesn't need.
		void SetSceneFlagMask(SceneFlags mask);
		SceneFlags GetSceneFlagMask() const;

		void EnableDLSS(bool enable);
		bool IsDLSSEnabled() const;

		float GetYaw();
		float GetPitch();
		float GetRoll();

		void SetYaw(float yawDegrees);
		void SetPitch(float pitchDegrees);
		void SetRoll(float rollDegrees);

		void SetViewMatrix(const math::Matrix& viewMatrix);
		void BuildFrustum();

		float GetNearZ() const;
		float GetFarZ() const;
		float GetFov() const;
		float GetAspectRatio() const;

		const math::Vector3& GetLookDir() const;
		const math::Vector3& GetViewOffset() const;
		const math::Matrix& GetViewMatrix() const;
		const math::Matrix& GetProjectionMatrix() const;
		const math::Matrix& GetViewMatrixPrev() const;
		const math::Matrix& GetProjectionMatrixPrev() const;
		// Combined view * projection (row-vector convention, view*proj order).
		// Returns by value - the two matrices are stored separately and this is
		// not cached, so callers that need it every frame in a hot loop should
		// keep a local. Prefer this over hand-composing GetViewMatrix() *
		// GetProjectionMatrix() at call sites.
		math::Matrix GetViewProjectionMatrix() const;
		math::Matrix GetViewProjectionMatrixPrev() const;

		bool IsVisibleInFrustum(const dx::BoundingBox& aabb);
		bool IsVisibleInFrustum(const dx::BoundingOrientedBox& obb);

		const dx::BoundingFrustum& GetFrustum() const;
		const dx::BoundingFrustum& GetLargerFrustum() const;
		const dx::BoundingSphere& GetFrustumSphere() const;
		const math::Viewport& GetViewport() const;
		void SetViewport(const math::Viewport& vp);
		// Render at `vp` but size the render target independently.
		//
		// The deferred fullscreen passes derive their gbuffer UVs from the
		// VIEWPORT size (g_screenWidth/g_screenHeight) while sampling the
		// full-size gbuffer over 0..1, so a camera whose viewport is smaller than
		// the shared buffers samples the wrong region entirely. An offscreen
		// capture therefore has to rasterize at the full buffer size even when it
		// only wants a square sub-rect of the result - which is what this lets it
		// say. See ReflectionProbeComponent::EnsureRig.
		void SetViewportWithTargetSize(const math::Viewport& vp, int32_t targetWidth, int32_t targetHeight);
		bool HasMovedThisFrame();
		void ResetHasMovedThisFrame();

		//virtual void OnTransformChanged(bool scaleChanged, bool rotationChanged, bool translationChanged) override;

		void SetYawLimits(const math::Vector2& limit, bool set = true);
		void SetPitchLimits(const math::Vector2& limit, bool set = true);
		void SetRollLimits(const math::Vector2& limit, bool set = true);
		void SetViewOffset(const math::Vector3& offset);

		virtual void Serialize(json& data, JsonFile* file) override;

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		virtual void OnMessage(Message* message, MessageListener* sender) override;

		virtual bool CreateWidget(ComponentWidget* widget) override;

		PVS* GetPVS() const;		

		void AddEffect(CameraEffect effect);
		void RemoveEffect(CameraEffect effect);
		void ToggleEffect(CameraEffect effect);
		CameraEffect GetCameraEffects() const;

		virtual void OnDebugRender() override;

	private:
		void ConstructProjectionMatrix();
		void ConstructViewMatrix();
		void UpdateRotation();
		void CreateRenderTarget(int32_t width, int32_t height);

	protected:
		math::Viewport _viewport;
		math::Viewport _dlssViewport;
		ITexture2D* _renderTarget = nullptr;
		//ITexture2D* _fullScreenRenderTarget = nullptr;
		bool _rendersToTarget = false;
		bool _environmentCapture = false;
		int32_t _sceneFlagMask = -1;   // all bits set = no masking; stored as int (SceneFlags is fwd-declared)
		CameraProjectionMode _projectionMode = CameraProjectionMode::PerspectiveProjection;
		math::Matrix _projectionMatrix;
		math::Matrix _largerProjectionMatrix;
		math::Matrix _projectionMatrixPrev;
		math::Matrix _viewMatrix;
		math::Matrix _viewMatrixBehind;
		math::Matrix _viewMatrixPrev;
		math::Matrix _cameraToWorld;
		// Pending motion-vector reference (see SnapshotPrevMatrices /
		// PromotePrevMatrices): latched at end of every render, promoted to
		// the Prev pair once per frame at the first render's start.
		math::Matrix _projectionMatrixPending;
		math::Matrix _viewMatrixPending;
		uint64_t _prevPromoteFrame = UINT64_MAX;
		bool _hasPendingPrev = false;
		
		float _fov = 0.0f;
		float _aspectRatio = 0.0f;
		float _screenNear = 0.0f;
		float _screenFar = 0.0f;
		bool _projectionMatrixNeedsUpdate = true;
		math::Vector3 _cameraAngles;
		math::Vector3 _previousCameraAngles;
		math::Vector3 _lookDir;
		math::Vector3 _viewOffset;
		math::Matrix _rotationMatrix;
		dx::BoundingFrustum _frustum;
		dx::BoundingFrustum _largerFrustum;
		dx::BoundingSphere _boundingSphere;
		bool _hasMovedThisFrame = false;

		math::Vector2 _yawLimits;
		math::Vector2 _pitchLimits;
		math::Vector2 _rollLimits;
		bool _hasYawLimit = false;
		bool _hasPitchLimit = false;
		bool _hasRollLimit = false;

		bool _dlssEnabled = false;
		bool _dlssValueChanged = false;

		CameraEffect _effects = CameraEffect::None;

		//std::unordered_map<MeshInstance*, std::vector<std::pair<Mesh*, Entity*>>> _renderables[4];

		PVS* _pvs = nullptr;
	};
}
