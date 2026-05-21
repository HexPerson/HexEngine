

#include "SpotLight.hpp"
#include "Transform.hpp"
#include "StaticMeshComponent.hpp"
#include "../Entity.hpp"
#include "../../Environment/TimeManager.hpp"
#include "../../Environment/IEnvironment.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include <algorithm>

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
		return _outerConeAngle;
	}

	void SpotLight::SetConeSize(float cone)
	{
		SetOuterConeAngle(cone);
	}

	float SpotLight::GetOuterConeAngle() const
	{
		return _outerConeAngle;
	}

	void SpotLight::SetOuterConeAngle(float degrees)
	{
		_outerConeAngle = std::clamp(degrees, 1.0f, 179.0f);
		// Keep inner <= outer at all times; nudging inner along avoids the user having to
		// fix both when they drag the outer slider below the current inner value.
		if (_innerConeAngle > _outerConeAngle)
			_innerConeAngle = _outerConeAngle;
	}

	float SpotLight::GetInnerConeAngle() const
	{
		return _innerConeAngle;
	}

	void SpotLight::SetInnerConeAngle(float degrees)
	{
		_innerConeAngle = std::clamp(degrees, 0.0f, _outerConeAngle);
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

		// Outer cone widget pushes through SetOuterConeAngle to keep inner <= outer
		// invariant after every drag. DragFloat binds the value pointer directly so the
		// inspector display tracks live, but the OnDrag callback re-runs the clamp.
		auto outerCone = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 20, 18),
			L"Outer Cone (deg)",
			&_outerConeAngle, 1.0f, 179.0f,
			0.1f);
		outerCone->SetOnDrag([this](float v, float, float) { SetOuterConeAngle(v); });
		outerCone->SetPrefabOverrideBinding(GetComponentName(), "/_outerConeAngle");

		auto innerCone = new DragFloat(
			widget,
			widget->GetNextPos(),
			Point(widget->GetSize().x - 20, 18),
			L"Inner Cone (deg)",
			&_innerConeAngle, 0.0f, 179.0f,
			0.1f);
		innerCone->SetOnDrag([this](float v, float, float) { SetInnerConeAngle(v); });
		innerCone->SetPrefabOverrideBinding(GetComponentName(), "/_innerConeAngle");

		return true;
	}

	void SpotLight::Serialize(json& data, JsonFile* file)
	{
		Light::Serialize(data, file);

		SERIALIZE_VALUE(_outerConeAngle);
		SERIALIZE_VALUE(_innerConeAngle);
	}

	void SpotLight::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		Light::Deserialize(data, file, mask);

		// Migrate legacy `_coneSize` field if present and the new outer-cone field hasn't
		// been written yet. The old _coneSize was always interpreted as the FULL cone
		// angle (forward-light path used `_coneSize * 0.5` for the half-angle, and the
		// shadow projection FOV used ToRadian(_coneSize)), so it maps 1:1 to the new
		// outer cone. Inner cone gets seeded a touch below outer so old assets get a
		// visible soft edge without authoring effort.
		bool hasOuter = data.contains("_outerConeAngle");
		bool hasInner = data.contains("_innerConeAngle");
		bool hasLegacy = data.contains("_coneSize");

		if (!hasOuter && hasLegacy)
		{
			float legacyConeSize = _outerConeAngle;
			file->Deserialize(data, "_coneSize", legacyConeSize);
			_outerConeAngle = std::clamp(legacyConeSize, 1.0f, 179.0f);
		}
		else if (hasOuter)
		{
			DESERIALIZE_VALUE(_outerConeAngle);
		}

		if (hasInner)
		{
			DESERIALIZE_VALUE(_innerConeAngle);
		}
		else
		{
			// 85% of outer = a small but visible soft edge band. Roughly matches what
			// the old Phong-exponent cone produced for typical content.
			_innerConeAngle = _outerConeAngle * 0.85f;
		}

		// Final invariant check after migration / re-serialise.
		_outerConeAngle = std::clamp(_outerConeAngle, 1.0f, 179.0f);
		_innerConeAngle = std::clamp(_innerConeAngle, 0.0f, _outerConeAngle);
	}

	/*DirectionalLight* DirectionalLight::Load(DiskFile* file)
	{
		DirectionalLight* light = new DirectionalLight;

		LoadBasicEntityData(file, light);

		g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(light);

		return light;
	}*/
}
