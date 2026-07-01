
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
		// The base clone ctor copied _doesCastShadows but NOT the per-side shadow
		// cube maps - those are allocated lazily inside SetDoesCastShadows, which
		// the clone path never runs. Without this, a cloned / prefab-spawned
		// shadow-casting point light has the flag ticked yet _shadowMaps are null,
		// so it renders no shadows until the user re-toggles the checkbox. Re-run
		// the setter (virtual dispatch reaches PointLight's override here since the
		// base is already constructed) to allocate the maps to match the flag.
		SetDoesCastShadows(_doesCastShadows);
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
		// WORLD space, not local. transform->GetPosition() returns the
		// entity's PARENT-space position - wrong for any light parented
		// under another entity. See SpotLight::ConstructMatrices for the
		// full explanation; same bug, same fix.
		const math::Vector3 worldPos = GetEntity()->GetWorldTM().Translation();

		math::Vector3 cross = math::Vector3::Up;
		if (cascadeIdx == 2 || cascadeIdx == 3)
			cross = math::Vector3::Forward;

		_viewMatrix[cascadeIdx] = math::Matrix::CreateLookAt(worldPos, worldPos + gLightDirs[cascadeIdx] * GetRadius(), cross);
		_projectionMatrix[cascadeIdx] = math::Matrix::CreatePerspectiveFieldOfView(ToRadian(90.0f), 1.0f, 1.0f, GetRadius());

		dx::BoundingFrustum::CreateFromMatrix(_lightBoundingFrustum[cascadeIdx], _projectionMatrix[cascadeIdx], true);
		_lightBoundingFrustum[cascadeIdx].Transform(_lightBoundingFrustum[cascadeIdx], _viewMatrix[cascadeIdx].Invert());

		// Bounding sphere used by the shadow-render PVS to cull geometry
		// for THIS face's depth pass. Without this the array stayed
		// default-initialised (centre=0, radius=1) and the PVS rejected
		// everything in the scene -> shadow map rendered empty.
		_lightBoundingSphere[cascadeIdx] = dx::BoundingSphere(worldPos, GetRadius());
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