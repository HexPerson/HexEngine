"InputLayout"
{
	PosNormTanBinTex_INSTANCED
}
"VertexShaderIncludes"
{
	MeshCommon
}
"PixelShaderIncludes"
{
	MeshCommon
}
"VertexShader"
{
	//static const float _Wavelength = 1.0f;
	//static const float _Amplitude = 0.001f;

	static const float WaveSizeMultiplier = 2.0f;

	float3 GerstnerWave(
		float4 wave, float3 p, inout float3 tangent, inout float3 binormal
	) {
		float steepness = wave.z / WaveSizeMultiplier;
		float wavelength = wave.w / WaveSizeMultiplier;
		float k = 2 * 3.14159f / wavelength;
		float c = sqrt(9.8 / k);
		float2 d = normalize(wave.xy);
		float f = k * (dot(d, p.xz) - c * g_time * 2);
		float a = steepness / k;

		//p.x += d.x * (a * cos(f));
		//p.y = a * sin(f);
		//p.z += d.y * (a * cos(f));

		tangent += float3(
			-d.x * d.x * (steepness * sin(f)),
			d.x * (steepness * cos(f)),
			-d.x * d.y * (steepness * sin(f))
			);
		binormal += float3(
			-d.x * d.y * (steepness * sin(f)),
			d.y * (steepness * cos(f)),
			-d.y * d.y * (steepness * sin(f))
			);
		return float3(
			d.x * (a * cos(f)),
			a * sin(f),
			d.y * (a * cos(f))
			);
	}

	static const float4 _WaveA = float4(0.6, 0.12, 0.10, 140);
	static const float4 _WaveB = float4(0.7, -1, 0.051, 125);
	static const float4 _WaveC = float4(0.4564, 0.348, 0.05, 20);
	static const float4 _WaveD = float4(-0.5, 0.12, 0.067, 175);

	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;

		//float time = g_time;// *10.0f;
		//const float waveScale = 0.6f;

		//float k = 2 * (3.14159) / _Wavelength;
		//float f = k * ((input.position.x + instance.worldPos.x) + time);

		//float dx = sin(input.position.x + instance.worldPos.x + time) * waveScale;
		//float dz = cos(input.position.z + instance.worldPos.z + time) * waveScale;

		//input.position.y += dx;
		//input.position.y += dz;

		float3 worldPos = instance.instanceWorld[3].xyz;


		float3 gridPoint = input.position.xyz + worldPos;

		float3 tangent = input.tangent;// float3(1, 0, 0); ;// float3(1, 0, 0);
		float3 binormal = input.binormal;// float3(0, 0, 1); ;// float3(0, 0, 1);
		float3 p = gridPoint;

		p += GerstnerWave(_WaveA, gridPoint, tangent, binormal);
		p += GerstnerWave(_WaveB, gridPoint, tangent, binormal);
		p += GerstnerWave(_WaveC, gridPoint, tangent, binormal);
		p += GerstnerWave(_WaveD, gridPoint, tangent, binormal);

		input.position = float4(p.xyz - worldPos, 1.0f);

		output.position = mul(input.position, instance.instanceWorld);
		output.position = mul(output.position, g_viewMatrix);
		output.position = mul(output.position, g_projectionMatrix);

		return output;
	}
}
"PixelShader"
{
	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		return float4(1,1,1,1);
	}
}