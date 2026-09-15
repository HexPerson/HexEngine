

#include "ReflectionProbeComponent.hpp"
#include "Camera.hpp"
#include "../Entity.hpp"
#include "../../Scene/Scene.hpp"
#include "../../Scene/PVS.hpp"
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

	const ReflectionProbeComponent* ReflectionProbeComponent::s_captureInFlight = nullptr;

	// Capture-chain dump. A probe that reports six banked faces and a completed
	// prefilter but hands the shader an all-black atlas can be failing at any of
	// four points - the rig's scene render, the post chain's copy into the rig
	// camera's target, the downsample into the face, or the prefilter. Set this
	// to 1 and the next capture writes the rig render target and each downsampled
	// face to <cwd>/probe_dump_*.png, which tells you which of the four it is
	// instead of leaving you to infer it from the final image.
	HVar r_iblProbeDumpCapture("r_iblProbeDumpCapture", "Dump each probe capture face + its source render target to disk", false, false, true);

	ReflectionProbeComponent::ReflectionProbeComponent(Entity* entity) :
		UpdateComponent(entity)
	{
	}

	void ReflectionProbeComponent::OnDebugRender()
	{
		if (g_pEnv == nullptr || g_pEnv->_debugRenderer == nullptr)
			return;

		const math::Vector3 c = GetWorldCentre();
		const math::Vector3 e = _extents;

		dx::BoundingBox box;
		box.Center = dx::XMFLOAT3(c.x, c.y, c.z);
		box.Extents = dx::XMFLOAT3(e.x, e.y, e.z);

		// Amber while capturing, green once the atlas is live, dim red if the probe
		// has no capture at all (nothing will be contributed inside this box).
		const math::Color colour =
			(_pendingFace >= 0 || _warmupFrames > 0) ? math::Color(1.0f, 0.65f, 0.0f, 0.8f) :
			_atlasReady                              ? math::Color(0.2f, 1.0f, 0.3f, 0.6f) :
			                                           math::Color(0.8f, 0.2f, 0.2f, 0.5f);

		g_pEnv->_debugRenderer->DrawAABB(box, colour);
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
		// Never leave the scene-wide bake slot held by a destroyed probe.
		if (s_captureInFlight == this)
			s_captureInFlight = nullptr;

		DestroyRig();

		for (auto*& face : _faces)
			SAFE_DELETE(face);
		SAFE_DELETE(_envAtlas);
		SAFE_DELETE(_shTex);
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

		const math::Vector3 centre = GetWorldCentre();
		const math::Vector3 localPos = GetEntity()->GetPosition();
		LOG_INFO("ReflectionProbe '%s': worldCentre=(%.2f, %.2f, %.2f) localPos=(%.2f, %.2f, %.2f)",
			GetEntity()->GetName().c_str(), centre.x, centre.y, centre.z,
			localPos.x, localPos.y, localPos.z);

		_rigEntity = scene->CreateEntity("ReflectionProbeRig", GetWorldCentre());
		if (_rigEntity == nullptr)
			return;

		// Runtime-only scaffolding: never serialized, never rendered.
		_rigEntity->SetFlag(EntityFlags::DoNotSave);

		_rigCamera = _rigEntity->AddComponent<Camera>();
		// 90-degree faces, near matched to the main camera's default. The aspect
		// and viewport are set together further down.
		//
		// DEGREES, not radians: ConstructProjectionMatrix applies ToRadian(_fov)
		// itself (IconService and the camera default both pass degrees). Passing
		// ToRadian(90) here converted twice - 90 degrees became 1.5708, which
		// became 1.57 DEGREES - so each face was a near-telephoto crop of a wall
		// patch a fraction of a metre across instead of a 90-degree view of the
		// room. Six of those prefilter to a flat, structureless atlas, which is
		// what made an indoor probe kill the environment term instead of
		// supplying it.
		// Capture at the LARGEST SQUARE that fits the shared render buffers, not at
		// the small face resolution.
		//
		// The renderer's fullscreen passes (lighting, composition) draw a quad
		// sampling UV 0..1 of the full-size gbuffer regardless of the current
		// viewport. Rendering the capture into a small corner therefore squashes
		// the ENTIRE gbuffer into that corner: the capture's own geometry occupies
		// ~0.067 x 0.124 of the sampled range and everything else is cleared, so
		// the face comes out a flat wash that varies only slightly per direction.
		// That is exactly what every capture looked like until this was found -
		// and raising the viewport to full size immediately produced real room
		// structure in the face.
		//
		// Cost: six full-resolution scene renders per capture, one per frame. It's
		// a bake, not a per-frame cost, and correctness beats cheapness here. The
		// faces are downsampled to kProbeFaceSize on the way out (see Update) so
		// only ~3 MB of probe textures are retained rather than ~200 MB.
		// Rasterize at the FULL buffer size, not at a square sub-rect of it.
		//
		// The fullscreen passes take their gbuffer UVs from the viewport size
		// (g_screenWidth/g_screenHeight) and sample the shared gbuffer over 0..1,
		// so those two only agree when the viewport IS the buffer. A square
		// viewport on a 16:9 buffer made every lighting pass read the whole
		// gbuffer squashed into the capture's width - the capture's own pixels
		// mixed with whatever the main camera left in the rest of it, which is
		// why captures came out as vertical bands of smeared room.
		//
		// So render full-size with the buffer's aspect and bank the CENTRED
		// square, which with a 90-degree vertical FOV is exactly the 90x90 face
		// the prefilter expects. The render target stays square because that
		// square is all we keep - see the copy in
		// SceneRenderer::RenderPostProcessing's environment-capture branch.
		uint32_t bbW = 0, bbH = 0;
		g_pEnv->_graphicsDevice->GetBackBufferDimensions(bbW, bbH);
		const float square = (float)std::min(bbW, bbH);
		_rigCamera->SetPespectiveParameters(90.0f, (float)bbW / (float)bbH, 0.1f, 1000.0f);
		_rigCamera->SetViewportWithTargetSize(
			math::Viewport(0.0f, 0.0f, (float)bbW, (float)bbH, 0.0f, 1.0f),
			(int32_t)square, (int32_t)square);
		// Marks this as an offline capture: the renderer skips SSR/NRD and TAA
		// for it, and won't sample reflection probes into the capture. Without
		// this the NRD denoiser asserts - its buffers are main-camera sized while
		// this camera is 256px, and the jitter conversion between the two scales
		// a half-pixel jitter to 7.5 pixels.
		_rigCamera->SetEnvironmentCapture(true);

		// Seed the rig camera's PVS with everything already in the scene.
		//
		// Scene::FlushPVS pushes an entity into every camera's PVS at the moment
		// that ENTITY is added. A camera created later - like this rig, in a
		// fully-loaded scene - therefore starts with an empty PVS and only ever
		// receives entities added after it. It renders nothing: the gbuffer stays
		// empty and lighting resolves to flat ambient, which is exactly what the
		// captured faces looked like (a different flat tone per direction, no
		// geometry at all, in a room with floor-to-ceiling windows).
		//
		// PVS::ForceRebuild is not enough - it only sets a rebuild flag and
		// re-evaluates the entities the PVS already knows about, which for a fresh
		// camera is none. The engine's other offscreen-capture path (IconService)
		// hits the same wall and solves it by seeding the PVS explicitly.
		if (auto* pvs = _rigCamera->GetPVS(); pvs != nullptr)
		{
			int32_t seeded = 0;
			for (const auto& [signature, entities] : scene->GetEntities())
			{
				for (auto* e : entities)
				{
					if (e != nullptr && e != _rigEntity)
					{
						pvs->AddEntity(e);
						++seeded;
					}
				}
			}
			pvs->ForceRebuild();
			LOG_INFO("ReflectionProbe '%s': seeded rig PVS with %d entities",
				GetEntity()->GetName().c_str(), seeded);
		}
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

	ITexture2D* ReflectionProbeComponent::EnsureShTex()
	{
		if (_shTex == nullptr)
		{
			// 1 x 9 L2 SH coefficients - must match ENVMAP_SH_COEFFS in
			// EnvMapCommon.shader and SceneRenderer's kIblEnvShCoeffs.
			_shTex = g_pEnv->_graphicsDevice->CreateTexture2D(
				1, 9,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				1,
				D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
				1);
			if (_shTex != nullptr)
				_shTex->SetDebugName("ReflectionProbeSH");
		}
		return _shTex;
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
				// Camera lost its target (device reset?) - abort and retry. Release
				// the bake slot too, or one failed capture would block every other
				// probe in the scene forever.
				_pendingFace = -1;
				_captureRequested = true;
				if (s_captureInFlight == this)
					s_captureInFlight = nullptr;
				return;
			}

			// Downsample the full-resolution capture into a small face texture. A
			// straight CopyTo can't do this (CopyResource needs matching sizes);
			// BlendTo_Additive draws a fullscreen quad into the destination, which
			// rescales - the same pattern IconService uses to shrink a rendered
			// frame into an icon.
			if (_faces[_pendingFace] == nullptr)
			{
				_faces[_pendingFace] = g_pEnv->_graphicsDevice->CreateTexture2D(
					kProbeFaceSize,
					kProbeFaceSize,
					DXGI_FORMAT_R16G16B16A16_FLOAT,
					1,
					D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,
					1);
				if (_faces[_pendingFace] != nullptr)
					_faces[_pendingFace]->SetDebugName("ReflectionProbeFace");
			}
			if (_faces[_pendingFace] != nullptr)
			{
				_faces[_pendingFace]->ClearRenderTargetView(math::Color(0, 0, 0, 0));

				// The viewport has to be the FACE's size before the downsample.
				//
				// BlendTo_Additive binds the destination and draws a quad that
				// spans NDC, but it never touches the viewport - so the draw uses
				// whatever was left bound, which during a game-update tick is the
				// main camera's full-size viewport. A full-NDC quad rasterized
				// with a 3840x2071 viewport into a 256x256 target keeps only the
				// part that lands inside the target, so the face received the top
				// 256/3840 x 256/2071 - about 6.7% x 12.4% - of the capture,
				// stretched, instead of the whole thing downsampled. That is the
				// "flat wash that varies only slightly per direction" this file's
				// EnsureRig comment describes; those two percentages are literally
				// the ratios quoted there. Enlarging the capture viewport made the
				// crop bigger without making it a downsample, so the faces stayed
				// unusable and the prefiltered atlas stayed ~0.003 - dark enough
				// that an indoor probe contributed nothing at all.
				D3D11_VIEWPORT faceViewport;
				faceViewport.TopLeftX = 0.0f;
				faceViewport.TopLeftY = 0.0f;
				faceViewport.Width = (float)kProbeFaceSize;
				faceViewport.Height = (float)kProbeFaceSize;
				faceViewport.MinDepth = 0.0f;
				faceViewport.MaxDepth = 1.0f;
				g_pEnv->_graphicsDevice->SetViewport(faceViewport);

				rt->BlendTo_Additive(_faces[_pendingFace]);
			}

			// Capture-chain dump: the rig's render target (what the scene render
			// + post chain produced) next to the downsampled face. If the RT has
			// the room in it and the face doesn't, the downsample is at fault; if
			// neither has it, the capture render is.
			if (r_iblProbeDumpCapture._val.b)
			{
				const std::string tag = std::to_string(_pendingFace);
				try { rt->SaveToFile(fs::path("probe_dump_rt_" + tag + ".png")); }
				catch (const std::exception& e) { LOG_WARN("probe dump: rt face %s failed: %s", tag.c_str(), e.what()); }
				if (_faces[_pendingFace] != nullptr)
				{
					try { _faces[_pendingFace]->SaveToFile(fs::path("probe_dump_face_" + tag + ".png")); }
					catch (const std::exception& e) { LOG_WARN("probe dump: face %s failed: %s", tag.c_str(), e.what()); }
				}
			}

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
				// Release the scene-wide bake slot so the next probe can start.
				if (s_captureInFlight == this)
					s_captureInFlight = nullptr;
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
			// Bake throttle: one probe at a time, scene-wide. Six full-resolution
			// renders each means an unthrottled load-time recapture of N probes
			// would stack N of them into the same frames. Wait our turn instead.
			if (s_captureInFlight != nullptr && s_captureInFlight != this)
				return;

			LOG_INFO("ReflectionProbe '%s': starting 6-face capture", GetEntity()->GetName().c_str());
			_captureRequested = false;
			s_captureInFlight = this;
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
