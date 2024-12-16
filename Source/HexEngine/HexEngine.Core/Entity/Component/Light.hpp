

#pragma once

#include "BaseComponent.hpp"
#include "../../Graphics/ShadowMap.hpp"
#include "../../Scene/PVS.hpp"

namespace HexEngine
{
	class Camera;

	enum class LightingEffect
	{
		None,
		SlowRandomPulse,
	};

	enum class LightFlags : uint32_t
	{
		None					= 0,
		RebuildShadowMatrices	= HEX_BITSET(0),
		RebuildPVS				= HEX_BITSET(1)
	};

	DEFINE_ENUM_FLAG_OPERATORS(LightFlags);

	struct SlowRandomPulseEffect
	{
		float current = 1.0f;
		float target = 0.0f;
	};

	/// <summary>
	/// This is the maximum number of shadow maps a light can have, at most it corresponds to the 6 sides of a sphere a point light needs to represent
	/// </summary>
	const int32_t MaxLightShadowMaps = 6;

	class Light : public BaseComponent
	{
	public:
		Light(Entity* entity);

		Light(Entity* entity, Light* clone);

		virtual bool GetDoesCastShadows()
		{
			return _doesCastShadows;
		}

		virtual void SetDoesCastShadows(bool enabled);


		virtual const math::Matrix& GetViewMatrix(uint32_t index = 0) const = 0;

		virtual const math::Matrix& GetProjectionMatrix(uint32_t index = 0) const = 0;

		virtual const math::Matrix& GetViewMatrixPrev(uint32_t index = 0) const;

		virtual const math::Matrix& GetProjectionMatrixPrev(uint32_t index = 0) const;

		virtual const dx::BoundingSphere& GetLightBoundingSphere(int32_t index = 0) const = 0;

		virtual const dx::BoundingFrustum& GetLightBoundingFrustum(int32_t index) const = 0;

		virtual void ConstructMatrices(Camera* camera, float zMin, float zMax, int32_t cascadeIdx) = 0;

		virtual PVS* GetPVS(int32_t index = 0);

		virtual ShadowMap* GetShadowMap(int32_t index = 0) const = 0;

		virtual int32_t GetMaxSupportedShadowCascades() const;		

		void SetLightingEffect(LightingEffect effect);

		void SetDiffuseColour(const math::Color& colour);

		math::Vector4 GetDiffuseColour() const;

		virtual void OnMessage(Message* message, MessageListener* sender) override;

		//void SetLightMultiplier(float value);
		//float GetLightMultiplier();

		void SetLightStength(float value);
		float GetLightStrength() const;

		void SetRadius(float radius);
		float GetRadius() const;

		void SetIsVolumetric(bool volumetric);
		bool GetIsVolumetric() const;

		LightFlags GetFlags() const;
		void SetFlag(LightFlags flag);
		void ClearFlag(LightFlags flag);

		virtual void Serialize(json& data, JsonFile* file) override;

		virtual void Deserialize(json& data, JsonFile* file, uint32_t mask = 0) override;

		virtual bool CreateWidget(ComponentWidget* widget) override;

	protected:
		LightFlags _flags = LightFlags::RebuildPVS;

	protected:
		bool _doesCastShadows = false;
		bool _isVolumetric = false;
		LightingEffect _effect = LightingEffect::None;

		float _strengthMultiplier = 1.0f;
		float _originalStrengthMultiplier = 1.0f;
		math::Vector4 _diffuseColour;

		// Effect data
		SlowRandomPulseEffect _slowRandomPulseEffect;

		//float _lightMultiplier = 1.0f;

		float _radius = 5.0f;

	protected:
		ShadowMap* _shadowMaps[MaxLightShadowMaps] = { nullptr };
		PVS _pvs[MaxLightShadowMaps];
	};
}
