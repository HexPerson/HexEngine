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
		o.position = mul(float4(worldPos, 1.0f), g_viewProjectionMatrix);
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
		float smoothness =
			(0.10f * heightWeighted.x) +
			(0.14f * heightWeighted.y) +
			(0.24f * heightWeighted.z) +
			(0.08f * heightWeighted.w);
		float specularProbability =
			(0.08f * heightWeighted.x) +
			(0.15f * heightWeighted.y) +
			(0.13f * heightWeighted.z) +
			(0.06f * heightWeighted.w);

		float4 viewPos = mul(float4(input.worldPos, 1.0f), g_viewMatrix);
		float pixelDepth = -viewPos.z;

		output.diff = float4(baseColor, 1.0f);
		output.mat = float4(metallic, roughness, smoothness, specularProbability);
		output.norm = float4(N, pixelDepth);
		output.pos = float4(input.worldPos, 0.0f);
		output.velocity = float2(0.0f, 0.0f);
		return output;
	}
}
