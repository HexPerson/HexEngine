
#include "PointLight.hpp"
#include "Transform.hpp"
#include "../Entity.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
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
		if (clone != nullptr)
		{
			_roomClip = clone->_roomClip;
			_roomClipMin = clone->_roomClipMin;
			_roomClipMax = clone->_roomClipMax;
			_fogScale = clone->_fogScale;
		}
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
		SERIALIZE_VALUE(_roomClip);
		SERIALIZE_VALUE(_roomClipMin);
		SERIALIZE_VALUE(_roomClipMax);
		SERIALIZE_VALUE(_fogScale);
	}

	void PointLight::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		Light::Deserialize(data, file, mask);
		DESERIALIZE_VALUE(_roomClip);
		DESERIALIZE_VALUE(_roomClipMin);
		DESERIALIZE_VALUE(_roomClipMax);
		DESERIALIZE_VALUE(_fogScale);
	}

	void PointLight::SetDoesCastShadows(bool enabled)
	{
		Light::SetDoesCastShadows(enabled);
		// Dedicated cube maps are only needed by the LEGACY path (shadow atlas
		// off / per-light sphere pass). They cost ~48 MB per light, so they are
		// allocated on first legacy use (EnsureShadowMaps), not here. Turning
		// shadows off still frees them.
		if (!enabled)
		{
			for (auto i = 0; i < PointLightShadowSides_Count; ++i)
				SAFE_DELETE(_shadowMaps[i]);
		}
	}

	void PointLight::ReleaseDedicatedShadowMaps()
	{
		for (auto i = 0; i < PointLightShadowSides_Count; ++i)
			SAFE_DELETE(_shadowMaps[i]);
	}

	void PointLight::EnsureShadowMaps()
	{
		if (_doesCastShadows)
		{
			for (auto i = 0; i < PointLightShadowSides_Count; ++i)
			{
				if (_shadowMaps[i] == nullptr)
				{
					// needsColourTarget = true: the volumetric scattering path copies these
					// six faces into a TextureCubeArray and needs the plain R32_FLOAT mirror
					// as the copy source (see SceneRenderer's pointShadowFaceMaps).
					_shadowMaps[i] = new ShadowMap(1024, 1024, true);
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

		new Checkbox(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Clip to room box when unshadowed", &_roomClip);
		new DragFloat(widget, widget->GetNextPos(), Point(widget->GetSize().x - 20, 18), L"Fog scale", &_fogScale, 0.0f, 1.0f, 0.01f, 2);

		return true;
	}
}