

#include "DirectionalLight.hpp"
#include "Transform.hpp"
#include "StaticMeshComponent.hpp"
#include "../Entity.hpp"
#include "../../HexEngine.hpp"

namespace HexEngine
{
	extern HVar r_shadowCascades;

	// Per-cascade sun shadow map resolution. Default 8192 preserves the historical
	// hardcoded value; 2048-4096 is the usual production range and each halving quarters
	// the VRAM (4 cascades at 8192 square is ~1.07 GB of depth alone). Read once when the
	// maps are allocated - changing it needs shadows toggled off/on to take effect, and the
	// value actually used is cached in _allocatedShadowMapResolution because the cascade
	// texel-snapping maths must match the texture that exists, not the current cvar.
	HVar r_shadowMapResolution("r_shadowMapResolution", "Per-cascade directional shadow map resolution (needs shadows re-toggled to apply)", 4096, 512, 8192);

	extern HVar r_shadowNearClip;

	DirectionalLight::DirectionalLight(Entity* entity) :
		Light(entity)
	{
		SetDoesCastShadows(true);
		SetIsVolumetric(true);
		SetInjectIntoGI(true);
	}

	DirectionalLight::DirectionalLight(Entity* entity, DirectionalLight* clone) :
		Light(entity)
	{
		SetDoesCastShadows(clone->GetDoesCastShadows());
		SetInjectIntoGI(clone->GetInjectIntoGI());
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
					// Cache the resolution actually allocated - ConstructMatrices' texel
					// snapping divides by it and must not drift from the real texture size.
					_allocatedShadowMapResolution = std::clamp(r_shadowMapResolution._val.i32, 512, 8192);

					// No colour target: the sun is only ever sampled through the depth SRV.
					_shadowMaps[i] = new ShadowMap(
						(uint32_t)_allocatedShadowMapResolution,
						(uint32_t)_allocatedShadowMapResolution,
						false);

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

		// Round the fitted radius UP in coarse 1/16-unit steps. The radius is
		// recomputed from the sub-frustum corners every frame and carries fp noise;
		// the ortho extent and worldUnitsPerTexel both derive from it, so even tiny
		// radius wobble rescales the shadow UV grid frame-to-frame and defeats the
		// whole-texel snap below. Quantising keeps the projection bit-stable while
		// the camera merely rotates or strafes.
		_lightBoundingSphere[cascadeIdx].Radius = ceilf(_lightBoundingSphere[cascadeIdx].Radius * 16.0f) / 16.0f;

		auto lightPosCenter = _lightBoundingSphere[cascadeIdx].Center;
		auto sunDistance = camera->GetFarZ();// _lightBoundingSphere[cascadeIdx].Radius;

		float diagonalLength = _lightBoundingSphere[cascadeIdx].Radius * 2.0f;
		float worldUnitsPerTexel = diagonalLength / (float)_allocatedShadowMapResolution;



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
		
		

		{
			// Snap the cascade centre to whole shadow-texel increments so the ortho
			// window translates in exact texel steps as the camera moves.
			//
			// The basis this happens in MUST have an origin independent of the centre
			// being snapped. The previous version built a LookAt whose TARGET was
			// lightPosCenter itself, so the centre always transformed to (0, 0, -dist)
			// in that view - snapping zero is a no-op, and every cascade origin still
			// crawled sub-texel with the camera (the user-visible edge shimmer).
			// A rotation-only basis (eye pinned at the world origin, same lookDir/Up
			// as the real view) carries the snapped X/Y through the final view matrix
			// unchanged, because the two matrices share the same rotation.
			auto lightBasis = math::Matrix::CreateLookAt(math::Vector3::Zero, lookDir, math::Vector3::Up);
			auto centerLS = math::Vector3::Transform(lightPosCenter, lightBasis);
			centerLS.x = floor((centerLS.x / worldUnitsPerTexel) + 0.5f) * worldUnitsPerTexel;
			centerLS.y = floor((centerLS.y / worldUnitsPerTexel) + 0.5f) * worldUnitsPerTexel;
			lightPosCenter = math::Vector3::Transform(centerLS, lightBasis.Invert());
		}

		_viewMatrix[cascadeIdx] = math::Matrix::CreateLookAt(lightPosCenter - (lookDir * sunDistance), lightPosCenter, math::Vector3::Up);
		//_viewMatrix[cascadeIdx] *= scalingMatrix;

		math::Vector3 mins = math::Vector3(FLT_MAX);// splitFrustumCornersLS[0];
		math::Vector3 maxs = math::Vector3(FLT_MIN);// splitFrustumCornersLS[0];

		dx::BoundingSphere sphereLightView = _lightBoundingSphere[cascadeIdx];
		
		_lightBoundingSphere[cascadeIdx].Transform(sphereLightView, _viewMatrix[cascadeIdx]);


		// X/Y: symmetric [-r, +r] so the texel snap actually takes effect. The view
		// already looks at the texel-SNAPPED centre; deriving the X/Y extent from the
		// transformed (unsnapped) sphere centre re-added a per-frame sub-texel offset
		// that cancelled the snap - imperceptible on cascade 0 (cm texels) but visible
		// crawl on far cascades (metre texels). A symmetric box leaves only the
		// whole-texel snap motion, so the grid stays aligned and edges stop shimmering.
		//
		// Z: KEEP the centre offset. Z is the depth bracket along the light dir and
		// must stay on the geometry (centre.z is ~ -sunDistance) - it has nothing to
		// do with texel snapping. (Zeroing it moved cascade 0's near/far off the
		// scene and wrecked its fidelity.)
		mins = math::Vector3(-sphereLightView.Radius, -sphereLightView.Radius, sphereLightView.Center.z - sphereLightView.Radius);
		maxs = math::Vector3( sphereLightView.Radius,  sphereLightView.Radius, sphereLightView.Center.z + sphereLightView.Radius);

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
