"ComputeShaderIncludes"
{
	Global
}
"ComputeShader"
{
	struct ParticleStateGpu
	{
		float4 position;
		float4 velocity;
		float4 color;
		float4 misc0;
		float4 misc1;
	};

	struct EmitterGpu
	{
		float4 emitterPosition;
		float4 emitterVelocity;
		float4 emitterForward;
		float4 shapeParams0;
		float4 shapeParams1;
		float4 simulation0;
		float4 simulation1;
		float4 simulation2;
		float4 simulation3;
		float4 simulation4;
		float4 colorStart;
		float4 colorEnd;
		float4 lifeParams;
		uint poolStart;
		uint poolCount;
		int spawnRemaining;
		uint flags;
	};

	struct MeshInstanceData
	{
		float4x4 world;
		float4x4 worldInverseTranspose;
		float4x4 worldPrev;
		float4 color;
		float2 uvScale;
	};

	RWStructuredBuffer<ParticleStateGpu> g_particles : register(u0);
	RWStructuredBuffer<EmitterGpu> g_emitters : register(u1);
	AppendStructuredBuffer<MeshInstanceData> g_instances : register(u2);

	cbuffer ParticleSimConstants : register(b6)
	{
		float4 g_cameraPos;
		float4 g_cameraRight;
		float4 g_cameraUp;
		float4 g_cameraForward;
		float4 g_dtTime;
		float4 g_globalParams;
	};

	uint HashU32(uint x)
	{
		x ^= x >> 16;
		x *= 0x7feb352d;
		x ^= x >> 15;
		x *= 0x846ca68b;
		x ^= x >> 16;
		return x;
	}

	float Hash01(uint x)
	{
		return (HashU32(x) & 0x00ffffff) / 16777215.0f;
	}

	uint FindEmitterIndex(uint particleIndex, uint emitterCount)
	{
		[loop]
		for (uint i = 0; i < emitterCount; ++i)
		{
			EmitterGpu e = g_emitters[i];
			if (particleIndex >= e.poolStart && particleIndex < (e.poolStart + e.poolCount))
				return i;
		}
		return 0xffffffff;
	}

	float3 SampleShapePosition(EmitterGpu emitter, uint randomSeed)
	{
		const uint shapeType = emitter.flags & 0xffu;
		const float r0 = Hash01(randomSeed * 31u + 1u);
		const float r1 = Hash01(randomSeed * 31u + 2u);
		const float r2 = Hash01(randomSeed * 31u + 3u);

		if (shapeType == 1u || shapeType == 2u)
		{
			float phi = r0 * 6.283185307f;
			float cosTheta = (shapeType == 2u) ? saturate(r1) : (r1 * 2.0f - 1.0f);
			float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
			float radius = lerp(emitter.shapeParams0.y, emitter.shapeParams0.x, r2);
			float3 dir = float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
			return emitter.emitterPosition.xyz + dir * radius;
		}
		if (shapeType == 3u)
		{
			float3 ext = emitter.shapeParams1.xyz;
			return emitter.emitterPosition.xyz + (float3(r0, r1, r2) * 2.0f - 1.0f) * ext;
		}
		if (shapeType == 6u)
		{
			float t = r0 * 2.0f - 1.0f;
			return emitter.emitterPosition.xyz + emitter.emitterForward.xyz * emitter.shapeParams0.w * t;
		}

		return emitter.emitterPosition.xyz;
	}

	float3 SampleSpawnVelocity(EmitterGpu emitter, uint randomSeed)
	{
		float3 dir = normalize(float3(
			Hash01(randomSeed * 17u + 11u) * 2.0f - 1.0f,
			Hash01(randomSeed * 17u + 12u) * 2.0f - 1.0f,
			Hash01(randomSeed * 17u + 13u) * 2.0f - 1.0f));

		if (length(dir) < 0.001f)
			dir = float3(0.0f, 1.0f, 0.0f);

		const float minSpeed = emitter.lifeParams.z;
		const float maxSpeed = emitter.lifeParams.w;
		float speed = lerp(minSpeed, max(maxSpeed, minSpeed), Hash01(randomSeed * 23u + 7u));
		return dir * speed + emitter.emitterVelocity.xyz * emitter.simulation2.z;
	}

	float EvaluateAlphaOverLifetime(EmitterGpu emitter, float lifeN)
	{
		const uint useThreePointCurve = (emitter.flags >> 16u) & 1u;
		if (useThreePointCurve == 0u)
		{
			return lerp(emitter.simulation3.x, emitter.simulation3.y, lifeN);
		}

		const float midT = saturate(emitter.simulation4.w);
		const float safeMidT = clamp(midT, 0.001f, 0.999f);
		if (lifeN <= safeMidT)
		{
			const float t = saturate(lifeN / safeMidT);
			return lerp(emitter.simulation4.x, emitter.simulation4.y, t);
		}

		const float t = saturate((lifeN - safeMidT) / (1.0f - safeMidT));
		return lerp(emitter.simulation4.y, emitter.simulation4.z, t);
	}

	void SpawnParticle(inout ParticleStateGpu state, EmitterGpu emitter, uint particleIndex, uint emitterIndex, uint localIndexInPool)
	{
		const uint spawnQuota = (uint)max(emitter.spawnRemaining, 0);
		if (localIndexInPool >= spawnQuota)
			return;

		uint seed = HashU32(particleIndex * 1664525u + (uint)g_dtTime.y + emitterIndex * 1013904223u);
		state.position.xyz = SampleShapePosition(emitter, seed);
		state.position.w = 1.0f;
		state.velocity.xyz = SampleSpawnVelocity(emitter, seed);
		state.velocity.w = 0.0f;
		state.color = emitter.colorStart;
		state.color.w = saturate(state.color.w * EvaluateAlphaOverLifetime(emitter, 0.0f));

		float lifeMin = max(0.01f, emitter.lifeParams.x);
		float lifeMax = max(lifeMin, emitter.lifeParams.y);
		float lifetime = lerp(lifeMin, lifeMax, Hash01(seed + 3u));
		float size = max(0.001f, emitter.simulation2.w);
		state.misc0 = float4(size, Hash01(seed + 11u) * 6.283185307f, 0.0f, lifetime);
		state.misc1 = float4(1.0f, (float)seed, 0.0f, (float)emitterIndex);
	}

	[numthreads(64, 1, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		uint particleCount = (uint)g_globalParams.x;
		uint emitterCount = (uint)g_globalParams.y;
		if (tid.x >= particleCount || emitterCount == 0u)
			return;

		uint emitterIndex = FindEmitterIndex(tid.x, emitterCount);
		if (emitterIndex == 0xffffffff)
			return;

		EmitterGpu emitter = g_emitters[emitterIndex];
		const uint localIndexInPool = tid.x - emitter.poolStart;
		ParticleStateGpu state = g_particles[tid.x];

		if (state.misc1.x < 0.5f)
		{
			SpawnParticle(state, emitter, tid.x, emitterIndex, localIndexInPool);
		}
		else
		{
			float dt = g_dtTime.x;
			float age = state.misc0.z + dt;
			float life = max(0.01f, state.misc0.w);
			if (age >= life)
			{
				state.misc1.x = 0.0f;
			}
			else
			{
				float lifeN = saturate(age / life);
				float drag = max(0.0f, emitter.simulation0.w);
				float3 accel = emitter.simulation0.xyz + emitter.simulation1.xyz;
				float noiseAmp = emitter.simulation2.x;
				if (noiseAmp > 0.0001f)
				{
					uint seed = (uint)state.misc1.y + (uint)(g_dtTime.y * 60.0f);
					float3 noise = float3(
						Hash01(seed + 41u) * 2.0f - 1.0f,
						Hash01(seed + 59u) * 2.0f - 1.0f,
						Hash01(seed + 73u) * 2.0f - 1.0f);
					accel += noise * noiseAmp;
				}

				state.velocity.xyz += accel * dt;
				state.velocity.xyz *= (1.0f / (1.0f + drag * dt));
				state.position.xyz += state.velocity.xyz * dt;
				state.misc0.z = age;

				state.color = lerp(emitter.colorStart, emitter.colorEnd, lifeN);
				state.color.w = saturate(state.color.w * EvaluateAlphaOverLifetime(emitter, lifeN));
				float sizeStart = max(0.001f, emitter.simulation2.w);
				float sizeEnd = max(0.001f, emitter.simulation1.w);
				state.misc0.x = lerp(sizeStart, sizeEnd, lifeN);
			}
		}

		if (state.misc1.x > 0.5f)
		{
			MeshInstanceData inst = (MeshInstanceData)0;
			float size = max(0.001f, state.misc0.x);
			float3 camRight = normalize(g_cameraRight.xyz);
			float3 camUp = normalize(g_cameraUp.xyz);

			float4x4 world = float4x4(
				float4(camRight * size, 0.0f),
				float4(camUp * size, 0.0f),
				float4(normalize(cross(camRight, camUp)) * size, 0.0f),
				float4(state.position.xyz, 1.0f));

			inst.world = world;
			inst.worldInverseTranspose = world;
			inst.worldPrev = world;
			inst.color = state.color;
			inst.uvScale = float2(1.0f, 1.0f);
			g_instances.Append(inst);
		}

		g_particles[tid.x] = state;
	}
}
