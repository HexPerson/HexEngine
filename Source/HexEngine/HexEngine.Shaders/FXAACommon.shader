"Global"
{
    // Copyright (c) 2011 NVIDIA Corporation. All rights reserved.
//
// TO  THE MAXIMUM  EXTENT PERMITTED  BY APPLICABLE  LAW, THIS SOFTWARE  IS PROVIDED
// *AS IS*  AND NVIDIA AND  ITS SUPPLIERS DISCLAIM  ALL WARRANTIES,  EITHER  EXPRESS
// OR IMPLIED, INCLUDING, BUT NOT LIMITED  TO, NONINFRINGEMENT,IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL  NVIDIA 
// OR ITS SUPPLIERS BE  LIABLE  FOR  ANY  DIRECT, SPECIAL,  INCIDENTAL,  INDIRECT,  OR  
// CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT LIMITATION,  DAMAGES FOR LOSS 
// OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY 
// OTHER PECUNIARY LOSS) ARISING OUT OF THE  USE OF OR INABILITY  TO USE THIS SOFTWARE, 
// EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
//
// Please direct any bugs or questions to SDKFeedback@nvidia.com

#define FXAA_HLSL_4 1


/*============================================================================

                                    FXAA

============================================================================*/

/*============================================================================
                                 API PORTING
============================================================================*/
#ifndef     FXAA_GLSL_120
#define FXAA_GLSL_120 0
#endif
#ifndef     FXAA_GLSL_130
#define FXAA_GLSL_130 0
#endif
#ifndef     FXAA_HLSL_3
#define FXAA_HLSL_3 0
#endif
#ifndef     FXAA_HLSL_4
#define FXAA_HLSL_4 0
#endif    
/*--------------------------------------------------------------------------*/
#if FXAA_GLSL_120
    // Requires,
    //  #version 120
    //  #extension GL_EXT_gpu_shader4 : enable
#define int2 ivec2
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define FxaaBool3 bvec3
#define FxaaInt2 ivec2
#define FxaaFloat2 vec2
#define FxaaFloat3 vec3
#define FxaaFloat4 vec4
#define FxaaBool2Float(a) mix(0.0, 1.0, (a))
#define FxaaPow3(x, y) pow(x, y)
#define FxaaSel3(f, t, b) mix((f), (t), (b))
#define FxaaTex sampler2D
#endif
/*--------------------------------------------------------------------------*/
#if FXAA_GLSL_130
    // Requires "#version 130" or better
#define int2 ivec2
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define FxaaBool3 bvec3
#define FxaaInt2 ivec2
#define FxaaFloat2 vec2
#define FxaaFloat3 vec3
#define FxaaFloat4 vec4
#define FxaaBool2Float(a) mix(0.0, 1.0, (a))
#define FxaaPow3(x, y) pow(x, y)
#define FxaaSel3(f, t, b) mix((f), (t), (b))
#define FxaaTex sampler2D
#endif
/*--------------------------------------------------------------------------*/
#if FXAA_HLSL_3
#define int2 float2
#define FxaaInt2 float2
#define FxaaFloat2 float2
#define FxaaFloat3 float3
#define FxaaFloat4 float4
#define FxaaBool2Float(a) (a)
#define FxaaPow3(x, y) pow(x, y)
#define FxaaSel3(f, t, b) ((f)*(!b) + (t)*(b))
#define FxaaTex sampler2D
#endif
/*--------------------------------------------------------------------------*/
#if FXAA_HLSL_4
#define FxaaInt2 int2
#define FxaaFloat2 float2
#define FxaaFloat3 float3
#define FxaaFloat4 float4
#define FxaaBool2Float(a) (a)
#define FxaaPow3(x, y) pow(x, y)
#define FxaaSel3(f, t, b) ((f)*(!b) + (t)*(b))
    struct FxaaTex { SamplerState smpl; Texture2D tex; };
#endif
    /*--------------------------------------------------------------------------*/
#define FxaaToFloat3(a) FxaaFloat3((a), (a), (a))
/*--------------------------------------------------------------------------*/
    float4 FxaaTexLod0(FxaaTex tex, float2 pos) {
#if FXAA_GLSL_120
        return texture2DLod(tex, pos.xy, 0.0);
#endif
#if FXAA_GLSL_130
        return textureLod(tex, pos.xy, 0.0);
#endif
#if FXAA_HLSL_3
        return tex2Dlod(tex, float4(pos.xy, 0.0, 0.0));
#endif
#if FXAA_HLSL_4
        return tex.tex.SampleLevel(tex.smpl, pos.xy, 0.0);
#endif
    }
    /*--------------------------------------------------------------------------*/
    float4 FxaaTexGrad(FxaaTex tex, float2 pos, float2 grad) {
#if FXAA_GLSL_120
        return texture2DGrad(tex, pos.xy, grad, grad);
#endif
#if FXAA_GLSL_130
        return textureGrad(tex, pos.xy, grad, grad);
#endif
#if FXAA_HLSL_3
        return tex2Dgrad(tex, pos.xy, grad, grad);
#endif
#if FXAA_HLSL_4
        return tex.tex.SampleGrad(tex.smpl, pos.xy, grad, grad);
#endif
    }
    /*--------------------------------------------------------------------------*/
    float4 FxaaTexOff(FxaaTex tex, float2 pos, int2 off, float2 rcpFrame) {
#if FXAA_GLSL_120
        return texture2DLodOffset(tex, pos.xy, 0.0, off.xy);
#endif
#if FXAA_GLSL_130
        return textureLodOffset(tex, pos.xy, 0.0, off.xy);
#endif
#if FXAA_HLSL_3
        return tex2Dlod(tex, float4(pos.xy + (off * rcpFrame), 0, 0));
#endif
#if FXAA_HLSL_4
        return tex.tex.SampleLevel(tex.smpl, pos.xy, 0.0, off.xy);
#endif
    }

    /*============================================================================
                                     SRGB KNOBS
    ------------------------------------------------------------------------------
    FXAA_SRGB_ROP - Set to 1 when applying FXAA to an sRGB back buffer (DX10/11).
                    This will do the sRGB to linear transform,
                    as ROP will expect linear color from this shader,
                    and this shader works in non-linear color.
    ============================================================================*/
#define FXAA_SRGB_ROP 0

    /*============================================================================
                                    DEBUG KNOBS
    ------------------------------------------------------------------------------
    All debug knobs draw FXAA-untouched pixels in FXAA computed luma (monochrome).

    FXAA_DEBUG_PASSTHROUGH - Red for pixels which are filtered by FXAA with a
                             yellow tint on sub-pixel aliasing filtered by FXAA.
    FXAA_DEBUG_HORZVERT    - Blue for horizontal edges, gold for vertical edges.
    FXAA_DEBUG_PAIR        - Blue/green for the 2 pixel pair choice.
    FXAA_DEBUG_NEGPOS      - Red/blue for which side of center of span.
    FXAA_DEBUG_OFFSET      - Red/blue for -/+ x, gold/skyblue for -/+ y.
    ============================================================================*/
#ifndef     FXAA_DEBUG_PASSTHROUGH
#define FXAA_DEBUG_PASSTHROUGH 0
#endif    
#ifndef     FXAA_DEBUG_HORZVERT
#define FXAA_DEBUG_HORZVERT    0
#endif    
#ifndef     FXAA_DEBUG_PAIR   
#define FXAA_DEBUG_PAIR        0
#endif    
#ifndef     FXAA_DEBUG_NEGPOS
#define FXAA_DEBUG_NEGPOS      0
#endif
#ifndef     FXAA_DEBUG_OFFSET
#define FXAA_DEBUG_OFFSET      0
#endif    
    /*--------------------------------------------------------------------------*/
#if FXAA_DEBUG_PASSTHROUGH || FXAA_DEBUG_HORZVERT || FXAA_DEBUG_PAIR
#define FXAA_DEBUG 1
#endif    
#if FXAA_DEBUG_NEGPOS || FXAA_DEBUG_OFFSET
#define FXAA_DEBUG 1
#endif
#ifndef FXAA_DEBUG
#define FXAA_DEBUG 0
#endif

/*============================================================================
                              COMPILE-IN KNOBS
------------------------------------------------------------------------------
FXAA_PRESET - Choose compile-in knob preset 0-5.
------------------------------------------------------------------------------
FXAA_EDGE_THRESHOLD - The minimum amount of local contrast required
                      to apply algorithm.
                      1.0/3.0  - too little
                      1.0/4.0  - good start
                      1.0/8.0  - applies to more edges
                      1.0/16.0 - overkill
------------------------------------------------------------------------------
FXAA_EDGE_THRESHOLD_MIN - Trims the algorithm from processing darks.
                          Perf optimization.
                          1.0/32.0 - visible limit (smaller isn't visible)
                          1.0/16.0 - good compromise
                          1.0/12.0 - upper limit (seeing artifacts)
------------------------------------------------------------------------------
FXAA_SEARCH_STEPS - Maximum number of search steps for end of span.
------------------------------------------------------------------------------
FXAA_SEARCH_ACCELERATION - How much to accelerate search,
                           1 - no acceleration
                           2 - skip by 2 pixels
                           3 - skip by 3 pixels
                           4 - skip by 4 pixels
------------------------------------------------------------------------------
FXAA_SEARCH_THRESHOLD - Controls when to stop searching.
                        1.0/4.0 - seems to be the best quality wise
------------------------------------------------------------------------------
FXAA_SUBPIX_FASTER - Turn on lower quality but faster subpix path.
                     Not recomended, but used in preset 0.
------------------------------------------------------------------------------
FXAA_SUBPIX - Toggle subpix filtering.
              0 - turn off
              1 - turn on
              2 - turn on full (ignores FXAA_SUBPIX_TRIM and CAP)
------------------------------------------------------------------------------
FXAA_SUBPIX_TRIM - Controls sub-pixel aliasing removal.
                   1.0/2.0 - low removal
                   1.0/3.0 - medium removal
                   1.0/4.0 - default removal
                   1.0/8.0 - high removal
                   0.0 - complete removal
------------------------------------------------------------------------------
FXAA_SUBPIX_CAP - Insures fine detail is not completely removed.
                  This is important for the transition of sub-pixel detail,
                  like fences and wires.
                  3.0/4.0 - default (medium amount of filtering)
                  7.0/8.0 - high amount of filtering
                  1.0 - no capping of sub-pixel aliasing removal
============================================================================*/
#ifndef FXAA_PRESET
#define FXAA_PRESET 6
#endif
/*--------------------------------------------------------------------------*/
#if (FXAA_PRESET == 0)
#define FXAA_EDGE_THRESHOLD      (1.0/4.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/12.0)
#define FXAA_SEARCH_STEPS        2
#define FXAA_SEARCH_ACCELERATION 4
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX              1
#define FXAA_SUBPIX_FASTER       1
#define FXAA_SUBPIX_CAP          (2.0/3.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#endif
/*--------------------------------------------------------------------------*/
#if (FXAA_PRESET == 1)
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/16.0)
#define FXAA_SEARCH_STEPS        4
#define FXAA_SEARCH_ACCELERATION 3
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX              1
#define FXAA_SUBPIX_FASTER       0
#define FXAA_SUBPIX_CAP          (3.0/4.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#endif
/*--------------------------------------------------------------------------*/
#if (FXAA_PRESET == 2)
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)
#define FXAA_SEARCH_STEPS        8
#define FXAA_SEARCH_ACCELERATION 2
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX              1
#define FXAA_SUBPIX_FASTER       0
#define FXAA_SUBPIX_CAP          (3.0/4.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#endif
/*--------------------------------------------------------------------------*/
#if (FXAA_PRESET == 3)
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)
#define FXAA_SEARCH_STEPS        16
#define FXAA_SEARCH_ACCELERATION 1
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX              1
#define FXAA_SUBPIX_FASTER       0
#define FXAA_SUBPIX_CAP          (3.0/4.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#endif
/*--------------------------------------------------------------------------*/
#if (FXAA_PRESET == 4)
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)
#define FXAA_SEARCH_STEPS        24
#define FXAA_SEARCH_ACCELERATION 1
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX              1
#define FXAA_SUBPIX_FASTER       0
#define FXAA_SUBPIX_CAP          (3.0/4.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#endif
/*--------------------------------------------------------------------------*/
#if (FXAA_PRESET == 5)
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)
#define FXAA_SEARCH_STEPS        32
#define FXAA_SEARCH_ACCELERATION 1
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX              1
#define FXAA_SUBPIX_FASTER       0
#define FXAA_SUBPIX_CAP          (3.0/4.0)
#define FXAA_SUBPIX_TRIM         (1.0/4.0)
#endif
/*--------------------------------------------------------------------------*/
#if (FXAA_PRESET == 6)
#define FXAA_EDGE_THRESHOLD      (1.0/8.0)
#define FXAA_EDGE_THRESHOLD_MIN  (1.0/24.0)
#define FXAA_SEARCH_STEPS        64
#define FXAA_SEARCH_ACCELERATION 1
#define FXAA_SEARCH_THRESHOLD    (1.0/4.0)
#define FXAA_SUBPIX              1
#define FXAA_SUBPIX_FASTER       0
#define FXAA_SUBPIX_CAP          (7.0/8.0)
#define FXAA_SUBPIX_TRIM         0//(1.0/8.0)
#endif
/*--------------------------------------------------------------------------*/
#define FXAA_SUBPIX_TRIM_SCALE (1.0/(1.0 - FXAA_SUBPIX_TRIM))

