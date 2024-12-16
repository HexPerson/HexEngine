

#pragma once

#include "../Required.hpp"
#include "../Graphics/ITexture3D.hpp"
#include "../Entity/Entity.hpp"

#if 0
namespace HexEngine
{
	class CloudVolume
	{
	public:
		CloudVolume(const math::Vector3& mins, const math::Vector3& maxs, const int32_t resolution);
		~CloudVolume();


		bool Generate();

	private:
		math::Vector3 _mins;
		math::Vector3 _maxs;
		ITexture3D* _texture;
		dx::BoundingBox _bbox;
		int32_t _resolution;
		Entity* _entity;
		IShader* _shader;
	};
}
#endif