

#include "DirectionalLight.hpp"
#include "Transform.hpp"
#include "StaticMeshComponent.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"

namespace HexEngine
{
	extern HVar r_shadowCascades;

	const int32_t DirectionalLightShadowMapResolution = 4096;

	extern HVar r_shadowNearClip;

	DirectionalLight::DirectionalLight(Entity* entity) :
		Light(entity)
	{
		SetDoesCastShadows(true);
		SetIsVolumetric(true);
	}

	DirectionalLight::DirectionalLight(Entity* entity, DirectionalLight* clone) :
		Light(entity)
	{
		SetDoesCastShadows(clone->GetDoesCastShadows());
	}

	void DirectionalLight::Destroy()
	{
		for (auto i = 0; i < 4; ++i)
		{
			SAFE_DELETE(_shadowMaps[i]);
		}
	}

	const math::Matrix& DirectionalLight::GetViewMatrix(uint32_t index) const
	{
		return _viewMatrix[index];
	}

	const math::Matrix& DirectionalLight::GetProjectionMatrix(uint32_t index) const
	{
		return _projectionMatrix[index];
	}

	const math::Matrix& DirectionalLight::GetViewMatrixPrev(uint32_t index) const
	{
		return _viewMatrixPrev[index];
	}

	const math::Matrix& DirectionalLight::GetProjectionMatrixPrev(uint32_t index) const
	{
		return _projectionMatrixPrev[index];
	}

	void DirectionalLight::SetDoesCastShadows(bool enabled)
	{
		if (enabled)
		{
			// Directional light can have up to 4 cascades
			//
			for (auto i = 0; i < 4; ++i)
			{
				if (_shadowMaps[i] == nullptr)
				{
					_shadowMaps[i] = new ShadowMap(DirectionalLightShadowMapResolution, DirectionalLightShadowMapResolution);

					_shadowMaps[i]->Create();
				}
			}
		}
		else
		{
			for (auto i = 0; i < 4; ++i)
			{
				SAFE_DELETE(_shadowMaps[i]);
			}
		}

		Light::SetDoesCastShadows(enabled);
	}

	const dx::BoundingSphere& DirectionalLight::GetLightBoundingSphere(int32_t index) const
	{
		return _lightBoundingSphere[index];
	}

	const dx::BoundingFrustum& DirectionalLight::GetLightBoundingFrustum(int32_t index) const
	{
		return _lightBoundingFrustums[index];
	}

	int32_t DirectionalLight::GetMaxSupportedShadowCascades() const
	{
		return r_shadowCascades._val.i32;
	}

	ShadowMap* DirectionalLight::GetShadowMap(int32_t index) const
	{
		if (index < 0 || index >= GetMaxSupportedShadowCascades())
			return nullptr;

		return _shadowMaps[index];
	}

	void DirectionalLight::OnMessage(Message* message, MessageListener* sender)
	{
		Light::OnMessage(message, sender);
	}

