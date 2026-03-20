

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

		//virtual void Create() override;

		void SetLookDirection(const math::Vector3& forward, const math::Vector3& up);

		void SetPespectiveParameters(float fov, float aspectRatio, float screenNear, float screenFar);

		void SetOrthographicOffScreenParameters(float minX, float maxX, float minY, float maxY, float minZ, float maxZ);

		ITexture2D* GetRenderTarget() const;
		//ITexture2D* GetFullScreenRenderTarget() const;

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

		bool IsVisibleInFrustum(const dx::BoundingBox& aabb);
		bool IsVisibleInFrustum(const dx::BoundingOrientedBox& obb);

		const dx::BoundingFrustum& GetFrustum() const;
		const dx::BoundingFrustum& GetLargerFrustum() const;
		const dx::BoundingSphere& GetFrustumSphere() const;
		const math::Viewport& GetViewport() const;
		void SetViewport(const math::Viewport& vp);
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
		CameraProjectionMode _projectionMode = CameraProjectionMode::PerspectiveProjection;
		math::Matrix _projectionMatrix;
		math::Matrix _largerProjectionMatrix;
		math::Matrix _projectionMatrixPrev;
		math::Matrix _viewMatrix;
		math::Matrix _viewMatrixBehind;
		math::Matrix _viewMatrixPrev;
		math::Matrix _cameraToWorld;
		
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
