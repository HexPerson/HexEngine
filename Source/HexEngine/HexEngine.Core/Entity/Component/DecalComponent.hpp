

#pragma once

#include "BaseComponent.hpp"
#include "../../Graphics/ITexture2D.hpp"

namespace HexEngine
{
	// Deferred projector decal. The owner entity's transform places an oriented box
	// in world space; pixels of the GBuffer that fall inside the box get their
	// diffuse / normal / material channels overwritten by the decal's textures.
	//
	// Use cases: puddles, blood, scorch marks, paint, graffiti, weathering, tyre
	// tracks. The decal is rendered in a dedicated pass between the opaque GBuffer
	// fill and the lighting pass, so lighting sees the modified surface naturally.
	//
	// Projection direction is local -Y (down). The decal's local Y-axis (after
	// applying the entity's rotation) should point INTO the surface being painted:
	// for puddles on flat ground that means leaving rotation at identity. The
	// component samples the GBuffer position to reconstruct world-space coords,
	// transforms them into local box space, and discards anything outside [-0.5,
	// 0.5] on each axis. UVs come from local XZ.
	//
	// Normal-cutoff angle (radians) lets the decal reject pixels whose underlying
	// surface normal differs too sharply from the projection direction - prevents
	// a puddle decal placed on the floor from "leaking" onto the side of a kerb.
	class HEX_API DecalComponent : public BaseComponent
	{
	public:
		CREATE_COMPONENT_ID(DecalComponent);

		DecalComponent(Entity* entity);
		DecalComponent(Entity* entity, DecalComponent* copy);

		virtual void Serialize(json& data, JsonFile* file) override;
		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;
		virtual bool CreateWidget(class ComponentWidget* widget) override;

		// Half-extents are taken from the owning Transform's scale; the decal box
		// is centred on the entity and projects along its local -Y axis.

		void SetAlbedoTexture(const std::shared_ptr<ITexture2D>& tex);
		void SetNormalTexture(const std::shared_ptr<ITexture2D>& tex);
		void SetMatTexture(const std::shared_ptr<ITexture2D>& tex);

		ITexture2D* GetAlbedoTexture() const { return _albedoTexture.get(); }
		ITexture2D* GetNormalTexture() const { return _normalTexture.get(); }
		ITexture2D* GetMatTexture() const     { return _matTexture.get(); }

		float GetOpacity() const { return _opacity; }
		void SetOpacity(float v) { _opacity = v; }

		// Cosine of the maximum angle between the underlying surface normal and the
		// decal's projection direction for which the decal still applies. 1.0 = only
		// perfectly-aligned surfaces; 0.0 = any surface (including back-facing). Default
		// 0.5 (~60deg) lets a puddle drape lightly over kerb edges without bleeding
		// onto vertical walls.
		float GetNormalCutoff() const { return _normalCutoff; }
		void SetNormalCutoff(float v) { _normalCutoff = v; }

		// Per-channel blend weights. 1.0 = decal fully replaces, 0.0 = decal leaves
		// the underlying GBuffer channel untouched. The texture's alpha is multiplied
		// on top - these are coarse "should this decal affect roughness at all?"
		// switches that artists can flip for e.g. blood (albedo only, leave normal
		// and mat alone) versus puddles (mostly mat + a touch of normal, no albedo).
		float GetAlbedoWeight() const { return _albedoWeight; }
		void SetAlbedoWeight(float v) { _albedoWeight = v; }
		float GetNormalWeight() const { return _normalWeight; }
		void SetNormalWeight(float v) { _normalWeight = v; }
		float GetMatWeight() const     { return _matWeight; }
		void SetMatWeight(float v)     { _matWeight = v; }

		// Override roughness / metallic when no mat texture is bound - lets puddle
		// decals tweak surface properties without authoring a dedicated mat texture.
		// Range [0, 1]. When _matTexture is present these are multiplied with it.
		float GetRoughnessOverride() const { return _roughnessOverride; }
		void SetRoughnessOverride(float v) { _roughnessOverride = v; }
		float GetMetallicOverride() const  { return _metallicOverride; }
		void SetMetallicOverride(float v)  { _metallicOverride = v; }

		// When true, the decal's effective opacity is multiplied by the global
		// weather puddleAmount (0..1) at draw time. Use for puddle decals so
		// they fade in/out with rain automatically; leave OFF for blood, paint,
		// scorch marks, graffiti and similar persistent decals that should
		// ignore the weather.
		bool GetRespondsToWeather() const { return _respondsToWeather; }
		void SetRespondsToWeather(bool v) { _respondsToWeather = v; }

		// Returns true when the decal has anything to render (at least one texture
		// bound OR a non-default mat override). Skip culled decals here so the
		// renderer doesn't enumerate them every frame.
		bool IsRenderable() const;

	private:
		std::shared_ptr<ITexture2D> _albedoTexture;
		std::shared_ptr<ITexture2D> _normalTexture;
		std::shared_ptr<ITexture2D> _matTexture;

		// Cached asset paths used for serialise + the editor widget's currently-bound
		// label. Updated on every SetXxxTexture call.
		fs::path _albedoPath;
		fs::path _normalPath;
		fs::path _matPath;

		float _opacity            = 1.0f;
		float _normalCutoff       = 0.5f;
		float _albedoWeight       = 1.0f;
		float _normalWeight       = 1.0f;
		float _matWeight          = 1.0f;
		float _roughnessOverride  = 0.05f; // smooth - puddles default
		float _metallicOverride   = 0.0f;
		bool  _respondsToWeather  = false;
	};
}
