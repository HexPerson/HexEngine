

#pragma once

#include "Light.hpp"

namespace HexEngine
{
	class Camera;

	class HEX_API SpotLight : public Light
	{
	public:
		CREATE_COMPONENT_ID(SpotLight);

		SpotLight(Entity* entity);

		SpotLight(Entity* entity, SpotLight* copy)
			: Light(entity, copy)
			, _direction(copy != nullptr ? copy->_direction : math::Vector3::Zero)
			, _yaw(copy != nullptr ? copy->_yaw : -90.0f)
			, _outerConeAngle(copy != nullptr ? copy->_outerConeAngle : 45.0f)
			, _innerConeAngle(copy != nullptr ? copy->_innerConeAngle : 35.0f)
		{
			// The original copy ctor only forwarded to Light(entity, copy) and ignored
			// SpotLight's own state - so CloneEntity (used by prefab spawn) produced a
			// SpotLight with default cone angles (45 / 35) regardless of what the source
			// authored. Inspector-visible cones plus the actual lighting/shadow frustum
			// were wrong on every prefab-instantiated spot light. We DON'T copy the cached
			// matrices / bounds (_viewMatrix, _projectionMatrix, _lightBoundingSphere,
			// _lightBoundingFrustum) - those get rebuilt by ConstructMatrices on the next
			// render pass from these primary fields.
		}

		virtual void Destroy() override;

		virtual void SetDoesCastShadows(bool enabled) override;

		virtual ShadowMap* GetShadowMap(int32_t index = 0) const override;

		virtual const math::Matrix& GetViewMatrix(uint32_t index = 0) const override;

		virtual const math::Matrix& GetProjectionMatrix(uint32_t index = 0) const override;

		virtual const dx::BoundingSphere& GetLightBoundingSphere(int32_t index) const override;

		virtual const dx::BoundingFrustum& GetLightBoundingFrustum(int32_t index) const override;

		virtual void ConstructMatrices(Camera* camera, float zMin, float zMax, int32_t cascadeIdx) override;

		virtual bool CreateWidget(ComponentWidget* widget) override;

		void SetLightDirection(const math::Vector3& direction);

		Camera* GetLightCamera();

		dx::BoundingSphere GetLightBoundingSphere();

		// Outer cone angle in degrees - the FULL angle of the cone (not half-angle).
		// Past this angle from the light's forward axis a fragment receives zero light.
		// GetConeSize() / SetConeSize() are kept as aliases for the outer angle so older
		// callers (shadow projection FOV, GI bookkeeping, etc.) keep compiling.
		float GetConeSize() const;
		void SetConeSize(float cone);

		float GetOuterConeAngle() const;
		void SetOuterConeAngle(float degrees);

		// Inner cone angle in degrees - inside this cone the fragment receives full
		// intensity; between inner and outer the cone falloff smoothsteps. Default is
		// slightly smaller than outer so the soft edge is visible without tuning.
		float GetInnerConeAngle() const;
		void SetInnerConeAngle(float degrees);

		virtual void Serialize(json& data, JsonFile* file) override;

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		//IMPLEMENT_LOADER(DirectionalLight);

	private:
		math::Vector3 _direction;
		math::Matrix _viewMatrix;
		math::Matrix _projectionMatrix;
		float _yaw = -90.0f;
		// Outer cone angle (degrees, FULL angle). Historic name kept for serialization
		// compatibility - the old `_coneSize` field this replaces was always treated as
		// the outer cone too, just without a matching inner one. Deserialise migrates the
		// old value into here.
		float _outerConeAngle = 45.0f;
		// Inner cone angle (degrees, FULL angle). Must be <= outer; clamped at runtime.
		float _innerConeAngle = 35.0f;
		dx::BoundingSphere _lightBoundingSphere;
		dx::BoundingFrustum _lightBoundingFrustum;
		
		//ShadowMap* _shadowMap = nullptr;
	};
}
