"Requirements"
{
	GBuffer
}
"InputLayout"
{
	None
}
"VertexShaderIncludes"
{
	Global
}
"PixelShaderIncludes"
{
	Global
	Utils
	PBRutils
}
"VertexShader"
{
	struct TerrainTriangleGpu
	{
		float4 p0;
		float4 p1;
		float4 p2;
		float4 n0;
		float4 n1;
		float4 n2;
	};

	StructuredBuffer<TerrainTriangleGpu> g_surfaceTriangles : register(t0);

	struct VSOut
	{
		float4 position : SV_Position;
		float3 worldPos : TEXCOORD0;
		float3 normal : TEXCOORD1;
		// Unjittered clip-space positions for the current and previous frame, used by the PS
		// to compute per-pixel motion vectors. Terrain is static, so the only motion comes
		// from the camera (encoded in the difference between g_viewProjectionMatrix and
		// g_viewProjectionMatrixPrev). Without these the velocity RT stays zero and TAA
		// reprojection cannot keep history aligned with the surface, producing visible
		// ghosting whenever the camera moves.
		float4 currentPositionUnjittered  : TEXCOORD2;
		float4 previousPositionUnjittered : TEXCOORD3;
	};

	VSOut ShaderMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
	{
		VSOut o;
		const TerrainTriangleGpu tri = g_surfaceTriangles[instanceId];
		float3 worldPos = tri.p0.xyz;
		float3 normal = tri.n0.xyz;
		if (vertexId == 1u)
		{
			worldPos = tri.p1.xyz;
			normal = tri.n1.xyz;
		}
		else if (vertexId == 2u)
		{
			worldPos = tri.p2.xyz;
			normal = tri.n2.xyz;
		}

		o.worldPos = worldPos;
		o.normal = normalize(normal);

		// Compute current/previous clip positions; these feed CalcVelocity in the pixel
		// shader so TAA can reproject this frame's terrain pixels onto where the same world
		// point appeared in last frame's history.
		const float4 currClip = mul(float4(worldPos, 1.0f), g_viewProjectionMatrix);
		const float4 prevClip = mul(float4(worldPos, 1.0f), g_viewProjectionMatrixPrev);
		o.currentPositionUnjittered  = currClip;
		o.previousPositionUnjittered = prevClip;

		o.position = currClip;
		o.position.xy += g_jitterOffsets * o.position.w;
		return o;
	}
}
"PixelShader"
{
	Texture3D<float4> g_materialWeights : register(t0);
	Texture2D g_grassAlbedoMap : register(t1);
	Texture2D g_rockAlbedoMap : register(t2);
	Texture2D g_snowAlbedoMap : register(t3);
	Texture2D g_dirtAlbedoMap : register(t4);
	Texture2D g_grassNormalMap : register(t5);
	Texture2D g_rockNormalMap : register(t6);
	Texture2D g_snowNormalMap : register(t7);
	Texture2D g_dirtNormalMap : register(t8);
	Texture2D g_grassAoMap : register(t9);
	Texture2D g_rockAoMap : register(t10);
	Texture2D g_snowAoMap : register(t11);
	Texture2D g_dirtAoMap : register(t12);
	Texture2D g_grassHeightMap : register(t13);
	Texture2D g_rockHeightMap : register(t14);
	Texture2D g_snowHeightMap : register(t15);
	Texture2D g_dirtHeightMap : register(t16);
	Texture2D g_grassRoughnessMap : register(t17);
	Texture2D g_rockRoughnessMap : register(t18);
	Texture2D g_snowRoughnessMap : register(t19);
	Texture2D g_dirtRoughnessMap : register(t20);
	Texture2D g_grassMetallicMap : register(t21);
	Texture2D g_rockMetallicMap : register(t22);
	Texture2D g_snowMetallicMap : register(t23);
	Texture2D g_dirtMetallicMap : register(t24);
	SamplerState g_textureSampler : register(s0);

	struct VSOut
	{
		float4 position : SV_Position;
		float3 worldPos : TEXCOORD0;
		float3 normal : TEXCOORD1;
		// Must match the VS output layout. PS reads these to compute motion vectors.
		float4 currentPositionUnjittered  : TEXCOORD2;
		float4 previousPositionUnjittered : TEXCOORD3;
	};

	cbuffer VolumetricTerrainChunkBuffer : register(b6)
	{
		float4 g_chunkOriginVoxel;
		float4 g_chunkWorldInfo;
		float4 g_chunkGrassColor;
		float4 g_chunkRockColor;
		float4 g_chunkSnowColor;
		float4 g_chunkDirtColor;
	};

	float4 SampleMaterialWeights(float3 worldPos)
	{
		const int baseResolution = (int)g_chunkWorldInfo.y;
		const float voxelSize = max(g_chunkOriginVoxel.w, 0.0001f);
		const float maxLocal = max((float)baseResolution, 1.0f);
		float3 local = (worldPos - g_chunkOriginVoxel.xyz) / voxelSize;
		float3 uvw = saturate(local / maxLocal);
		float4 weights = g_materialWeights.SampleLevel(g_textureSampler, uvw, 0);
		float weightSum = max(weights.x + weights.y + weights.z + weights.w, 0.0001f);
		return weights / weightSum;
	}

	float3 UnpackNormalMap(float4 sampleValue)
	{
		return normalize(sampleValue.xyz * 2.0f - 1.0f);
	}

	float3 ComputeTriplanarWeights(float3 normal)
	{
		float3 weights = pow(abs(normal), 4.0f);
		return weights / max(weights.x + weights.y + weights.z, 0.0001f);
	}

	float3 SampleTriplanarAlbedo(Texture2D tex, float3 worldPos, float3 normal, float scale, float3 fallbackColor)
	{
		float3 weights = ComputeTriplanarWeights(normal);
		float2 uvX = worldPos.yz * scale;
		float2 uvY = worldPos.xz * scale;
		float2 uvZ = worldPos.xy * scale;
		float3 sx = tex.Sample(g_textureSampler, uvX).rgb;
		float3 sy = tex.Sample(g_textureSampler, uvY).rgb;
		float3 sz = tex.Sample(g_textureSampler, uvZ).rgb;
		float3 sampled = (sx * weights.x) + (sy * weights.y) + (sz * weights.z);
		return lerp(fallbackColor, sampled, 0.9f);
	}

	float SampleTriplanarScalar(Texture2D tex, float3 worldPos, float3 normal, float scale, float fallbackValue)
	{
		float3 weights = ComputeTriplanarWeights(normal);
		float2 uvX = worldPos.yz * scale;
		float2 uvY = worldPos.xz * scale;
		float2 uvZ = worldPos.xy * scale;
		float sx = tex.Sample(g_textureSampler, uvX).r;
		float sy = tex.Sample(g_textureSampler, uvY).r;
		float sz = tex.Sample(g_textureSampler, uvZ).r;
		float sampled = (sx * weights.x) + (sy * weights.y) + (sz * weights.z);
		return lerp(fallbackValue, sampled, 0.9f);
	}

	float3 SampleTriplanarDetailNormal(Texture2D tex, float3 worldPos, float3 baseNormal, float scale)
	{
		float3 weights = ComputeTriplanarWeights(baseNormal);
		float2 uvX = worldPos.yz * scale;
		float2 uvY = worldPos.xz * scale;
		float2 uvZ = worldPos.xy * scale;

		float3 nx = UnpackNormalMap(tex.Sample(g_textureSampler, uvX));
		float3 ny = UnpackNormalMap(tex.Sample(g_textureSampler, uvY));
		float3 nz = UnpackNormalMap(tex.Sample(g_textureSampler, uvZ));

		nx = normalize(float3(nx.z * sign(baseNormal.x == 0.0f ? 1.0f : baseNormal.x), nx.x, nx.y));
		ny = normalize(float3(ny.x, ny.z * sign(baseNormal.y == 0.0f ? 1.0f : baseNormal.y), ny.y));
		nz = normalize(float3(nz.x, nz.y, nz.z * sign(baseNormal.z == 0.0f ? 1.0f : baseNormal.z)));

		return normalize((nx * weights.x) + (ny * weights.y) + (nz * weights.z));
	}

	GBufferOut ShaderMain(VSOut input)
	{
		GBufferOut output;
		float3 interpolatedNormal = normalize(input.normal);
		float3 geometricNormal = normalize(cross(ddy(input.worldPos), ddx(input.worldPos)));
		if (dot(geometricNormal, interpolatedNormal) < 0.0f)
		{
			geometricNormal = -geometricNormal;
		}
		float3 N = normalize(lerp(geometricNormal, interpolatedNormal, 0.7f));
		float4 layerWeights = SampleMaterialWeights(input.worldPos);
		float grassHeight = SampleTriplanarScalar(g_grassHeightMap, input.worldPos, N, 0.075f, 0.5f);
		float rockHeight = SampleTriplanarScalar(g_rockHeightMap, input.worldPos, N, 0.045f, 0.5f);
		float snowHeight = SampleTriplanarScalar(g_snowHeightMap, input.worldPos, N, 0.02f, 0.5f);
		float dirtHeight = SampleTriplanarScalar(g_dirtHeightMap, input.worldPos, N, 0.06f, 0.5f);

		float4 heightWeighted = layerWeights * float4(
			lerp(0.75f, 1.25f, grassHeight),
			lerp(0.75f, 1.25f, rockHeight),
			lerp(0.75f, 1.25f, snowHeight),
			lerp(0.75f, 1.25f, dirtHeight));
		heightWeighted /= max(heightWeighted.x + heightWeighted.y + heightWeighted.z + heightWeighted.w, 0.0001f);

		float3 grassAlbedo = SampleTriplanarAlbedo(g_grassAlbedoMap, input.worldPos, N, 0.075f, g_chunkGrassColor.rgb);
		float3 rockAlbedo = SampleTriplanarAlbedo(g_rockAlbedoMap, input.worldPos, N, 0.045f, g_chunkRockColor.rgb);
		float3 snowAlbedo = SampleTriplanarAlbedo(g_snowAlbedoMap, input.worldPos, N, 0.02f, g_chunkSnowColor.rgb);
		float3 dirtAlbedo = SampleTriplanarAlbedo(g_dirtAlbedoMap, input.worldPos, N, 0.06f, g_chunkDirtColor.rgb);

		float grassAo = SampleTriplanarScalar(g_grassAoMap, input.worldPos, N, 0.075f, 1.0f);
		float rockAo = SampleTriplanarScalar(g_rockAoMap, input.worldPos, N, 0.045f, 1.0f);
		float snowAo = SampleTriplanarScalar(g_snowAoMap, input.worldPos, N, 0.02f, 1.0f);
		float dirtAo = SampleTriplanarScalar(g_dirtAoMap, input.worldPos, N, 0.06f, 1.0f);

		float3 grassDetailNormal = SampleTriplanarDetailNormal(g_grassNormalMap, input.worldPos, N, 0.075f);
		float3 rockDetailNormal = SampleTriplanarDetailNormal(g_rockNormalMap, input.worldPos, N, 0.045f);
		float3 snowDetailNormal = SampleTriplanarDetailNormal(g_snowNormalMap, input.worldPos, N, 0.02f);
		float3 dirtDetailNormal = SampleTriplanarDetailNormal(g_dirtNormalMap, input.worldPos, N, 0.06f);

		float grassRoughness = SampleTriplanarScalar(g_grassRoughnessMap, input.worldPos, N, 0.075f, 1.0f);
		float rockRoughness = SampleTriplanarScalar(g_rockRoughnessMap, input.worldPos, N, 0.045f, 1.0f);
		float snowRoughness = SampleTriplanarScalar(g_snowRoughnessMap, input.worldPos, N, 0.02f, 1.0f);
		float dirtRoughness = SampleTriplanarScalar(g_dirtRoughnessMap, input.worldPos, N, 0.06f, 1.0f);

		float grassMetallic = SampleTriplanarScalar(g_grassMetallicMap, input.worldPos, N, 0.075f, 1.0f);
		float rockMetallic = SampleTriplanarScalar(g_rockMetallicMap, input.worldPos, N, 0.045f, 1.0f);
		float snowMetallic = SampleTriplanarScalar(g_snowMetallicMap, input.worldPos, N, 0.02f, 1.0f);
		float dirtMetallic = SampleTriplanarScalar(g_dirtMetallicMap, input.worldPos, N, 0.06f, 1.0f);

		float3 baseColor =
			(grassAlbedo * grassAo * heightWeighted.x) +
			(rockAlbedo * rockAo * heightWeighted.y) +
			(snowAlbedo * snowAo * heightWeighted.z) +
			(dirtAlbedo * dirtAo * heightWeighted.w);

		float3 detailNormal =
			(grassDetailNormal * heightWeighted.x) +
			(rockDetailNormal * heightWeighted.y) +
			(snowDetailNormal * heightWeighted.z) +
			(dirtDetailNormal * heightWeighted.w);
		N = normalize(lerp(N, normalize(detailNormal), 0.55f));

		float metallic =
			(0.02f * grassMetallic * heightWeighted.x) +
			(0.03f * rockMetallic * heightWeighted.y) +
			(0.01f * snowMetallic * heightWeighted.z) +
			(0.02f * dirtMetallic * heightWeighted.w);
		float roughness =
			(0.90f * grassRoughness * heightWeighted.x) +
			(0.72f * rockRoughness * heightWeighted.y) +
			(0.52f * snowRoughness * heightWeighted.z) +
			(0.86f * dirtRoughness * heightWeighted.w);
		// Smoothness is the SSR enable gate (mat.b - read by SSR as both the
		// "should this pixel reflect?" test and the reflection-sharpness scale).
		// Previously the blend baked in per-layer smoothness contributions
		// (0.08..0.24) which looked matte at a glance but triggered noticeable
		// SSR pulls of nearby buildings / sky onto open ground - the "terrain
		// mistakenly reflecting" symptom. Terrain (grass / rock / dirt / dry
		// snow) has essentially zero macroscopic specular gloss in real life;
		// the high-frequency glare you do see on damp grass or fresh snow is
		// the weather/wetness layer's job, not the base terrain shader's.
		// Force smoothness to 0 so SSR is disabled for dry terrain pixels.
		float smoothness = 0.0f;

		// Snow accumulation. Same global driver from g_weatherSurface.snowCoverage
		// that DefaultPixel / DefaultAnimated / graph-compiled PSs use - just
		// called explicitly here so terrain (which has its own surface shader,
		// not Default) also catches snow. See ApplySnowAccumulation in
		// PBRutils.shader for the full doc.
		if (g_weatherSurface.snowCoverage > 0.001f)
		{
			const float4 __snowResult = ApplySnowAccumulation(baseColor, roughness, N, input.worldPos, g_weatherSurface.snowCoverage);
			baseColor = __snowResult.rgb;
			roughness = __snowResult.w;
		}

		float4 viewPos = mul(float4(input.worldPos, 1.0f), g_viewMatrix);
		float pixelDepth = -viewPos.z;

		output.diff = float4(baseColor, 1.0f);
		// .a was the per-layer specularProbability blend - removed along with the
		// MaterialProperties::specularProbability field, since nothing in the
		// renderer actually sampled mat.a. Writes 0 to keep the channel clean.
		output.mat = float4(metallic, roughness, 0.0f, 0.0f);
		output.norm = float4(N, pixelDepth);
		output.pos = float4(input.worldPos, 0.0f);
		// Per-pixel screen-space motion from the camera moving relative to this static surface.
		// CalcVelocity divides by .w and converts NDC -> UV so the result drops straight into
		// the velocity RT in the same units TAA expects.
		output.velocity = CalcVelocity(input.currentPositionUnjittered, input.previousPositionUnjittered, float2(g_screenWidth, g_screenHeight));
		// Terrain is standard PBR.
		output.feat = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return output;
	}
}
