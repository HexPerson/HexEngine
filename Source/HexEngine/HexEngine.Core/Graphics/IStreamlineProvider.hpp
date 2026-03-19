
#pragma once

#include "../Plugin/IPlugin.hpp"

namespace HexEngine
{
	/** @brief Streamline DLSS quality/performance mode. */
	enum class DLSSMode : uint32_t
	{
		Off,
		MaxPerformance,
		Balanced,
		MaxQuality,
		UltraPerformance,
		UltraQuality,
		DLAA,
		Count,
	};

	/** @brief Bitmask of Streamline features supported by the backend. */
	enum StreamlineFeature
	{
		DLSS	= HEX_BITSET(0),
		NRD		= HEX_BITSET(1),
	};

	/** @brief Per-frame camera and motion constants consumed by Streamline features. */
	struct StreamlineConstants
	{
        //! Specifies matrix transformation from the camera view to the clip space.
        math::Matrix cameraViewToClip;
        //! Specifies matrix transformation from the clip space to the camera view space.
        math::Matrix clipToCameraView;
        //! Optional - Specifies matrix transformation describing lens distortion in clip space.
        math::Matrix clipToLensClip;
        //! Specifies matrix transformation from the current clip to the previous clip space.
        //! clipToPrevClip = clipToView * viewToViewPrev * viewToClipPrev
        //! Sample code can be found in sl_matrix_helpers.h
        math::Matrix clipToPrevClip;
        //! Specifies matrix transformation from the previous clip to the current clip space.
        //! prevClipToClip = clipToPrevClip.inverse()
        math::Matrix prevClipToClip;

        //! Specifies pixel space jitter offset
        math::Vector2 jitterOffset;
        //! Specifies scale factors used to normalize motion vectors (so the values are in [-1,1] range)
        math::Vector2 mvecScale;
        //! Optional - Specifies camera pinhole offset if used.
        math::Vector2 cameraPinholeOffset;
        //! Specifies camera position in world space.
        math::Vector3 cameraPos;
        //! Specifies camera up vector in world space.
        math::Vector3 cameraUp;
        //! Specifies camera right vector in world space.
        math::Vector3 cameraRight;
        //! Specifies camera forward vector in world space.
        math::Vector3 cameraFwd;

        //! Specifies camera near view plane distance.
        float cameraNear = FLT_MIN;
        //! Specifies camera far view plane distance.
        float cameraFar = FLT_MAX;
        //! Specifies camera field of view in radians.
        float cameraFOV = ToRadian(90.0f);
        //! Specifies camera aspect ratio defined as view space width divided by height.
        float cameraAspectRatio = 1.0f;
        //! Specifies which value represents an invalid (un-initialized) value in the motion vectors buffer
        //! NOTE: This is only required if `cameraMotionIncluded` is set to false and SL needs to compute it.
        float motionVectorsInvalidValue = 0.0f;

        //! Specifies if depth values are inverted (value closer to the camera is higher) or not.
        bool depthInverted = false;
        //! Specifies if camera motion is included in the MVec buffer.
        bool cameraMotionIncluded = false;
        //! Specifies if motion vectors are 3D or not.
        bool motionVectors3D = false;
        //! Specifies if previous frame has no connection to the current one (i.e. motion vectors are invalid)
        bool reset = false;
        //! Specifies if orthographic projection is used or not.
        bool orthographicProjection = false;
        //! Specifies if motion vectors are already dilated or not.
        bool motionVectorsDilated = false;
        //! Specifies if motion vectors are jittered or not.
        bool motionVectorsJittered = false;

        //! Version 2 members:
        //! 
        //! Optional heuristic that specifies the minimum depth difference between two objects in screen-space.
        //! The units of the value are in linear depth units.
        //! Linear depth is computed as:
        //!     if depthInverted is false:  `lin_depth = 1 / (1 - depth)` 
        //!     if depthInverted is true:   `lin_depth = 1 / depth`
        //! 
        //! Although unlikely to need to be modified, smaller thresholds are useful when depth units are
        //! unusually compressed into a small dynamic range near 1.
        //! 
        //! If not specified, the default value is 40.0f.
        float minRelativeLinearDepthObjectSeparation = 40.0f;
	};

	/** @brief Plugin interface for NVIDIA Streamline integration (DLSS/NRD). */
	class IStreamlineProvider : public IPluginInterface
	{
	public:
		DECLARE_PLUGIN_INTERFACE(IStreamlineProvider, 001);

		/** @brief Returns whether Streamline is initialized and usable. */
		virtual bool IsEnabled() = 0;

		virtual HRESULT D3D11CreateDevice(
			IDXGIAdapter* pAdapter,
			D3D_DRIVER_TYPE DriverType,
			HMODULE Software,
			UINT Flags,
			const D3D_FEATURE_LEVEL* pFeatureLevels,
			UINT FeatureLevels,
			UINT SDKVersion,
			ID3D11Device** ppDevice,
			D3D_FEATURE_LEVEL* pFeatureLevel,
			ID3D11DeviceContext** ppImmediateContext) = 0;

		virtual HRESULT CreateDXGIFactory1(REFIID riid, _COM_Outptr_ void** ppFactory) = 0;

		/** @brief Queries optimal render resolution for the requested DLSS mode. */
		virtual bool QueryOptimalDLSSSettings(
			int32_t desiredWidth,
			int32_t desiredHeight,
			DLSSMode mode,
			int32_t& optimalWidth,
			int32_t& optimalHeight) = 0;

		/** @brief Sets active DLSS options for subsequent evaluations. */
		virtual void SetDLSSOptions(float sharpness, bool hdr, bool autoExposure, DLSSMode mode, int32_t optimalWidth, int32_t optimalHeight) = 0;

		/** @brief Binds per-frame input/output resources used by Streamline. */
        virtual void PrepareFrameResources(void* colourIn, void* colourOut, void* motionVectors, void* depth, void* cmdList) = 0;

		/** @brief Returns a bitmask of supported Streamline features. */
		virtual uint32_t GetSupportedFeaturesMask() = 0;

		/** @brief Sets per-frame camera and motion constants. */
        virtual void SetCommonConstants(const StreamlineConstants& constants) = 0;

		/** @brief Begins Streamline frame scope. */
        virtual void BeginFrame() = 0;

		/** @brief Ends Streamline frame scope. */
        virtual void EndFrame() = 0;

		/** @brief Evaluates one Streamline feature on the current command list. */
        virtual void EvaluateFeature(StreamlineFeature feature, void* cmdList) = 0;
	};
}
