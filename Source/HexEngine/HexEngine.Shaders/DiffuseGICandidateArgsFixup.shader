"ComputeShader"
{
	// One-thread pass converting the candidate append count into real
	// DispatchIndirect args for the consuming voxelize pass: the historic
	// CopyStructureCount path wrote the RAW element count as the GROUP count,
	// a 64x thread overdispatch (surplus threads early-out on the live-count
	// guard, but the groups still launch and retire).
	Buffer<uint> g_candidateLiveCount : register(t0);
	RWBuffer<uint> g_dispatchArgs : register(u0);

	[numthreads(1, 1, 1)]
	void ShaderMain(uint3 tid : SV_DispatchThreadID)
	{
		const uint liveCount = g_candidateLiveCount[0];
		g_dispatchArgs[0] = (liveCount + 63u) / 64u;
		g_dispatchArgs[1] = 1u;
		g_dispatchArgs[2] = 1u;
	}
}
