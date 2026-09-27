"InputLayout"
{
	PosTexColour
}
"VertexShaderIncludes"
{
	UICommon
}
"PixelShaderIncludes"
{
	UICommon
	Global
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;
		output.position = input.position;
		output.texcoord = input.texcoord;
		return output;
	}
}
"PixelShader"
{
	// Phase D apply pass. Replaces the per-pixel ray-march in the legacy
	// VolumetricLighting.shader. Samples the integration volume produced
	// by VolumetricScatterIntegrate.shader at (uv, depth) and composites:
	//   final = beauty * transmittance + inscatter
	// Where transmittance and inscatter come from accumulated light scatter
	// from camera to the pixel through the participating-media froxel grid.
	//
	// The volume covers 0.1 m near to 256 m far with exp depth distribution:
	//   depth = 0.1 * pow(2560, w)
	// To invert (find w from depth):
	//   w = log(depth / 0.1) / log(2560)
	//   w = (log(depth) - log(0.1)) / log(2560)
	// Pixels beyond 256 m clamp to the last slice (full integration). The
	// aerial-perspective volume handles atmospheric scattering past that.

	GBUFFER_RESOURCE(0, 1, 2, 3, 4);
	Texture2D    g_beauty                     : register(t5);
	Texture3D    g_volumetricIntegrationLUT   : register(t6);
	// The REAL depth buffer (post-transparency). Water and other depth-
	// writing transparents write depth but no gbuffer, so keying the fog on
	// the OPAQUE gbuffer depth (nd.w) attenuated them at the depth of
	// whatever sat BEHIND them - a blizzard integrated fog all the way to
	// the seabed / sky far plane and flattened the water into the weather.
	Texture2D    g_sceneDepthTex              : register(t7);
	// GI bilateral-blurred voxel-occlusion AO (.r = occlusion, 1 = blocked).
	// Gated by g_giComposeParams.z; null-bound reads 0 = no occlusion.
	Texture2D    g_giAoTex                    : register(t8);
	SamplerState g_pointSampler               : register(s2);
	SamplerState g_linearSampler              : register(s4);

	static const float NEAR_PLANE_M = 0.1f;
	// Must match VolumetricScattering::kFarDepthM. Volume range is short
	// so the new "visibility=0 outside cascade" gating works - the haze
	// stops where the shadow data stops, instead of continuing as flat
	// fog across the whole 256m the volume used to span.
	static const float FAR_PLANE_M  = 384.0f;
	// Must match VolumetricScattering::kVolumeWidth/Height/Depth.
	static const float3 VOLUME_DIMS = float3(256.0f, 144.0f, 64.0f);

	// Smoothstep-corrected trilinear sampling ("smooth trilinear"). Raw
	// trilinear of a volume this coarse (one froxel covers ~15x15 screen
	// pixels at 1080p) produces piecewise-LINEAR gradients between cells -
	// visible as diamond/plateau cell structure wherever the volume holds a
	// high-frequency signal (the emissive neon-sign glow especially; sun
	// shafts are too low-frequency to show it). Re-mapping the fractional
	// texel coordinate through a smoothstep before the hardware tap makes
	// the reconstruction C1-continuous: same single tap, same data, but
	// gradients now ease in/out across cell boundaries instead of kinking
	// at them. Standard low-res-volume upsampling trick.
	float4 SampleVolumeSmooth(Texture3D vol, SamplerState samp, float3 uvw)
	{
		const float3 coordTexels = uvw * VOLUME_DIMS - 0.5f;
		const float3 base = floor(coordTexels);
		const float3 f = coordTexels - base;
		const float3 fSmooth = f * f * (3.0f - 2.0f * f);
		const float3 uvwSmooth = (base + 0.5f + fSmooth) / VOLUME_DIMS;
		return vol.SampleLevel(samp, uvwSmooth, 0);
	}

	float DepthToVolumeW(float depthM)
	{
		// Inverse of exp distribution. Clamp to [0,1] so beyond-range
		// pixels sample the last slice.
		const float ratio = FAR_PLANE_M / NEAR_PLANE_M;          // 2560
		const float w = log(max(depthM, NEAR_PLANE_M) / NEAR_PLANE_M) / log(ratio);
		return saturate(w);
	}

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		const float2 uv = input.texcoord;

		const float4 beauty = g_beauty.Sample(g_pointSampler, uv);
		const float4 diff   = GBUFFER_DIFFUSE.Sample(g_pointSampler, uv);
		const float4 nd     = GBUFFER_NORMAL.Sample(g_pointSampler, uv);

		// Sky / no-geometry detection. SkySphere writes diff.a < -0.5 or
		// nd.w <= 0. For SKY pixels we still want LOCAL light inscatter
		// (a streetlamp's beam shafting up into the night sky should
		// remain visible past the building silhouette), but we don't
		// want to attenuate the sky colour itself by the fog
		// transmittance - the sky's atmospheric scattering is already
		// baked in via the SkyView LUT, and multiplying it down by
		// volumetric transmittance would darken it incorrectly. So:
		//   Geometry pixels: full composite = beauty * trans + inscatter
		//   Sky pixels:      additive only  = beauty + inscatter
		// Sky pixels sample the volume's far slice (w=1) to pick up the
		// full integrated inscatter along the camera ray.
		const bool gbufferSky = (diff.a < -0.5f) || (nd.w <= 0.0f);

		// View-space depth. Start from the opaque gbuffer's nd.w, then let
		// the REAL depth buffer override with anything NEARER: a water pixel
		// has gbuffer depth of the seabed (or sky) behind it, but the depth
		// buffer holds the water surface itself - fog must integrate to the
		// surface the viewer actually sees. Reconstruction mirrors PostFog.
		float depthVS = gbufferSky ? FAR_PLANE_M : nd.w;
		const float depthSample = g_sceneDepthTex.Sample(g_pointSampler, uv).r;
		if (depthSample < 0.999999f)
		{
			const float2 ndcXY = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
			const float4 worldPosH = mul(float4(ndcXY, depthSample, 1.0f), g_viewProjectionMatrixInverse);
			const float3 worldPos = worldPosH.xyz / max(worldPosH.w, 1e-5f);
			const float depthBufVS = -(mul(float4(worldPos, 1.0f), g_viewMatrix).z);
			depthVS = gbufferSky ? depthBufVS : min(depthVS, depthBufVS);
		}

		// Sky treatment (additive-only composite) now requires the depth
		// buffer to agree: a gbuffer-sky pixel COVERED by depth-writing water
		// is geometry at the water's depth, not sky.
		const bool skyPixel = gbufferSky && (depthSample >= 0.999999f);

		const float w = DepthToVolumeW(depthVS);
		const float4 vlut = SampleVolumeSmooth(g_volumetricIntegrationLUT, g_linearSampler, float3(uv, w));

		float3 inscatter     = vlut.rgb;
		float  transmittance = vlut.a;

		// Analytic continuation beyond the froxel far plane. The volume only
		// covers 128m; previously the whole volumetric contribution FADED OUT
		// between 128m and 512m, so weather fog could never close off the
		// distance - a blizzard rendered as a thin mist shell with clear air
		// behind it, and every preset converged to the same long visibility.
		// Instead, continue the UNIFORM fog component analytically over the
		// remaining distance: exponential extinction with inscatter
		// converging to the ambient fog colour. That equilibrium matches the
		// froxel medium's ambient-inscatter term exactly (its accumulated
		// inscatter saturates to the same colour), so the 128m seam doesn't
		// show. Height fog past the range is deliberately NOT continued
		// (PostFog's damped height term covers it). g_atmosphere.fogDensity
		// is the night-dimmed cbuffer copy, so far fog dims after dark with
		// the rest of the scene instead of glowing.
		//
		// Sky pixels keep the additive-only composite for now: their colour
		// already carries the atmosphere, and the cloud deck is authored
		// grey for foggy presets anyway. (Possible follow-up: fog the sky
		// toward ambient for very dense media so a blizzard whites out the
		// sky too.)
		if (!skyPixel && depthVS > FAR_PLANE_M)
		{
			const float extraDist = depthVS - FAR_PLANE_M;

			// HEIGHT fog must be continued too, not just the uniform term.
			// The froxel medium's extinction is baseExt + heightDensity *
			// exp2(-(y - pivot) * falloff); if the continuation only carries
			// baseExt, the medium abruptly THINS at the far plane and draws
			// a visible ring around the camera at 128m. Closed-form integral
			// of the exponential height profile along the ray segment from
			// the volume's far plane to the surface:
			//   integral = L * (exp(-k*u0) - exp(-k*u1)) / (k*(u1-u0))
			// with k = falloff*ln2 (the froxel medium uses exp2), u = y-pivot,
			// degenerating to L * exp(-k*u0) for horizontal rays.
			const float2 ndcRay = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
			const float4 farH = mul(float4(ndcRay, 1.0f, 1.0f), g_viewProjectionMatrixInverse);
			const float3 rayDir = normalize(farH.xyz / max(farH.w, 1e-6f) - g_eyePos.xyz);
			const float y0 = g_eyePos.y + rayDir.y * FAR_PLANE_M  - g_atmosphere.fogHeightPivot;
			const float y1 = g_eyePos.y + rayDir.y * depthVS      - g_atmosphere.fogHeightPivot;
			const float k = max(g_atmosphere.fogHeightFalloff, 0.0001f) * 0.6931472f; // ln(2)
			const float dy = y1 - y0;
			// Clamp the exponentials so a pivot far above the camera can't
			// blow the integral up to inf (the froxel medium has the same
			// unbounded exp2 but only integrates 128m of it).
			const float e0 = min(exp(-k * y0), 8.0f);
			const float e1 = min(exp(-k * y1), 8.0f);
			const float heightProfile = (abs(dy) > 1e-3f) ? (e0 - e1) / (k * dy) : e0;

			const float extraExt = (max(g_atmosphere.fogDensity, 0.0f)
				+ max(g_atmosphere.fogHeightDensity, 0.0f) * max(heightProfile, 0.0f)) * extraDist;
			const float extraTrans = exp(-extraExt);
			float3 ambientFog = max(g_atmosphere.ambientLight.rgb, 0.0f.xxx);
			// GI occlusion on the beyond-range ambient continuation, matching
			// the froxel medium's in-range ambient gate so the far-plane handoff
			// stays seamless under the same occlusion.
			if (g_giComposeParams.z > 0.5f)
			{
				const float giOcc = saturate(g_giAoTex.Sample(g_pointSampler, uv).r);
				ambientFog *= saturate(1.0f - giOcc * saturate(g_giComposeParams.y));
			}

			// SOURCE RADIANCE FROM THE VOLUME ITSELF. Continuing with the
			// ambient colour alone is what drew the hard line: inside the
			// volume the haze is lit by the SUN (phase x visibility x cloud
			// shadow) as well as ambient, so at the far plane the in-scatter
			// dropped from "sunlit" to "ambient only" in one step.
			//
			// Along any ray, over a segment of constant source radiance L the
			// integrated in-scatter grows by exactly L x (T0 - T1). So the
			// medium's source at the far end of THIS pixel's ray is
			//     L = dI / dT
			// between the last slices - no extinction coefficient, phase
			// function or light list needed, and whatever the froxel pass
			// computed there (sun, cloud shadow, building shadow, ambient) is
			// what carries on. The continuation is then C0 in value AND slope
			// at the far plane by construction: no seam at any range.
			//
			// Where the medium is too thin for dT to be measurable the fog
			// contribution is negligible anyway; blend back to the ambient
			// equilibrium there, and clamp so one bright local light sitting in
			// the last slice cannot be smeared to the horizon.
			const float wPrev = 1.0f - 3.0f / VOLUME_DIMS.z;
			const float4 vlutPrev = SampleVolumeSmooth(g_volumetricIntegrationLUT, g_linearSampler, float3(uv, wPrev));
			const float dT = vlutPrev.a - transmittance;
			const float3 dI = max(inscatter - vlutPrev.rgb, 0.0f.xxx);
			const float3 measuredSource = dI / max(dT, 1e-5f);
			const float ambientPeak = max(max(ambientFog.r, ambientFog.g), max(ambientFog.b, 1e-3f));
			const float3 clampedSource = min(measuredSource, (ambientPeak * 24.0f + 0.5f).xxx);
			const float3 farSource = lerp(ambientFog, clampedSource, saturate(dT / 0.0015f));

			inscatter     += transmittance * (1.0f - extraTrans) * farSource;
			transmittance *= extraTrans;
		}

		// Composite. Geometry uses beauty * t + I; sky skips the
		// transmittance multiply to avoid double-darkening atmospheric
		// scattering already baked into the sky colour.
		const float3 final = skyPixel
			? (beauty.rgb + inscatter)
			: (beauty.rgb * transmittance + inscatter);

		return float4(final, beauty.a);
	}
}
