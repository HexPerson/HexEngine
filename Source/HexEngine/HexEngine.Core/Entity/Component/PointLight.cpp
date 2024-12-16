
#include "PointLight.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"

namespace HexEngine
{
	math::Vector3 gLightDirs[PointLight::ShadowSides::PointLightShadowSides_Count] = {
		math::Vector3::Forward,
		math::Vector3::Backward,
		math::Vector3::Up,
		math::Vector3::Down,
		math::Vector3::Left,
		math::Vector3::Right,		
	};

	PointLight::PointLight(Entity* entity) :		
		Light(entity)
	{
	}

	PointLight::PointLight(Entity* entity, PointLight* clone) : Light(entity, clone)
	{
	
	}

	void PointLight::Destroy()
	{
		Light::Destroy();

		for (auto i = 0; i < PointLightShadowSides_Count; ++i)
		{
			SAFE_DELETE(_shadowMaps[i]);
		}
	}

	

	void PointLight::Serialize(json& data, JsonFile* file)
	{		
		Light::Serialize(data, file);
	}

	void PointLight::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		Light::Deserialize(data, file, mask);
	}

	void PointLight::SetDoesCastShadows(bool enabled)
	{
		if (enabled)
		{
			for (auto i = 0; i < PointLightShadowSides_Count; ++i)
			{
				if (_shadowMaps[i] == nullptr)
				{
					_shadowMaps[i] = new ShadowMap(1024, 1024);
					_shadowMaps[i]->Create();
				}
			}
		}
		else
		{
			for (auto i = 0; i < PointLightShadowSides_Count; ++i)
			{
				SAFE_DELETE(_shadowMaps[i]);
			}
		}

		Light::SetDoesCastShadows(enabled);
	}

	ShadowMap* PointLight::GetShadowMap(int32_t index) const
	{
		assert(index >= 0 && index < PointLightShadowSides_Count && "Shadowmap index out of bounds for point light");

		return _shadowMaps[index];
	}

	void PointLight::ConstructMatrices(Camera* camera, float zMin, float zMax, int32_t cascadeIdx)
	{
		auto transform = GetEntity()->GetComponent<Transform>();

		math::Vector3 cross = math::Vector3::Up;

		if (cascadeIdx == 2 || cascadeIdx == 3)
			cross = math::Vector3::Forward;

		_viewMatrix[cascadeIdx] = math::Matrix::CreateLookAt(transform->GetPosition(), transform->GetPosition() + gLightDirs[cascadeIdx] * GetRadius(), cross);

		_projectionMatrix[cascadeIdx] = math::Matrix::CreatePerspectiveFieldOfView(ToRadian(90.0f), 1.0f, 1.0f, GetRadius());

		dx::BoundingFrustum::CreateFromMatrix(_lightBoundingFrustum[cascadeIdx], _projectionMatrix[cascadeIdx], true);

		_lightBoundingFrustum[cascadeIdx].Transform(_lightBoundingFrustum[cascadeIdx], _viewMatrix[cascadeIdx].Invert());
	}

	const math::Matrix& PointLight::GetViewMatrix(PointLight::ShadowSides side) const
	{
		return _viewMatrix[side];
	}

	const math::Matrix& PointLight::GetProjectionMatrix(PointLight::ShadowSides side) const
	{
		return _projectionMatrix[side];
	}

	bool PointLight::CreateWidget(ComponentWidget* widget)
	{
		if (!Light::CreateWidget(widget))
			return false;

		

		return true;
	}
}