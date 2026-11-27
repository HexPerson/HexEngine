"GlobalIncludes"
{
	Global
}
"Global"
{
#ifndef RTCOMMON_SHADER
#define RTCOMMON_SHADER

	// Compute ray tracing against the RtScene acceleration structure
	// (HexEngine.Core/Scene/RtScene.cpp). Two levels:
	//   TLAS  - BVH2 over instance world AABBs, leaves hold instance indices
	//           (instances are stored in leaf order, so a leaf is a range).
	//   BLAS  - one BVH2 per (mesh, material), nodes/triangles appended into
	//           shared buffers; child/leaf indices are relative to the BLAS's
	//           own node/triangle base.
	// Node layout (32 B): children of an interior node are ADJACENT
	// (left = leftFirst, right = leftFirst + 1); count > 0 marks a leaf whose
	// primitives are [leftFirst, leftFirst + count).
	//
	// Registers t52-t55 are reserved for these buffers in every pass that
	// includes this file - above every existing pass's bindings (SSR uses
	// up to t24) so the reflection pass can include it too.

	struct RtNode
	{
		float3 bmin;
		uint leftFirst;
		float3 bmax;
		uint count;
	};

	// v0 (xyz) + packed linear albedo RGBA8 (w)
	// e1 (xyz) + emissive R|G as half2 (w)
	// e2 (xyz) + emissive B half | flags (w)
	struct RtTri
	{
		float4 v0Albedo;
		float4 e1Emis;
		float4 e2EmisFlags;
	};

	struct RtInstance
	{
		float4 worldToObject0;
		float4 worldToObject1;
		float4 worldToObject2;
		uint blasNodeOffset;
		uint blasTriOffset;
		uint flags;
		uint pad;
	};

	StructuredBuffer<RtNode> g_rtBlasNodes : register(t52);
	StructuredBuffer<RtTri> g_rtTris : register(t53);
	StructuredBuffer<RtNode> g_rtTlasNodes : register(t54);
	StructuredBuffer<RtInstance> g_rtInstances : register(t55);

	struct RtHit
	{
		float t;
		uint instance;
		uint tri;      // absolute index into g_rtTris
		float2 bary;
	};

	static const float kRtNoHit = 1e30f;

	float RtSlab(float3 bmin, float3 bmax, float3 origin, float3 invDir, float tMax)
	{
		const float3 t0 = (bmin - origin) * invDir;
		const float3 t1 = (bmax - origin) * invDir;
		const float3 tmin = min(t0, t1);
		const float3 tmax = max(t0, t1);
		const float enter = max(max(tmin.x, tmin.y), max(tmin.z, 0.0f));
		const float exit = min(min(tmax.x, tmax.y), min(tmax.z, tMax));
		return (enter <= exit) ? enter : kRtNoHit;
	}

	float3 RtSafeInv(float3 d)
	{
		const float eps = 1e-12f;
		return float3(
			1.0f / (abs(d.x) > eps ? d.x : (d.x >= 0.0f ? eps : -eps)),
			1.0f / (abs(d.y) > eps ? d.y : (d.y >= 0.0f ? eps : -eps)),
			1.0f / (abs(d.z) > eps ? d.z : (d.z >= 0.0f ? eps : -eps)));
	}

	// Moller-Trumbore, both faces. Returns t or kRtNoHit.
	float RtIntersectTri(RtTri tri, float3 origin, float3 dir, float tMax, out float2 bary)
	{
		bary = 0.0f.xx;
		const float3 e1 = tri.e1Emis.xyz;
		const float3 e2 = tri.e2EmisFlags.xyz;
		const float3 p = cross(dir, e2);
		const float det = dot(e1, p);
		if (abs(det) < 1e-12f)
			return kRtNoHit;
		const float invDet = 1.0f / det;
		const float3 s = origin - tri.v0Albedo.xyz;
		const float u = dot(s, p) * invDet;
		if (u < 0.0f || u > 1.0f)
			return kRtNoHit;
		const float3 q = cross(s, e1);
		const float v = dot(dir, q) * invDet;
		if (v < 0.0f || u + v > 1.0f)
			return kRtNoHit;
		const float t = dot(e2, q) * invDet;
		if (t <= 1e-5f || t >= tMax)
			return kRtNoHit;
		bary = float2(u, v);
		return t;
	}

	float3 RtToObjectPoint(RtInstance inst, float3 p)
	{
		return float3(
			dot(inst.worldToObject0.xyz, p) + inst.worldToObject0.w,
			dot(inst.worldToObject1.xyz, p) + inst.worldToObject1.w,
			dot(inst.worldToObject2.xyz, p) + inst.worldToObject2.w);
	}

	float3 RtToObjectDir(RtInstance inst, float3 d)
	{
		return float3(
			dot(inst.worldToObject0.xyz, d),
			dot(inst.worldToObject1.xyz, d),
			dot(inst.worldToObject2.xyz, d));
	}

	// Object-space normal -> world: transpose(worldToObject 3x3) * n.
	float3 RtNormalToWorld(RtInstance inst, float3 n)
	{
		return normalize(inst.worldToObject0.xyz * n.x + inst.worldToObject1.xyz * n.y + inst.worldToObject2.xyz * n.z);
	}

	// Closest hit over one BLAS with the ray already in object space. The
	// object-space direction is NOT renormalised, so t stays the world-space
	// parameter and hits from different instances compare directly.
	void RtTraceBlas(RtInstance inst, uint instIndex, float3 o, float3 d, bool anyHit, inout RtHit hit)
	{
		const float3 invD = RtSafeInv(d);
		uint stack[32];
		uint sp = 0;
		stack[sp++] = 0;
		[loop]
		while (sp > 0)
		{
			const uint nodeIndex = stack[--sp];
			const RtNode node = g_rtBlasNodes[inst.blasNodeOffset + nodeIndex];
			if (RtSlab(node.bmin, node.bmax, o, invD, hit.t) >= kRtNoHit)
				continue;
			if (node.count > 0)
			{
				[loop]
				for (uint i = 0; i < node.count; ++i)
				{
					const uint triIndex = inst.blasTriOffset + node.leftFirst + i;
					float2 bary;
					const float t = RtIntersectTri(g_rtTris[triIndex], o, d, hit.t, bary);
					if (t < hit.t)
					{
						hit.t = t;
						hit.instance = instIndex;
						hit.tri = triIndex;
						hit.bary = bary;
						if (anyHit)
							return;
					}
				}
				continue;
			}
			// Near child last so it pops first.
			const RtNode a = g_rtBlasNodes[inst.blasNodeOffset + node.leftFirst];
			const RtNode b = g_rtBlasNodes[inst.blasNodeOffset + node.leftFirst + 1];
			const float ta = RtSlab(a.bmin, a.bmax, o, invD, hit.t);
			const float tb = RtSlab(b.bmin, b.bmax, o, invD, hit.t);
			if (sp + 2 > 32)
				continue;
			if (ta <= tb)
			{
				if (tb < kRtNoHit) stack[sp++] = node.leftFirst + 1;
				if (ta < kRtNoHit) stack[sp++] = node.leftFirst;
			}
			else
			{
				if (ta < kRtNoHit) stack[sp++] = node.leftFirst;
				if (tb < kRtNoHit) stack[sp++] = node.leftFirst + 1;
			}
		}
	}

	// World-space ray, normalised dir. anyHit: stop at the first hit
	// (shadow / visibility rays).
	// skipKey != 0: ignore the instance whose pad (owner entity index + 1)
	// equals it - a local light's own fixture (DDGI.cpp FixtureKey).
	RtHit RtTraceSkip(float3 origin, float3 dir, float tMax, bool anyHit, uint skipKey)
	{
		RtHit hit;
		hit.t = tMax;
		hit.instance = 0xffffffffu;
		hit.tri = 0xffffffffu;
		hit.bary = 0.0f.xx;

		const float3 invD = RtSafeInv(dir);
		uint stack[32];
		uint sp = 0;
		stack[sp++] = 0;
		[loop]
		while (sp > 0)
		{
			const uint nodeIndex = stack[--sp];
			const RtNode node = g_rtTlasNodes[nodeIndex];
			if (RtSlab(node.bmin, node.bmax, origin, invD, hit.t) >= kRtNoHit)
				continue;
			if (node.count > 0)
			{
				[loop]
				for (uint i = 0; i < node.count; ++i)
				{
					const uint instIndex = node.leftFirst + i;
					const RtInstance inst = g_rtInstances[instIndex];
					if (skipKey != 0u && inst.pad == skipKey)
						continue;
					RtTraceBlas(inst, instIndex, RtToObjectPoint(inst, origin), RtToObjectDir(inst, dir), anyHit, hit);
					if (anyHit && hit.instance != 0xffffffffu)
						return hit;
				}
				continue;
			}
			const RtNode a = g_rtTlasNodes[node.leftFirst];
			const RtNode b = g_rtTlasNodes[node.leftFirst + 1];
			const float ta = RtSlab(a.bmin, a.bmax, origin, invD, hit.t);
			const float tb = RtSlab(b.bmin, b.bmax, origin, invD, hit.t);
			if (sp + 2 > 32)
				continue;
			if (ta <= tb)
			{
				if (tb < kRtNoHit) stack[sp++] = node.leftFirst + 1;
				if (ta < kRtNoHit) stack[sp++] = node.leftFirst;
			}
			else
			{
				if (ta < kRtNoHit) stack[sp++] = node.leftFirst;
				if (tb < kRtNoHit) stack[sp++] = node.leftFirst + 1;
			}
		}
		if (hit.instance == 0xffffffffu)
			hit.t = kRtNoHit;
		return hit;
	}

	RtHit RtTrace(float3 origin, float3 dir, float tMax, bool anyHit)
	{
		return RtTraceSkip(origin, dir, tMax, anyHit, 0u);
	}

	bool RtOccluded(float3 origin, float3 dir, float tMax)
	{
		return RtTraceSkip(origin, dir, tMax, true, 0u).instance != 0xffffffffu;
	}

	bool RtOccludedSkip(float3 origin, float3 dir, float tMax, uint skipKey)
	{
		return RtTraceSkip(origin, dir, tMax, true, skipKey).instance != 0xffffffffu;
	}

	float3 RtUnpackAlbedo(uint packed)
	{
		return float3(
			(float)(packed & 0xffu),
			(float)((packed >> 8) & 0xffu),
			(float)((packed >> 16) & 0xffu)) * (1.0f / 255.0f);
	}

	struct RtSurface
	{
		float3 position;
		float3 normal;      // world geometric normal, facing the ray origin side as authored
		bool frontFace;     // ray hit the authored front side
		float3 albedo;
		float3 emissive;
	};

	RtSurface RtGetSurface(RtHit hit, float3 origin, float3 dir)
	{
		RtSurface s;
		const RtTri tri = g_rtTris[hit.tri];
		const RtInstance inst = g_rtInstances[hit.instance];
		s.position = origin + dir * hit.t;
		const float3 nObj = cross(tri.e1Emis.xyz, tri.e2EmisFlags.xyz);
		float3 n = RtNormalToWorld(inst, nObj);
		s.frontFace = dot(n, dir) < 0.0f;
		s.normal = n;
		s.albedo = RtUnpackAlbedo(asuint(tri.v0Albedo.w));
		const uint e1 = asuint(tri.e1Emis.w);
		const uint e2 = asuint(tri.e2EmisFlags.w);
		s.emissive = float3(f16tof32(e1 & 0xffffu), f16tof32(e1 >> 16), f16tof32(e2 & 0xffffu));
		return s;
	}

#endif
}
