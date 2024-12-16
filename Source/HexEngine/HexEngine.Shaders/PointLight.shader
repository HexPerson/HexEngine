"Requirements"
{
	GBuffer
}
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
	PBRutils
}
"VertexShader"
{
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output = (MeshPixelInput)0;

		input.position.w = 1.0f;

		output.position = mul(input.position, instance.world);
		output.position = mul(output.position, g_viewProjectionMatrix);

		output.positionWS = float4(instance.world[3].xyz, instance.world[0].x);// mul(input.position, instance.instanceWorld);

		output.colour = instance.colour;

		// we'll use this for radius and strength
		output.texcoord = instance.uvScale;

		return output;
	}
}
"PixelShader"
{
	GBUFFER_RESOURCE(0, 1, 2, 3, 4);

	Texture2D g_beautyTex : register(t13);

	SamplerState g_textureSampler : register(s0);
	SamplerState g_pointSampler : register(s2);

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		float2 projectTexCoord;

		float2 screenPos = float2(input.position.x / (float)g_screenWidth, input.position.y / (float)g_screenHeight);
	

		// Sample the gbuffer
		//
		float4 pixelColour = GBUFFER_DIFFUSE.Sample(g_pointSampler, screenPos);
		float4 pixelNormal = GBUFFER_NORMAL.Sample(g_pointSampler, screenPos);
		float4 pixelPosWS = GBUFFER_POSITION.Sample(g_pointSampler, screenPos);
		//float4 pixelSpecular = GBUFFER_SPECULAR.Sample(g_pointSampler, screenPos);

		//return float4(pixelColour.rgb, 1.0f);


		float3 eyeVector = normalize(g_eyePos.xyz - pixelPosWS.xyz);

		float4 lightPos = float4(input.positionWS.xyz, 1.0f);// float4(5, 6, 250, 1);
		float lightRange = input.texcoord.x;//g_lightRadius;//input.positionWS.w;// *0.9f;

		float3 lightToPixelVec = lightPos.xyz - pixelPosWS.xyz;

		float d = length(lightToPixelVec);// / 0.9f;

		if (d > lightRange)
			return float4(0.0f, 0.0f, 0.0f, 0.0f);

		lightToPixelVec /= d;
		//lightToPixelVec = normalize(lightToPixelVec);

		//return float4(lightToPixelVec, 1.0f);
			

		float4 finalColor = float4(0.0f, 0.0f, 0.0f, 0.0f);// realColour;// 
			
		float4 lightDiffuse = float4(input.colour.rgb, 1.0f);
		float lightIntensity = input.colour.a;
		//float shinyPower = pixelSpecular.w;
		//float shininessStrength = pixelColour.w;

		float howMuchLight = saturate(dot(lightToPixelVec, pixelNormal));
		float3 lightAtt = float3(0.01f, 0.10f, 0.009f);

		float attenuation = saturate(1.0f - saturate(d / lightRange));
		attenuation = pow(attenuation, 2);

		float4 pbr = CalculatePBRPointLighting(			
			GBUFFER_SPECULAR,
			g_pointSampler,
			screenPos,
			pixelNormal.xyz,
			pixelPosWS.xyz,
			lightToPixelVec,
			input.colour.rgb,
			pixelColour.rgb,
			1.0f,
			attenuation
			);

		return float4(pbr.rgb * lightIntensity, 1.0f);

		//float4 specular = float4(0.0f, 0.0f, 0.0f, 0.0f);

		if (howMuchLight > 0.0f)
		{
			// calculate diffuse
			//
			finalColor += /*howMuchLight **/ lightDiffuse * float4(pixelColour.rgb, 1.0f);

			finalColor /= lightAtt[0] + (lightAtt[1] * d) + (lightAtt[2] * (d * d));

			// calculate specular
			//
			float3 reflection = normalize(2 * howMuchLight * pixelNormal - lightToPixelVec);

			
			// Determine the amount of specular light based on the reflection vector, viewing direction, and specular power.
			//specular = pow(saturate(dot(reflection, eyeVector)), shinyPower) * shininessStrength;

			//specular.rgb = specular.rgb * pixelSpecular.rgb * input.colour.rgb /** howMuchLight*/;
				
			//specular = float4(specular.rgb * specularMap.rgb, specular.a) * lightDiffuse;

			//specular /= lightAtt[0] + (lightAtt[1] * d) + (lightAtt[2] * (d * d));
		}

		//return attenuation * lightIntensity * finalColor + specular * attenuation;

		return (lightIntensity * finalColor /* + specular*/) * attenuation;
	}
}