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
	};

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
		float3 L = normalize(-g_lightDirection.xyz);
		float diffuse = saturate(dot(N, L)) * max(g_globalLight.x, 0.1f);
		float slope = saturate(1.0f - N.y);
		float lowBlend = saturate(1.0f - input.worldPos.y / max(g_chunkWorldInfo.z, 1.0f));
		float highBlend = saturate((input.worldPos.y - (g_chunkWorldInfo.z * 0.55f)) / max(g_chunkWorldInfo.z * 0.25f, 1.0f));
		float3 baseColor = lerp(g_chunkGrassColor.rgb, g_chunkRockColor.rgb, saturate(slope * 1.6f + lowBlend * 0.2f));
		baseColor = lerp(baseColor, g_chunkSnowColor.rgb, highBlend * saturate(1.0f - slope * 0.7f));
		float3 ambient = float3(0.18f, 0.2f, 0.22f);
		float3 lit = baseColor * (ambient + diffuse);
		float4 viewPos = mul(float4(input.worldPos, 1.0f), g_viewMatrix);
		float pixelDepth = -viewPos.z;

		output.diff = float4(lit, 1.0f);
		output.mat = float4(0.02f, 0.85f, 0.15f, 0.0f);
		output.norm = float4(N, pixelDepth);
		output.pos = float4(input.worldPos, 0.0f);
		output.velocity = float2(0.0f, 0.0f);
		return output;
	}
}
