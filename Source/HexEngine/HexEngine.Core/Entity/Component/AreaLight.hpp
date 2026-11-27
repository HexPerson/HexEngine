#pragma once

#include "Light.hpp"
#include "../../Graphics/ITexture2D.hpp"

namespace HexEngine
{
	class Camera;

	// Physically-shaped local light: a tube (fluorescent tube, neon strip) or a
	// rectangle (window, panel, screen, softbox).
	//
	// Shaded on the clustered path only (ClusterLightApply, the forward/transparent
	// path and the froxel fog): Karis-style representative-point specular plus
	// closed-form diffuse - see PBRutils.shader's area-light section. Unshadowed in
	// this version, so "Casts shadows" is not offered and is forced off.
	//
	// Axes follow the entity: width runs along Right (the tube's axis), height
	// along Up, and a rect emits along Forward (both ways when two-sided).
	// Radius (the base Light range) is measured from the SHAPE, not the centre,
	// so a long tube lights its whole length evenly.
	//
	// A rect can take an image (TV screen, stained glass, softbox pattern): it
	// modulates the emitted colour across the rectangle. Glossy reflections show
	// the picture (sharp on mirrors, blurred with roughness), diffuse light and fog
	// pick up its local / average colour. Seen from the emitting side the image
	// reads the right way round (u runs along -Right, v down along -Up). The
	// image multiplies the colour, so a dark image dims the light.
	//
	// Luminous flux (physical units) is the total emitted: a tube is spread over
	// the full sphere like a point light (lm / 4pi), a one-sided rect over a
	// hemisphere as a cosine emitter (lm / pi), a two-sided rect splits it (lm / 2pi).
	class HEX_API AreaLight : public Light
	{
	public:
		enum class Shape : int32_t
		{
			Tube = 0,
			Rect = 1,
		};

		CREATE_COMPONENT_ID(AreaLight);

		AreaLight(Entity* entity);

		AreaLight(Entity* entity, AreaLight* copy);

		virtual void SetDoesCastShadows(bool enabled) override;

		// Shadow interface: area lights cast no shadows yet, so these return
		// empty state and nothing ever renders a map for them.
		virtual ShadowMap* GetShadowMap(int32_t index = 0) const override { return nullptr; }
		virtual const math::Matrix& GetViewMatrix(uint32_t index = 0) const override { return _identity; }
		virtual const math::Matrix& GetProjectionMatrix(uint32_t index = 0) const override { return _identity; }
		virtual const dx::BoundingSphere& GetLightBoundingSphere(int32_t index = 0) const override { return _boundingSphere; }
		virtual const dx::BoundingFrustum& GetLightBoundingFrustum(int32_t index) const override { return _boundingFrustum; }
		virtual void ConstructMatrices(Camera* camera, float zMin, float zMax, int32_t cascadeIdx) override {}

		virtual void OnRenderEditorGizmo(bool isSelected, bool& isHovering) override;

		virtual bool CreateWidget(ComponentWidget* widget) override;

		virtual void Serialize(json& data, JsonFile* file) override;

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		Shape GetShape() const { return _shape; }
		void SetShape(Shape shape) { _shape = shape; }

		// Tube length / rect width (metres, along Right).
		float GetWidth() const { return _width; }
		void SetWidth(float width);

		// Rect height (metres, along Up). Unused by tubes.
		float GetHeight() const { return _height; }
		void SetHeight(float height);

		// Tube cross-section radius (metres). Unused by rects.
		float GetTubeRadius() const { return _tubeRadius; }
		void SetTubeRadius(float radius);

		bool GetTwoSided() const { return _twoSided; }
		void SetTwoSided(bool twoSided) { _twoSided = twoSided; }

		// Rect image (null = uniform colour). Tubes ignore it.
		const std::shared_ptr<ITexture2D>& GetTexture() const { return _texture; }
		void SetTexture(const std::shared_ptr<ITexture2D>& texture);
		// 8-bit images are usually sRGB-encoded and are linearised when sampled.
		bool GetTextureIsSRGB() const { return _textureIsSRGB; }
		void SetTextureIsSRGB(bool srgb) { _textureIsSRGB = srgb; }
		// Re-sample the image every frame (render targets, video); otherwise only
		// when the texture changes.
		bool GetTextureIsLive() const { return _textureIsLive; }
		void SetTextureIsLive(bool live) { _textureIsLive = live; }

		// World-space half-extent vectors of the shape: halfWidth along Right and
		// halfHeight along Up (zero for a tube), from the entity's world transform.
		void GetWorldAxes(math::Vector3& centre, math::Vector3& halfWidthAxis, math::Vector3& halfHeightAxis,
			math::Vector3& facing) const;

		// Bounding radius of everything the light can reach: range + shape extent.
		float GetBoundingRadius() const;

	private:
		Shape _shape = Shape::Tube;
		float _width = 1.2f;
		float _height = 0.6f;
		float _tubeRadius = 0.02f;
		bool _twoSided = false;

		std::shared_ptr<ITexture2D> _texture;
		fs::path _texturePath;
		bool _textureIsSRGB = true;
		bool _textureIsLive = false;

		math::Matrix _identity = math::Matrix::Identity;
		dx::BoundingSphere _boundingSphere;
		dx::BoundingFrustum _boundingFrustum;
	};
}
