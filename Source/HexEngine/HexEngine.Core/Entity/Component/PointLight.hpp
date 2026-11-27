

#pragma once

#include "Light.hpp"

namespace HexEngine
{
	class HEX_API PointLight : public Light
	{
	public:
		enum ShadowSides
		{
			PointLightShadowSides_Up,
			PointLightShadowSides_Down,
			PointLightShadowSides_Left,
			PointLightShadowSides_Right,
			PointLightShadowSides_Front,
			PointLightShadowSides_Back,
			PointLightShadowSides_Count
		};

		CREATE_COMPONENT_ID(PointLight);

		PointLight(Entity* entity);

		PointLight(Entity* entity, PointLight* clone);

		virtual void Destroy() override;

		virtual void SetDoesCastShadows(bool enabled) override;

		// Allocate the dedicated cube maps (legacy shadow path only - the
		// point shadow atlas needs none).
		virtual void EnsureShadowMaps() override;
		virtual void ReleaseDedicatedShadowMaps() override;

		virtual int32_t GetMaxSupportedShadowCascades() const override { return PointLightShadowSides_Count; }

		virtual ShadowMap* GetShadowMap(int32_t index = 0) const override;

		virtual const math::Matrix& GetViewMatrix(uint32_t index = 0) const { return _viewMatrix[index]; };

		virtual const math::Matrix& GetProjectionMatrix(uint32_t index = 0) const { return _projectionMatrix[index]; };

		virtual const dx::BoundingSphere& GetLightBoundingSphere(int32_t index) const { return _lightBoundingSphere[index]; }

		virtual const dx::BoundingFrustum& GetLightBoundingFrustum(int32_t index) const { return _lightBoundingFrustum[index]; }

		virtual void ConstructMatrices(Camera* camera, float zMin, float zMax, int32_t cascadeIdx) override;

		

		virtual void Serialize(json& data, JsonFile* file) override;

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		const math::Matrix& GetViewMatrix(ShadowSides side) const;
		const math::Matrix& GetProjectionMatrix(ShadowSides side) const;

		virtual bool CreateWidget(ComponentWidget* widget) override;

		// Room clip box: when enabled the light only shades points inside the
		// world-axis box [pos + min, pos + max] whenever it is UNSHADOWED (the
		// clustered path). Interior lamps can't all afford cube shadow maps;
		// without this they light the outside faces of their walls. A shadowed
		// light ignores the box, so its light still spills out of windows.
		bool HasRoomClip() const { return _roomClip; }
		const math::Vector3& GetRoomClipMin() const { return _roomClipMin; }
		const math::Vector3& GetRoomClipMax() const { return _roomClipMax; }
		void SetRoomClip(bool enabled, const math::Vector3& minOffset, const math::Vector3& maxOffset)
		{
			_roomClip = enabled; _roomClipMin = minOffset; _roomClipMax = maxOffset;
		}

		// Volumetric fog contribution (0..1). The froxel grid is coarse (metres
		// deep at distance): a cell straddling a wall whose centre sits in a lit
		// room glows as a whole, so interior lamps show a fog ball through their
		// walls/roof. Interior lights turn this down.
		float GetFogScale() const { return _fogScale; }
		void SetFogScale(float scale) { _fogScale = scale; }

	private:
		bool _roomClip = false;
		float _fogScale = 1.0f;
		math::Vector3 _roomClipMin = math::Vector3(-1.0f, -1.0f, -1.0f);   // offsets from the light, world axes
		math::Vector3 _roomClipMax = math::Vector3(1.0f, 1.0f, 1.0f);

		ShadowMap* _shadowMaps[ShadowSides::PointLightShadowSides_Count] = { nullptr };

		math::Matrix _viewMatrix[ShadowSides::PointLightShadowSides_Count];
		math::Matrix _projectionMatrix[ShadowSides::PointLightShadowSides_Count];
		dx::BoundingSphere _lightBoundingSphere[6];
		dx::BoundingFrustum _lightBoundingFrustum[6];
	};
}
