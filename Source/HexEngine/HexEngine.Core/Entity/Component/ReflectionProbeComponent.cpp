

#include "ReflectionProbeComponent.hpp"
#include "Camera.hpp"
#include "../Entity.hpp"
#include "../../Scene/Scene.hpp"
#include "../../HexEngine.hpp"
#include "../../Graphics/IGraphicsDevice.hpp"
#include "../../Graphics/ITexture2D.hpp"
#include "../../GUI/Elements/ComponentWidget.hpp"
#include "../../GUI/Elements/DragFloat.hpp"
#include "../../GUI/Elements/Checkbox.hpp"
#include "../../GUI/Elements/Button.hpp"

namespace HexEngine
{
	// Capture-face basis. ProbeEnvMap.shader reconstructs the identical basis
	// (forward = dir, right = normalize(cross(up, forward)), up = cross(forward,
	// right)) to map a direction back onto a face + UV, so any change here must
	// be mirrored there. Order: +X, -X, +Y, -Y, +Z, -Z.
	const math::Vector3 ReflectionProbeComponent::kFaceDirs[6] =
	{
		math::Vector3( 1.0f, 0.0f, 0.0f),
		math::Vector3(-1.0f, 0.0f, 0.0f),
		math::Vector3( 0.0f, 1.0f, 0.0f),
		math::Vector3( 0.0f,-1.0f, 0.0f),
		math::Vector3( 0.0f, 0.0f, 1.0f),
		math::Vector3( 0.0f, 0.0f,-1.0f),
	};
	const math::Vector3 ReflectionProbeComponent::kFaceUps[6] =
	{
		math::Vector3(0.0f, 1.0f, 0.0f),
		math::Vector3(0.0f, 1.0f, 0.0f),
		math::Vector3(0.0f, 0.0f,-1.0f), // looking straight up: pick a horizontal up
		math::Vector3(0.0f, 0.0f, 1.0f), // looking straight down
		math::Vector3(0.0f, 1.0f, 0.0f),
		math::Vector3(0.0f, 1.0f, 0.0f),
	};

	// Face capture resolution. Modest on purpose: the atlas the deferred pass
	// samples is 128px per row, so 256px faces already oversample it 2x.
	static constexpr int32_t kProbeFaceSize = 256;

	// Atlas geometry - MUST match ENVMAP_FACE_SIZE / ENVMAP_ROUGHNESS_ROWS in
	// EnvMapCommon.shader (same layout as the sky atlas).
	static constexpr int32_t kProbeAtlasWidth = 128;
	static constexpr int32_t kProbeAtlasRows = 5;

	ReflectionProbeComponent::ReflectionProbeComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	ReflectionProbeComponent::ReflectionProbeComponent(Entity* entity, ReflectionProbeComponent* copy) :
		UpdateComponent(entity)
	{
		if (copy == nullptr)
			return;

		_extents = copy->_extents;
		_boxProjection = copy->_boxProjection;
		// Captured data is per-position; a copied probe recaptures at its own spot.
		_captureRequested = true;
	}

	ReflectionProbeComponent::~ReflectionProbeComponent()
	{
		DestroyRig();

		for (auto*& face : _faces)
			SAFE_DELETE(face);
		SAFE_DELETE(_envAtlas);
	}

	math::Vector3 ReflectionProbeComponent::GetWorldCentre() const
	{
		// World transform, not GetPosition(): GetPosition is parent-space and a
		// probe parented under a moving root would capture from the wrong spot.
		return const_cast<Entity*>(GetEntity())->GetWorldTM().Translation();
	}

	void ReflectionProbeComponent::EnsureRig()
	{
		if (_rigEntity != nullptr)
			return;

		Scene* scene = GetEntity()->GetScene();
		if (scene == nullptr)
			return;

		_rigEntity = scene->CreateEntity("ReflectionProbeRig", GetWorldCentre());
		if (_rigEntity == nullptr)
			return;

		// Runtime-only scaffolding: never serialized, never rendered.
		_rigEntity->SetFlag(EntityFlags::DoNotSave);

		_rigCamera = _rigEntity->AddComponent<Camera>();
		// 90-degree square faces, near matched to the main camera's default.
		_rigCamera->SetPespectiveParameters(ToRadian(90.0f), 1.0f, 0.1f, 1000.0f);
		_rigCamera->SetViewport(math::Viewport(0.0f, 0.0f, (float)kProbeFaceSize, (float)kProbeFaceSize, 0.0f, 1.0f));
		// Marks this as an offline capture: the renderer skips SSR/NRD and TAA
		// for it, and won't sample reflection probes into the capture. Without
		// this the NRD denoiser asserts - its buffers are main-camera sized while
		// this camera is 256px, and the jitter conversion between the two scales
		// a half-pixel jitter to 7.5 pixels.
		_rigCamera->SetEnvironmentCapture(true);
		// Post-processing stays ON for capture faces even though that bakes the
		// tonemapped LDR frame into the probe: the camera render target is only
		// ever written by the post chain's output stage (every RT write in
		// SceneRenderer sits behind canPostProcess), so masking
		// PostProcessingEnabled off leaves the RT at its clear colour and the
		// probe captures six black faces. Verified the hard way. A linear HDR
		// capture needs a pre-tonemap copy path in the renderer - worthwhile
		// follow-up, but a tonemapped probe is correct-looking in practice
		// because the reflected scene is tonemapped again only subtly.
	}

