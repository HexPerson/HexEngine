

#pragma once

#include "Light.hpp"

namespace HexEngine
{
	class PointLight : public Light
	{
	public:
		enum ShadowSides
		{
			PointLightShadowSides_Up,
			PointLightShadowSides_Down,
			PointLightShadowSides_Left,
			PointLightShadowSides_Right,
			PointLightShadowSides_Front,
			PointLightShadowSides_Back,
			PointLightShadowSides_Count
		};

		CREATE_COMPONENT_ID(PointLight);

		PointLight(Entity* entity);

		PointLight(Entity* entity, PointLight* clone);

		virtual void Destroy() override;

		virtual void SetDoesCastShadows(bool enabled) override;

		virtual int32_t GetMaxSupportedShadowCascades() const override { return PointLightShadowSides_Count; }

		virtual ShadowMap* GetShadowMap(int32_t index = 0) const override;

		virtual const math::Matrix& GetViewMatrix(uint32_t index = 0) const { return _viewMatrix[index]; };

		virtual const math::Matrix& GetProjectionMatrix(uint32_t index = 0) const { return _projectionMatrix[index]; };

		virtual const dx::BoundingSphere& GetLightBoundingSphere(int32_t index) const { return _lightBoundingSphere[index]; }

		virtual const dx::BoundingFrustum& GetLightBoundingFrustum(int32_t index) const { return _lightBoundingFrustum[index]; }

		virtual void ConstructMatrices(Camera* camera, float zMin, float zMax, int32_t cascadeIdx) override;

		

		virtual void Serialize(json& data, JsonFile* file) override;

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		const math::Matrix& GetViewMatrix(ShadowSides side) const;
		const math::Matrix& GetProjectionMatrix(ShadowSides side) const;

		virtual bool CreateWidget(ComponentWidget* widget) override;

	private:
		ShadowMap* _shadowMaps[ShadowSides::PointLightShadowSides_Count] = { nullptr };

		math::Matrix _viewMatrix[ShadowSides::PointLightShadowSides_Count];
		math::Matrix _projectionMatrix[ShadowSides::PointLightShadowSides_Count];
		dx::BoundingSphere _lightBoundingSphere[6];
		dx::BoundingFrustum _lightBoundingFrustum[6];
	};
}
