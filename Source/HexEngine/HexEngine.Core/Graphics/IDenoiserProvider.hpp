#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	struct DenoiserFrameData
	{
		Camera* camera = nullptr;
		math::Vector2 jitter;
		ITexture2D* diffuseSignalInput = nullptr;
		ITexture2D* diffuseHitDistance = nullptr;
		ITexture2D* specularSignalInput = nullptr;
		ITexture2D* specularHitDistance = nullptr;
		ITexture2D* normalAndDepth = nullptr;
		ITexture2D* material = nullptr;
		ITexture2D* motionVectors = nullptr;
	};

	class IDenoiserProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IDenoiserProvider, 001);

		virtual void CreateBuffers(int32_t width, int32_t height, ITexture2D* diffuseSignalInput, ITexture2D* diffuseHitDistance, ITexture2D* specularSignalInput, ITexture2D* specularHitDistance, ITexture2D* normalAndDepth, ITexture2D* material, ITexture2D* motionVectors) = 0;

		virtual void BuildFrameData(DenoiserFrameData& fd, ITexture2D* diffuseSignalInput, ITexture2D* diffuseHitDistance, ITexture2D* specularSignalInput, ITexture2D* specularHitDistance, ITexture2D* normalAndDepth, ITexture2D* material, ITexture2D* motionVectors) = 0;

		virtual void FilterFrame(const DenoiserFrameData& fd, ITexture2D* output) = 0;
	};
}
