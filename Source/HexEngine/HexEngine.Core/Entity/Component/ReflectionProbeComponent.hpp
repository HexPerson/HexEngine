

#pragma once

#include "UpdateComponent.hpp"

namespace HexEngine
{
	class Camera;
	class Entity;
	class ITexture2D;

	// Local reflection probe (IBL step 2). Captures the scene from the owning
	// entity's position into a prefiltered octahedral environment atlas (same
	// layout as the sky atlas - see EnvMapCommon.shader), which the deferred
	// IBL term samples with box projection for pixels inside the probe's box.
	//
	// This is the piece that makes environment reflections correct indoors: a
	// probe captured inside a room KNOWS the walls occlude the sky, so sky
	// radiance can no longer leak through solid geometry - the failure mode
	// every screen-space heuristic hit (see r_ssrSkyFallbackStrength's comment
	// in SceneRenderer.cpp for that history).
	//
	// Capture rides the engine's existing secondary-camera path: the component
	// owns a hidden rig entity with a 90-degree square camera that
	// SceneManager renders like any other RendersToTarget camera - one cube
	// face per frame, six frames per capture. No re-entrant scene rendering,
	// no new engine hooks. After the sixth face the SceneRenderer's
	// RenderProbeEnvMaps pass converts + GGX-prefilters the faces into the
	// probe's atlas (ProbeEnvMap.shader).
	//
	// Captured data is NOT serialized; probes recapture automatically after
	// load. Box half-extents are authored on the component; the box is centred
	// on and axis-aligned with the world (v1: no rotated probe boxes).
	class HEX_API ReflectionProbeComponent : public UpdateComponent
	{
	public:
		CREATE_COMPONENT_ID(ReflectionProbeComponent);

		ReflectionProbeComponent(Entity* entity);
		ReflectionProbeComponent(Entity* entity, ReflectionProbeComponent* copy);
		virtual ~ReflectionProbeComponent();

		virtual void Update(float frameTime) override;

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;

		// Queue a fresh 6-face capture starting next frame.
		void RequestCapture() { _captureRequested = true; }

		// Prefiltered octahedral atlas, or null until the first capture + prefilter
		// completes. Owned by this component.
		ITexture2D* GetEnvAtlas() const { return _envAtlas; }

		// True once all six faces are captured and waiting for (or done with) the
		// prefilter pass.
		bool HasCapture() const { return _facesComplete; }

		// Set by RenderProbeEnvMaps once the atlas holds prefiltered data.
		bool IsAtlasReady() const { return _atlasReady; }

		// Faces awaiting prefilter. The renderer consumes these and calls
		// MarkAtlasPrefiltered().
		bool IsAtlasDirty() const { return _atlasDirty; }
		ITexture2D* GetFace(int32_t index) const { return _faces[index]; }
		ITexture2D* EnsureEnvAtlas();
		void MarkAtlasPrefiltered() { _atlasDirty = false; _atlasReady = true; }

		const math::Vector3& GetExtents() const { return _extents; }
		void SetExtents(const math::Vector3& e) { _extents = e; }

		bool GetBoxProjection() const { return _boxProjection; }
		void SetBoxProjection(bool v) { _boxProjection = v; }

		// World-space probe centre (the owning entity's world position at the
		// last capture, or current position if never captured).
		math::Vector3 GetWorldCentre() const;

		// Capture-camera view directions and ups, shared with ProbeEnvMap.shader's
		// face basis (the shader mirrors these exactly - keep in sync).
		static const math::Vector3 kFaceDirs[6];
		static const math::Vector3 kFaceUps[6];

	private:
		void EnsureRig();
		void OrientRigForFace(int32_t face);
		void DestroyRig();

		// Authored.
		math::Vector3 _extents = math::Vector3(10.0f, 5.0f, 10.0f); // half-extents, metres
		bool _boxProjection = true;

		// Capture state machine. _pendingFace is the face the rig camera is
		// currently oriented for (rendered by the secondary-camera pass this
		// frame, copied out next Update). -1 = idle.
		bool _captureRequested = true; // capture automatically on first update after load
		// Frames to let the freshly-created rig camera update itself (frustum +
		// PVS) before the renderer draws it. See Update().
		int32_t _warmupFrames = 0;
		int32_t _pendingFace = -1;
		bool _facesComplete = false;
		bool _atlasDirty = false;
		bool _atlasReady = false;

		Entity* _rigEntity = nullptr;
		Camera* _rigCamera = nullptr;

		ITexture2D* _faces[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
		ITexture2D* _envAtlas = nullptr;
	};
}
