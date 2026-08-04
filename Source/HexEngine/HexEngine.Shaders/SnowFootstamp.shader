"InputLayout"
{
	Pos
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
	// Snow footprint STAMP (Phase 3 Part B). One instance per live footprint;
	// the static VB is a unit quad in [-0.5,0.5]^2 (XY). The instance's world
	// position / facing / foot size come from a structured buffer indexed by
	// SV_InstanceID. The quad is laid flat on the ground and projected with the
	// top-down footprint ortho (g_viewProjectionMatrix is set to that VP for
	// this pass, exactly like the rain-occlusion render).
	struct FootprintGpu
	{
		float2 worldXZ;
		float2 dirXZ;
		float  halfLen;
		float  halfWidth;
		float  side;
		float  fade;
	};
	StructuredBuffer<FootprintGpu> g_footprints : register(t0);

	struct VSIn { float3 position : POSITION; };
	struct PSIn
	{
		float4 position : SV_POSITION;
		float2 local    : TEXCOORD0; // [-1,1] within the foot quad
		float  side     : TEXCOORD1; // 0 = left, 1 = right
		float  fade     : TEXCOORD2; // 1 fresh -> 0 refilled
	};

	PSIn ShaderMain(VSIn input, uint iid : SV_INSTANCEID)
	{
		PSIn o;
		FootprintGpu fp = g_footprints[iid];

		// Oriented ground basis: fwd = facing, right = perpendicular.
		const float2 fwd = normalize(fp.dirXZ + float2(0.0f, 1e-4f));
		const float2 rgt = float2(fwd.y, -fwd.x);

		// Unit quad corner [-0.5,0.5] -> foot-sized world offset on the plane.
		const float2 corner = input.position.xy;
		const float2 offXZ = rgt * (corner.x * 2.0f * fp.halfWidth)
		                   + fwd * (corner.y * 2.0f * fp.halfLen);
		const float2 worldXZ = fp.worldXZ + offXZ;

		// Top-down ortho spans the whole vertical range, so the exact y is
		// irrelevant; use the map centre height baked into the VP (y=0 here).
		const float3 worldPos = float3(worldXZ.x, 0.0f, worldXZ.y);
		o.position = mul(float4(worldPos, 1.0f), g_viewProjectionMatrix);
		o.local = corner * 2.0f; // -1..1
		o.side = fp.side;
		o.fade = fp.fade;
		return o;
	}
}
"PixelShader"
{
	struct PSIn
	{
		float4 position : SV_POSITION;
		float2 local    : TEXCOORD0;
		float  side     : TEXCOORD1;
		float  fade     : TEXCOORD2;
	};

	// Soft elliptical lobe: 1 at the centre, 0 at the rim.
	float EllipseMask(float2 p, float2 c, float2 r)
	{
		const float2 d = (p - c) / r;
		return saturate(1.0f - dot(d, d));
	}

	// Procedural foot: a wide "ball" lobe toward the toe (+y) plus a smaller
	// "heel" lobe behind (-y), with a slight inward arch. side mirrors X so the
	// left and right prints differ. Output = depression depth 0..1 in R, faded
	// by age. Additive blend + saturate downstream keeps overlaps bounded.
	float4 ShaderMain(PSIn input) : SV_TARGET
	{
		float2 p = input.local;                       // [-1,1]
		p.x *= (input.side > 0.5f) ? -1.0f : 1.0f;    // mirror for right foot

		const float ball = EllipseMask(p, float2(0.10f,  0.35f), float2(0.62f, 0.60f));
		const float heel = EllipseMask(p, float2(-0.06f, -0.55f), float2(0.42f, 0.42f));
		float mask = max(ball, heel);
		mask = smoothstep(0.0f, 0.55f, mask);         // rounded depression

		const float depth = saturate(mask * saturate(input.fade));
		return float4(depth, 0.0f, 0.0f, depth);
	}
}
