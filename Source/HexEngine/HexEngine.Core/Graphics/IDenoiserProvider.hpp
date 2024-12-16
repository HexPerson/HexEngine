
#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	struct DenoiserFrameData
	{
		std::vector<float> colour;
		std::vector<float> normals;
		std::vector<float> albedo;
		Camera* camera;
		math::Vector2 jitter;
	};

	class IDenoiserProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IDenoiserProvider, 001);

		virtual void CreateBuffers(int32_t width, int32_t height, ITexture2D* beauty, ITexture2D* normals, ITexture2D* albedo) = 0;

		virtual void BuildFrameData(DenoiserFrameData& fd, ITexture2D* beauty, ITexture2D* normals, ITexture2D* albedo) = 0;

		virtual void FilterFrame(const DenoiserFrameData& fd, ITexture2D* beauty) = 0;
	};
}
