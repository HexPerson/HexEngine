

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

		SpotLight(Entity* entity, SpotLight* copy) : Light(entity, copy) {}

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

		float GetConeSize() const;
		void SetConeSize(float cone);

		virtual void Serialize(json& data, JsonFile* file) override;

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		//IMPLEMENT_LOADER(DirectionalLight);

	private:
		math::Vector3 _direction;
		math::Matrix _viewMatrix;
		math::Matrix _projectionMatrix;
		float _yaw = -90.0f;
		float _coneSize = 10.0f;
		dx::BoundingSphere _lightBoundingSphere;
		dx::BoundingFrustum _lightBoundingFrustum;
		
		//ShadowMap* _shadowMap = nullptr;
	};
}
