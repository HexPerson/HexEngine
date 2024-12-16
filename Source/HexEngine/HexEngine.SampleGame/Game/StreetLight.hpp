

#pragma once

#include "../../HexEngine.Core/Entity/BaseChunkEntity.hpp"
#include "../../HexEngine.Core/Entity/SpotLight.hpp"
#include "../../HexEngine.Core/Entity/Billboard.hpp"

namespace CityBuilder
{
	// {EB9C3D53-0164-446A-8E13-B2C336F64C03}
	DEFINE_HEX_GUID(StreetLightGUID,
		0xeb9c3d53, 0x164, 0x446a, 0x8e, 0x13, 0xb2, 0xc3, 0x36, 0xf6, 0x4c, 0x3);

	class StreetLight : public HexEngine::BaseChunkEntity
	{
	public:
		DEFINE_OBJECT_GUID(StreetLight);

		StreetLight();

		virtual void Create() override;
		virtual void Destroy() override;

		virtual void OnTransformChanged(bool scaleChanged, bool rotationChanged, bool translationChanged) override;

		IMPLEMENT_LOADER(StreetLight);

	private:
		math::Vector3 _lightOffset;
		HexEngine::SpotLight* _light = nullptr;
		HexEngine::Billboard* _billboard = nullptr;
	};
}