	void ReflectionProbeComponent::DestroyRig()
	{
		if (_rigEntity != nullptr)
		{
			if (Scene* scene = GetEntity() ? GetEntity()->GetScene() : nullptr)
				scene->DestroyEntity(_rigEntity);
			_rigEntity = nullptr;
			_rigCamera = nullptr;
		}
	}

	void ReflectionProbeComponent::OrientRigForFace(int32_t face)
	{
		if (_rigEntity == nullptr || _rigCamera == nullptr)
			return;

		_rigEntity->SetPosition(GetWorldCentre());
		_rigCamera->SetLookDirection(kFaceDirs[face], kFaceUps[face]);
		// Render this camera via the engine's secondary-camera pass this frame.
		_rigCamera->SetRendersToTarget(true);
	}

	ITexture2D* ReflectionProbeComponent::EnsureEnvAtlas()
	{
		if (_envAtlas == nullptr)
		{
			_envAtlas = g_pEnv->_graphicsDevice->CreateTexture2D(
				kProbeAtlasWidth,
				kProbeAtlasWidth * kProbeAtlasRows,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
				1);
			if (_envAtlas != nullptr)
				_envAtlas->SetDebugName("ReflectionProbeEnvAtlas");
		}
		return _envAtlas;
	}

	void ReflectionProbeComponent::Update(float frameTime)
	{
		(void)frameTime;

		if (_pendingFace >= 0)
		{
			LOG_INFO("ReflectionProbe '%s': banking face %d", GetEntity()->GetName().c_str(), _pendingFace);
			// The secondary-camera pass rendered face _pendingFace last frame;
			// bank it before re-aiming the rig.
			ITexture2D* rt = _rigCamera != nullptr ? _rigCamera->GetRenderTarget() : nullptr;
			if (rt == nullptr)
			{
				// Camera lost its target (device reset?) - abort and retry.
				_pendingFace = -1;
				_captureRequested = true;
				return;
			}

			if (_faces[_pendingFace] == nullptr)
				_faces[_pendingFace] = g_pEnv->_graphicsDevice->CreateTexture(rt);
			if (_faces[_pendingFace] != nullptr)
				rt->CopyTo(_faces[_pendingFace]);

			if (_pendingFace == 5)
			{
				// Capture complete: stop the per-frame scene render, flag the
				// prefilter pass, and drop the rig until the next request.
				_pendingFace = -1;
				_facesComplete = true;
				_atlasDirty = true;
				if (_rigCamera != nullptr)
					_rigCamera->SetRendersToTarget(false);
				DestroyRig();
			}
			else
			{
				++_pendingFace;
				OrientRigForFace(_pendingFace);
			}
			return;
		}

		// Rig warm-up. The rig entity and its camera are created from inside the
		// component update phase, so the camera's own Update - which builds its
		// frustum and PVS - has not run yet. Rendering a camera whose PVS was
		// never built is what the engine's other offscreen-capture path
		// (IconService) avoids by explicitly seeding the PVS before calling
		// RenderScene. Rather than reach into the PVS here, give the rig one
		// full frame to update itself before asking the renderer to draw it.
		if (_warmupFrames > 0)
		{
			--_warmupFrames;
			if (_warmupFrames == 0)
			{
				LOG_INFO("ReflectionProbe '%s': rig warm, starting face 0",
					GetEntity()->GetName().c_str());
				_pendingFace = 0;
				OrientRigForFace(0);
			}
			return;
		}

		if (_captureRequested)
		{
			LOG_INFO("ReflectionProbe '%s': starting 6-face capture", GetEntity()->GetName().c_str());
			_captureRequested = false;
			EnsureRig();
			if (_rigEntity == nullptr || _rigCamera == nullptr)
			{
				// Scene not ready yet - try again next frame.
				_captureRequested = true;
				return;
			}
			// Aim the rig but leave RendersToTarget off until it has updated once.
			_rigEntity->SetPosition(GetWorldCentre());
			_rigCamera->SetLookDirection(kFaceDirs[0], kFaceUps[0]);
			_warmupFrames = 2;
		}
	}

	void ReflectionProbeComponent::Serialize(json& data, JsonFile* file)
	{
		SERIALIZE_VALUE(_extents);
		SERIALIZE_VALUE(_boxProjection);
	}

	void ReflectionProbeComponent::Deserialize(json& data, JsonFile* file, uint32_t mask)
	{
		(void)mask;
		_serializationState = BaseComponent::SerializationState::Deserializing;

		DESERIALIZE_VALUE(_extents);
		DESERIALIZE_VALUE(_boxProjection);

		// Captured radiance is not serialized - rebuild it at this position.
		_captureRequested = true;

		_serializationState = BaseComponent::SerializationState::Ready;
	}

	bool ReflectionProbeComponent::CreateWidget(ComponentWidget* widget)
	{
		const int32_t fullWidth = widget->GetSize().x - 20;

		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Extent X", &_extents.x, 0.5f, 500.0f, 0.25f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Extent Y", &_extents.y, 0.5f, 500.0f, 0.25f);
		new DragFloat(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Extent Z", &_extents.z, 0.5f, 500.0f, 0.25f);
		new Checkbox(widget, widget->GetNextPos(), Point(fullWidth, 18), L"Box Projection", &_boxProjection);
		new Button(widget, widget->GetNextPos(), Point(fullWidth, 22), L"Capture Now",
			[this](Button*) { RequestCapture(); return true; });

		return true;
	}
}
