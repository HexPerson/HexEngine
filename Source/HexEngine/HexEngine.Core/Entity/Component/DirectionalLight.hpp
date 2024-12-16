

#pragma once

#include "Light.hpp"

namespace HexEngine
{
	class Camera;
	class DirectionalLight : public Light
	{
	public:
		CREATE_COMPONENT_ID(DirectionalLight);

		DirectionalLight(Entity* entity);

		DirectionalLight(Entity* entity, DirectionalLight* clone);

		virtual void Destroy() override;

		virtual void SetDoesCastShadows(bool enabled) override;

		virtual const math::Matrix& GetViewMatrix(uint32_t index = 0) const override;

		virtual const math::Matrix& GetProjectionMatrix(uint32_t index = 0) const override;

		virtual const math::Matrix& GetViewMatrixPrev(uint32_t index = 0) const override;

		virtual const math::Matrix& GetProjectionMatrixPrev(uint32_t index = 0) const override;

		virtual const dx::BoundingSphere& GetLightBoundingSphere(int32_t index) const override;

		virtual const dx::BoundingFrustum& GetLightBoundingFrustum(int32_t index) const override;

		virtual void ConstructMatrices(Camera* camera, float zMin, float zMax, int32_t cascadeIdx) override;

		virtual int32_t GetMaxSupportedShadowCascades() const override;

		virtual ShadowMap* GetShadowMap(int32_t index = 0) const override;		

		Camera* GetLightCamera();

		virtual void OnMessage(Message* message, MessageListener* sender) override;

		virtual void Serialize(json& data, JsonFile* file) override;

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

	protected:
		

	private:
		math::Matrix _viewMatrix[6];
		math::Matrix _projectionMatrix[6];
		math::Matrix _viewMatrixPrev[6];
		math::Matrix _projectionMatrixPrev[6];
		dx::BoundingSphere _lightBoundingSphere[6];
		dx::BoundingFrustum _lightBoundingFrustums[6];
	};
}
