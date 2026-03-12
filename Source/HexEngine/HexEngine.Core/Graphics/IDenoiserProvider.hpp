#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	struct DenoiserFrameData
	{
		Camera* camera = nullptr;
		math::Vector2 jitter;
		ITexture2D* signalInput = nullptr;
		ITexture2D* hitDistance = nullptr;
		ITexture2D* normalAndDepth = nullptr;
		ITexture2D* material = nullptr;
		ITexture2D* motionVectors = nullptr;
	};

	class IDenoiserProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IDenoiserProvider, 001);

		virtual void CreateBuffers(int32_t width, int32_t height, ITexture2D* signalInput, ITexture2D* hitDistance, ITexture2D* normalAndDepth, ITexture2D* material, ITexture2D* motionVectors) = 0;

		virtual void BuildFrameData(DenoiserFrameData& fd, ITexture2D* signalInput, ITexture2D* hitDistance, ITexture2D* normalAndDepth, ITexture2D* material, ITexture2D* motionVectors) = 0;

		virtual void FilterFrame(const DenoiserFrameData& fd, ITexture2D* output) = 0;
	};
}