	void DirectionalLight::ConstructMatrices(Camera* camera, float zMin, float zMax, int32_t cascadeIdx)
	{
		_viewMatrixPrev[cascadeIdx] = _viewMatrix[cascadeIdx];
		_projectionMatrixPrev[cascadeIdx] = _projectionMatrix[cascadeIdx];

		//auto frustum = camera->GetFrustum();

		//math::Vector3 corners[8], originalCorners[8];
		/*frustum.GetCorners(corners);

		for (int i = 0; i < 8; ++i)
			originalCorners[i] = corners[i];*/

		

		/*dx::BoundingFrustum cascadeFrustum;
		dx::BoundingFrustum::CreateFromMatrix(cascadeFrustum, camera->GetProjectionMatrix(), true);

		auto viewMatrixInverse = _viewMatrix;
		viewMatrixInverse = viewMatrixInverse.Invert();

		_frustum.Transform(_frustum, viewMatrixInverse);
		_boundingSphere.CreateFromFrustum(_boundingSphere, _frustum);*/

		float fmin = zMin == 0.0f ? 1.0f : (camera->GetFarZ() * zMin);
		float fmax = camera->GetFarZ() * zMax;

		

		math::Matrix tempProjection = math::Matrix::CreatePerspectiveFieldOfView(ToRadian(camera->GetFov()), g_pEnv->GetAspectRatio(), fmin, fmax);

		dx::BoundingFrustum::CreateFromMatrix(_lightBoundingFrustums[cascadeIdx], tempProjection, true);

		// Move the sub frustum into world space
		auto viewMatrixInverse = camera->GetViewMatrix();
		viewMatrixInverse = viewMatrixInverse.Invert();

		_lightBoundingFrustums[cascadeIdx].Transform(_lightBoundingFrustums[cascadeIdx], viewMatrixInverse);		



		auto transform = GetEntity()->GetComponent<Transform>();

		auto cameraPosition = camera->GetEntity()->GetComponent<Transform>()->GetPosition();


		//auto rotation = transform->GetRotationMatrix();
		auto lookDir = transform->GetForward();

		dx::BoundingSphere::CreateFromFrustum(_lightBoundingSphere[cascadeIdx], _lightBoundingFrustums[cascadeIdx]);

		//_lightBoundingSphere[cascadeIdx].Radius *= 1.3f;

		auto lightPosCenter = _lightBoundingSphere[cascadeIdx].Center;
		auto sunDistance = camera->GetFarZ();// _lightBoundingSphere[cascadeIdx].Radius;

		float diagonalLength = _lightBoundingSphere[cascadeIdx].Radius * 2.0f;
		float texelsPerUnit = (float)DirectionalLightShadowMapResolution / diagonalLength;



		// Tangent values.
		float TanFOVX = tan(0.5f * ToRadian(60.0f) * g_pEnv->GetAspectRatio());
		float TanFOVY = tan(0.5f * ToRadian(60.0f));

		// Compute the bounding sphere.
		//math::Vector3 Center = cameraPosition + camera->GetEntity()->GetComponent<Transform>()->GetForward() * (fmin + (fmax - fmin) / 2.0f);
		//math::Vector3 CornerPoint = cameraPosition + (camera->GetEntity()->GetComponent<Transform>()->GetRight() * TanFOVX + camera->GetEntity()->GetComponent<Transform>()->GetUp() * TanFOVY + camera->GetEntity()->GetComponent<Transform>()->GetForward()) * fmax;
		//float Radius = (CornerPoint - Center).Length();


		//lightPosCenter = Center;
		//sunDistance = Radius;

		//_lightBoundingSphere[cascadeIdx].Radius = Radius * 1.0f;

		//auto lightPosCenter = frustumCenter;

		//float sunDistance = diagonalLength / 2.0f;

		// ANTI SHIMMER
		//
		/*auto scalingMatrix = math::Matrix::CreateScale(texelsPerUnit);

		auto lookAt = math::Matrix::CreateLookAt(lightPosCenter - (lookDir * sunDistance), lightPosCenter, math::Vector3::Up);
		lookAt *= scalingMatrix;

		lightPosCenter = math::Vector3::Transform(lightPosCenter, lookAt);
		lightPosCenter.x = floor(lightPosCenter.x);
		lightPosCenter.y = floor(lightPosCenter.y);
		lightPosCenter.z = floor(lightPosCenter.z);
		lightPosCenter = math::Vector3::Transform(lightPosCenter, lookAt.Invert());*/


		// ANTI SHIMMER END

		// Create a sphere to cover the sub-frustum
		
		

		_viewMatrix[cascadeIdx] = math::Matrix::CreateLookAt(lightPosCenter - (lookDir * sunDistance), lightPosCenter, math::Vector3::Up);
		//_viewMatrix[cascadeIdx] *= scalingMatrix;

		math::Vector3 mins = math::Vector3(FLT_MAX);// splitFrustumCornersLS[0];
		math::Vector3 maxs = math::Vector3(FLT_MIN);// splitFrustumCornersLS[0];

		dx::BoundingSphere sphereLightView = _lightBoundingSphere[cascadeIdx];
		
		_lightBoundingSphere[cascadeIdx].Transform(sphereLightView, _viewMatrix[cascadeIdx]);


		mins = sphereLightView.Center - math::Vector3(sphereLightView.Radius);
		maxs = sphereLightView.Center + math::Vector3(sphereLightView.Radius);

		//mins = -math::Vector3(sphereLightView.Radius);
		//maxs = math::Vector3(sphereLightView.Radius);

		float worldsUnitsPerTexel = diagonalLength / (float)DirectionalLightShadowMapResolution;

		/*math::Vector3 vBorderOffset = (math::Vector3(diagonalLength, diagonalLength, diagonalLength) - (maxs - mins)) * 0.5f;
		maxs += vBorderOffset;
		mins -= vBorderOffset;*/

		/*mins /= worldsUnitsPerTexel;
		mins.x = (float)floor(mins.x);
		mins.y = (float)floor(mins.y);
		mins.z = (float)floor(mins.z);
		mins *= worldsUnitsPerTexel;

		maxs /= worldsUnitsPerTexel;
		maxs.x = (float)floor(maxs.x);
		maxs.y = (float)floor(maxs.y);
		maxs.z = (float)floor(maxs.z);
		maxs *= worldsUnitsPerTexel;*/

		//mins += sphereLightView.Center;
		//maxs += sphereLightView.Center;

		float distFromLightToCamera = 0.0f;// (transform->GetPosition() - cameraPosition).Length();
		//distFromLightToCamera += r_directionLightNearClip._val.f32;

		_projectionMatrix[cascadeIdx] = math::Matrix::CreateOrthographicOffCenter(mins.x, maxs.x, mins.y, maxs.y, -maxs.z - (camera->GetFarZ() + r_shadowNearClip._val.f32), -mins.z /*+ r_shadowNearClip._val.f32*/);

		dx::BoundingFrustum::CreateFromMatrix(_lightBoundingFrustums[cascadeIdx], _projectionMatrix[cascadeIdx], true);
		_lightBoundingFrustums[cascadeIdx].Transform(_lightBoundingFrustums[cascadeIdx], viewMatrixInverse/*_viewMatrix[cascadeIdx].Invert()*/);

		//_projectionMatrix[cascadeIdx] = math::Matrix::CreateOrthographicOffCenter(min_x, max_x, min_y, max_y, min_z, max_z);
		//_projectionMatrix[cascadeIdx] = math::Matrix::CreateOrthographicOffCenter(mins.x, maxs.x, mins.y, maxs.y, -mins.z - nearClipOffset, -maxs.z + nearClipOffset);

		//_projectionMatrix[cascadeIdx] = math::Matrix::CreateOrthographicOffCenter(0, 4096, 4096, 0, -2048, 2048);
		

		//transform->GetRotation().
		//_lightCamera.SetViewMatrix(math::Matrix::CreateLookAt(_boundingSphere.Center - (lookDir * 500.0f), _boundingSphere.Center, math::Vector3::Up));
		//_lightCamera.SetPespectiveParameters(ToRadian(_boundingSphere.Radius * 2), (maxs.x - mins.x) / (maxs.y - mins.y), 1.0f, -mins.z);
		////_lightCamera.SetOrthographicOffScreenParameters(mins.x, maxs.x, mins.y, maxs.y, -maxs.z - nearClipOffset, -mins.z);
		//_lightCamera.BuildFrustum();

		//_projectionMatrix = math::Matrix::CreateOrthographicOffCenter(frustum.LeftSlope, frustum.RightSlope, frustum.BottomSlope, frustum.TopSlope, frustum.Far, frustum.Near);// size, size, 1.0f, 900.0f);

	}

	/*DirectionalLight* DirectionalLight::Load(DiskFile* file)
	{
		DirectionalLight* light = new DirectionalLight;

		LoadBasicEntityData(file, light);

		g_pEnv->_sceneManager->GetCurrentScene()->AddEntity(light);

		return light;
	}*/

	void DirectionalLight::Serialize(json& data, JsonFile* file)
	{

		//file->Write(&direction, sizeof(math::Vector3));
	}

	void DirectionalLight::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		//math::Vector3 direction;		

		//file->Read(&direction, sizeof(math::Vector3));
		
		//_direction = direction;
	}

	
}