

#include "SpotLight.hpp"
#include "Transform.hpp"
#include "StaticMeshComponent.hpp"
#include "../Entity.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"

namespace HexEngine
{
	const int32_t SpotLightShadowMapResolution = 1024;

	SpotLight::SpotLight(Entity* entity) :
		Light(entity)
	{
	}

	void SpotLight::Destroy()
	{
		SAFE_DELETE(_shadowMaps[0]);
	}

	void SpotLight::SetDoesCastShadows(bool enabled)
	{
		if (enabled)
		{
			// Directional light only requires one shadow map
			//

			_shadowMaps[0] = new ShadowMap(SpotLightShadowMapResolution, SpotLightShadowMapResolution);

			_shadowMaps[0]->Create();
		}
		else
		{
			SAFE_DELETE(_shadowMaps[0]);
		}

		Light::SetDoesCastShadows(enabled);
	}

	float SpotLight::GetConeSize() const
	{
		return _coneSize;
	}

	void SpotLight::SetConeSize(float cone)
	{
		_coneSize = cone;
	}

	ShadowMap* SpotLight::GetShadowMap(int32_t index) const
	{
		if (index > 0)
			return nullptr;

		return _shadowMaps[0];
	}

	const math::Matrix& SpotLight::GetViewMatrix(uint32_t index) const
	{
		return _viewMatrix;
	}

	const math::Matrix& SpotLight::GetProjectionMatrix(uint32_t index) const
	{
		return _projectionMatrix;
	}

	const dx::BoundingSphere& SpotLight::GetLightBoundingSphere(int32_t index) const
	{
		return _lightBoundingSphere;
	}

	const dx::BoundingFrustum& SpotLight::GetLightBoundingFrustum(int32_t index) const
	{
		return _lightBoundingFrustum;
	}

	void SpotLight::SetLightDirection(const math::Vector3& direction)
	{
		_direction = direction;
	}

	void SpotLight::ConstructMatrices(Camera* camera, float zMin, float zMax, int32_t cascadeIdx)
	{
		auto transform = GetEntity()->GetComponent<Transform>();

		//math::Vector3 forward(cos(g_pEnv->_timeManager->_currentTime), 0.0f, sin(g_pEnv->_timeManager->_currentTime));

		_viewMatrix = math::Matrix::CreateLookAt(transform->GetPosition(), transform->GetPosition() + transform->GetForward(), math::Vector3::Up);		
		
		const float nearClipOffset = 0.0f;

		const auto& shadowViewport = _shadowMaps[0]->GetViewport();
				
		_projectionMatrix = math::Matrix::CreatePerspectiveFieldOfView(ToRadian(GetConeSize()), 1.0f, 0.01f, GetRadius());

		dx::BoundingFrustum::CreateFromMatrix(_lightBoundingFrustum, _projectionMatrix, true);

		_lightBoundingFrustum.Transform(_lightBoundingFrustum, _viewMatrix.Invert());

		_lightBoundingSphere = dx::BoundingSphere(transform->GetPosition(), GetRadius());
	}

	bool SpotLight::CreateWidget(ComponentWidget* widget)
	{
		Light::CreateWidget(widget);

		auto coneSize = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 20, 18),
			L"Cone Size",
			&_coneSize, 0.1f, 50.0f,
			0.1f);

		return true;
	}

	void SpotLight::Serialize(json& data, JsonFile* file)
	{
		Light::Serialize(data, file);

		SERIALIZE_VALUE(_coneSize);
	}

	void SpotLight::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		Light::Deserialize(data, file, mask);

		DESERIALIZE_VALUE(_coneSize);
	}

	/*DirectionalLight* DirectionalLight::Load(DiskFile* file)
	{
		DirectionalLight* light = new DirectionalLight;

		LoadBasicEntityData(file, light);

		g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(light);

		return light;
	}*/
}