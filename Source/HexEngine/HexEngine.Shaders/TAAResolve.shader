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
}
"VertexShader"
{
	UIPixelInput ShaderMain(UIVertexInput input)
	{
		UIPixelInput output;

		output.position = input.position;
		output.texcoord = input.texcoord;
		output.colour = input.colour;

		return output;
	}
}
"PixelShader"
{
	Texture2D historyTexture : register(t0);
	Texture2D velocityTexture : register(t1);
	Texture2D depthTexture : register(t2);
	Texture2D shaderTexture : register(t3);

	SamplerState PointSampler : register(s3);
	SamplerState LinearSampler : register(s4);

#define FRAME_VELOCITY_IN_TEXELS_DIFF 0.01f
#define FRAME_DEPTH_MAX_DIFF 2.0f

	float4 ShaderMain(UIPixelInput input) : SV_Target
	{
		float3 colour = shaderTexture.Sample(PointSampler, input.texcoord).rgb;

		float2 velocity = velocityTexture.Sample(PointSampler, input.texcoord).xy;

		//return float4(velocity.x, velocity.y, 0.0f, 1.0f);

		//velocity *= float2(g_screenWidth, g_screenHeight);
		//velocity.xy = (velocity.xy + 1) / 2.0f;
		//velocity.y = 1 - velocity.y;

		//return float4(velocity.x, velocity.y, 0.0f, 1.0f);

		float depth = depthTexture.Sample(PointSampler, input.texcoord).w;

		// NOTE: a previous "velocityConfidence" term fell to zero whenever |velocity| exceeded
		// 0.01 UV (~ a couple of pixels of motion), which is essentially any camera movement.
		// That zeroed out the modulationFactor below, making TAA output the current frame only
		// during motion - completely defeating the temporal AA and causing the walls to shimmer
		// the moment the camera moved. The neighbourhood box clamp lower down already handles
		// the genuinely-stale-history case (it clamps the reprojected history to the current
		// frame's local color range), so we don't need a separate motion-based reject term.

		float2 prevousPixelPos = input.texcoord + velocity;

		float3 history = historyTexture.Sample(LinearSampler, prevousPixelPos);

		float oldDepth = depthTexture.Sample(LinearSampler, prevousPixelPos).w;

		// Depth-based history rejection. At silhouette edges (building-against-terrain,
		// roof-against-sky), the reprojected history position lands on a *different* surface
		// than the current pixel. Without this guard, the 4-neighbour box clamp below widens
		// to include the adjacent surface's colour (e.g. green grass next to a roof edge) and
		// lets ghosted history bleed across the silhouette - that's the camera-angle-dependent
		// green band on building edges.
		//
		// The tolerance scales with view depth: a flat 0.15-world-unit threshold catches close
		// silhouettes correctly but produces false rejections on relief-heavy surfaces far
		// from the camera (e.g. volumetric terrain), where any lateral camera motion shifts
		// the reprojection across a slope and naturally pulls a depth delta larger than 0.15.
		// Allowing the threshold to grow with depth (roughly a 4% relative tolerance) keeps the
		// silhouette test useful up close while letting distant terrain accumulate properly.
		const float depthDelta = abs(depth - oldDepth);
		const float adaptiveThreshold = max(FRAME_DEPTH_MAX_DIFF, depth * 0.4f);

		// Off-screen reprojection rejection. The history texture is bound through a sampler
		// that uses wrap addressing (LinearWrap at s4 - see GraphicsDeviceD3D11.cpp), so a
		// prevousPixelPos outside [0,1] pulls colour from the OPPOSITE edge of the previous
		// frame and produces a visible "mirror" of left-edge content onto the right edge
		// (and vice versa) at the screen border. Treat any out-of-bounds reprojection as a
		// history miss so we fall back to the current frame at the edges rather than blending
		// in unrelated wrapped pixels.
		const bool outOfBounds = any(prevousPixelPos < 0.0f.xx) || any(prevousPixelPos > 1.0f.xx);
		const bool historyRejected = (depthDelta > adaptiveThreshold) || outOfBounds;

		// Variance clipping (Salvi 2016, used by Unreal among others) instead of plain min/max
		// box clamp. The reason: for high-frequency content like triplanar-blended terrain or
		// detailed albedo textures, a 3x3 colour neighbourhood spans a WIDE range of values,
		// so the box clamp's [min, max] interval is loose enough that mis-reprojected history
		// samples (from sub-pixel velocity error or LOD-transition vertex shifts) still slip
		// through and blend in at 90% - producing the persistent ghost trail terrain showed
		// even with correct motion vectors. Variance clipping defines a tighter clamp range
		// based on the colour distribution's mean and standard deviation (mean +/- gamma*sigma),
		// which excludes outlier-looking history values without false-rejecting legitimately
		// noisy regions. gamma = 1.25 is the standard balance between tightness and stability.
		const float3 N0 = shaderTexture.Sample(PointSampler, input.texcoord, int2( 1,  0)).xyz;
		const float3 N1 = shaderTexture.Sample(PointSampler, input.texcoord, int2(-1,  0)).xyz;
		const float3 N2 = shaderTexture.Sample(PointSampler, input.texcoord, int2( 0,  1)).xyz;
		const float3 N3 = shaderTexture.Sample(PointSampler, input.texcoord, int2( 0, -1)).xyz;
		const float3 N4 = shaderTexture.Sample(PointSampler, input.texcoord, int2( 1,  1)).xyz;
		const float3 N5 = shaderTexture.Sample(PointSampler, input.texcoord, int2(-1,  1)).xyz;
		const float3 N6 = shaderTexture.Sample(PointSampler, input.texcoord, int2( 1, -1)).xyz;
		const float3 N7 = shaderTexture.Sample(PointSampler, input.texcoord, int2(-1, -1)).xyz;

		const float3 m1 = colour + N0 + N1 + N2 + N3 + N4 + N5 + N6 + N7;
		const float3 m2 = colour * colour
		                + N0 * N0 + N1 * N1 + N2 * N2 + N3 * N3
		                + N4 * N4 + N5 * N5 + N6 * N6 + N7 * N7;
		const float invN = 1.0f / 9.0f;
		const float3 mean = m1 * invN;
		const float3 variance = max(m2 * invN - mean * mean, 0.0f.xxx);
		const float3 sigma = sqrt(variance);

		const float gamma = 0.5f;
		const float3 clipMin = mean - sigma * gamma;
		const float3 clipMax = mean + sigma * gamma;
		history = clamp(history, clipMin, clipMax);

		// 0.9 = standard temporal blend; collapse to 0 (current-only) when we've decided the
		// history sample is from a different surface.
		const float modulationFactor = historyRejected ? 0.0f : 0.9f;

		float3 resolvedColour = lerp(colour, history, modulationFactor);

		//return float4(1,0,0,1);

		return float4(resolvedColour, 1.0f);
	}
}