/*============================================================================
                                   HELPERS
============================================================================*/
// Return the luma, the estimation of luminance from rgb inputs.
// This approximates luma using one FMA instruction,
// skipping normalization and tossing out blue.
// FxaaLuma() will range 0.0 to 2.963210702.
    float FxaaLuma(float3 rgb) {
        return rgb.y * (0.587 / 0.299) + rgb.x;
    }
    /*--------------------------------------------------------------------------*/
    float3 FxaaLerp3(float3 a, float3 b, float amountOfA) {
        return (FxaaToFloat3(-amountOfA) * b) +
            ((a * FxaaToFloat3(amountOfA)) + b);
    }
    /*--------------------------------------------------------------------------*/
    // Support any extra filtering before returning color.
    float4 FxaaFilterReturn(float3 rgb) {
#if FXAA_SRGB_ROP
        // Do sRGB encoded value to linear conversion.
        return FxaaSel3(
            rgb * FxaaToFloat3(1.0 / 12.92),
            FxaaPow3(
                rgb * FxaaToFloat3(1.0 / 1.055) + FxaaToFloat3(0.055 / 1.055),
                FxaaToFloat3(2.4)),
            rgb > FxaaToFloat3(0.04045));
#else
        return float4(rgb, 1.0f);
#endif
    }
}
