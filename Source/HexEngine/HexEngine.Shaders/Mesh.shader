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
	MeshPixelInput ShaderMain(MeshVertexInput input, MeshInstanceData instance)
	{
		MeshPixelInput output;
		
		input.position.w = 1.0f;

		//input.position += float4(instance.instancePos, 0.0f);

		output.position = mul(input.position, instance.world);
		output.position = mul(output.position, g_viewProjectionMatrix);

		// Calculate the light dir
		output.positionWS = mul(input.position, instance.world);
		//output.lightDir = g_lightPosition.xyz - worldPosition.xyz;
		//output.lightDir = normalize(output.lightDir);
		
		// Calculate the position of the vertice as viewed by the light source.
		float4 lightView = mul(input.position, instance.world);
		lightView = mul(lightView, g_lightViewMatrix);

		output.lightViewPosition1 = mul(lightView, g_lightProjectionMatrix[0]);
		output.lightViewPosition2 = mul(lightView, g_lightProjectionMatrix[1]);
		output.lightViewPosition3 = mul(lightView, g_lightProjectionMatrix[2]);
		output.lightViewPosition4 = mul(lightView, g_lightProjectionMatrix[3]);
				
		output.texcoord = input.texcoord;	
		
		output.normal = mul(input.normal, (float3x3)instance.world);
		output.normal = normalize(output.normal);
		
		output.tangent = mul(input.tangent, (float3x3)instance.world);
		output.tangent = normalize(output.tangent);
		
		output.binormal = mul(input.binormal, (float3x3)instance.world);
		output.binormal = normalize(output.binormal);
		
		// Determine the viewing direction based on the position of the camera and the position of the vertex in the world.
		output.viewDirection = g_eyePos.xyz - output.positionWS.xyz;

		// Normalize the viewing direction vector.
		output.viewDirection = normalize(output.viewDirection);

		output.colour = instance.colour;
		
		return output;
	}
}
"PixelShader"
{
	Texture2D shaderTexture : register(t0);
	Texture2D normalMap : register(t1);
	Texture2D specularMap : register(t2);

	Texture2D depthMapTexture1 : register(t3);
	Texture2D depthMapTexture2 : register(t4);
	Texture2D depthMapTexture3 : register(t5);
	Texture2D depthMapTexture4 : register(t6);
	
	SamplerState TextureSampler : register(s0);
	//SamplerState SampleTypeWrap  : register(s1);
	
	//static float4 ambientColor = float4(0.2f, 0.2f, 0.2f, 1.0f);
	static const float g_shadowBlendRange = 30.0f; // the range at which shadow cascades start being blended together
	static const float g_shadowPcfFactorFar = 3.5f; // the pcf to use when far enough away for it to not matter
	static const float g_shadowPcfFactorNear = 5.5f; // the pcf to use when close up and needs to be smoother
	static const float g_shadowPcfNear = 10.0f;
	static const float g_shadowPcfFar = 80.0f;
	
	SamplerComparisonState cmpSampler : register(s1);
	
	float2 texOffset( float u, float v )
	{
		return float2( u * 1.0f/ 4096.0f, v * 1.0f/ 4096.0f);
	}

	float2 VogelDiskSample(int sampleIndex, int samplesCount, float phi)
	{
		float GoldenAngle = 2.4f;

		float r = sqrt((float)sampleIndex + 0.5f) / sqrt((float)samplesCount);
		float theta = (float)sampleIndex * GoldenAngle + phi;

		float sine, cosine;
		sincos(theta, sine, cosine);

		return float2(r * cosine, r * sine);
	}

	float InterleavedGradientNoise(float2 position_screen)
	{
		float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
		return frac(magic.z * frac(dot(position_screen, magic.xy)));
	}

	/*float Penumbra(float gradientNoise, float2 shadowMapUV, float z_shadowMapView, int samplesCount)
	{
		float avgBlockersDepth = 0.0f;
		float blockersCount = 0.0f;

		for (int i = 0; i < samplesCount; i++)
		{
			float2 sampleUV = VogelDiskOffset(i, samplesCount, gradientNoise);
			sampleUV = shadowMapUV + penumbraFilterMaxSize * sampleUV;

			floatsampleDepth = shadowMapTexture.SampleLevel(pointClampSampler, sampleUV, 0).x;

			if (sampleDepth < z_shadowMapView)
			{
				avgBlockersDepth += sampleDepth;
				blockersCount += 1.0f;
			}
		}

		if (blockersCount > 0.0f)
		{
			avgBlockersDepth /= blockersCount;
			return AvgBlockersDepthToPenumbra(z_shadowMapView, avgBlockersDepth);
		}
		else
		{
			return 0.0f;
		}
	}*/

	float sampleDepth(Texture2D depthMap, float lightDepthValue, float2 projectTexCoord, float pcfFactor, float2 screenPos)
	{
		float sum = 0;
		float x, y;
		int num = 0;

		float gradientNoise = InterleavedGradientNoise(screenPos);

		//float penumbra = Penumbra(gradientNoise, projectTexCoord, z_shadowMapView, 16);
		float penumbra = 1.0f;
		float shadowFilterMaxSize = 1.0f;

		float shadow = 0.0f;
		for (int i = 0; i < 16; i++)
		{
			float2 sampleUV = VogelDiskSample(i, 16, gradientNoise * 2 * 3.14159265);
			//sampleUV = projectTexCoord + sampleUV * penumbra * shadowFilterMaxSize;

			shadow += depthMap.SampleCmp(cmpSampler, projectTexCoord.xy + texOffset(4.0f * sampleUV.x, 4.0f * sampleUV.y), lightDepthValue).x;
		}
		shadow /= 16.0f;
		return shadow;

		//sum += depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy + texOffset(x, y), lightDepthValue).r;

		//perform PCF filtering on a 4 x 4 texel neighborhood
		for (y = -pcfFactor; y <= pcfFactor; y += 1.0)
		{
			for (x = -pcfFactor; x <= pcfFactor; x += 1.0)
			{
				sum += depthMap.SampleCmpLevelZero(cmpSampler, projectTexCoord.xy + texOffset(x, y), lightDepthValue).r;
				num = num + 1;
			}
		}

		return sum / (float)num;
	}

	float4 ShaderMain(MeshPixelInput input) : SV_Target
	{
		float bias;
		float4 color;
		float2 projectTexCoord;
		float depthValue = 1.0f;
		float lightDepthValue;
		float lightIntensity;
		float4 albedo;	
		float3 reflection;
		float4 specular;

		// Set the bias value for fixing the floating point precision issues.
		bias = 0;// 0.000001f;

		albedo = shaderTexture.Sample(TextureSampler, input.texcoord) * input.colour;// g_material.diffuseColour;

		// Set the default output color to the ambient light value for all pixels.
		color = albedo * g_atmosphere.ambientLight;

		// Initialize the specular color.
		specular = float4(0.0f, 0.0f, 0.0f, 0.0f);
		
		int index = 0;

		float4 lightViewPosition;
		float shadowDelta = 0.0f;

		float4 worldViewPosition = mul(input.positionWS, g_viewMatrix);
		float pixelDepth = -worldViewPosition.z;

		// calculate the pcf factor
		//
		float shadowPcfFactor = g_shadowPcfFactorFar; // default to far factor

		// The following code will smooth shadows in the closer they are to the camera, to hide any jitter
		/*
		if (input.pixelDepth <= g_shadowPcfFar)
		{
			float range = g_shadowPcfFar - g_shadowPcfNear;

			float lerpRange = saturate((input.pixelDepth - g_shadowPcfNear) / range);

			shadowPcfFactor = lerp(g_shadowPcfFactorNear, g_shadowPcfFactorFar, lerpRange);
		}
		*/

		// PCSS
		// first calculate the pixel depth from the light source
		float pixelDepthLight = -mul(input.positionWS, g_viewMatrix).z;
		//float4 worldViewPosition = 
		//output.pixelDepth = -worldViewPosition.z;

		// Calculate the position of the vertice as viewed by the light source.
		//float4 lightView = mul(input.position, instance.instanceWorld);
		//lightView = mul(lightView, g_lightViewMatrix);

		//output.lightViewPosition1 = mul(lightView, g_lightProjectionMatrix[0]);

		if (pixelDepth <= g_frustumDepths[0])
		{
			index = 0;
			lightViewPosition = input.lightViewPosition1;

			shadowDelta = g_frustumDepths[0] - pixelDepth;
		}
		else if (pixelDepth <= g_frustumDepths[1])
		{
			index = 1;
			lightViewPosition = input.lightViewPosition2;

			shadowDelta = g_frustumDepths[1] - pixelDepth;
		}
		else if(pixelDepth <= g_frustumDepths[2])
		{
			index = 2;
			lightViewPosition = input.lightViewPosition3;

			shadowDelta = g_frustumDepths[2] - pixelDepth;
		}
		else
		{
			index = 3;
			lightViewPosition = input.lightViewPosition4;

			shadowDelta = g_frustumDepths[3] - pixelDepth;
		}

		// Calculate the projected texture coordinates.
		projectTexCoord.x =  lightViewPosition.x / lightViewPosition.w / 2.0f + 0.5f;
		projectTexCoord.y = -lightViewPosition.y / lightViewPosition.w / 2.0f + 0.5f;

		// Determine if the projected coordinates are in the 0 to 1 range.  If so then this pixel is in the view of the light.
		if((saturate(projectTexCoord.x) == projectTexCoord.x) && (saturate(projectTexCoord.y) == projectTexCoord.y))
		{
			// Calculate the depth of the light.
			lightDepthValue = (lightViewPosition.z / lightViewPosition.w);
			
			// Subtract the bias from the lightDepthValue.
			lightDepthValue = lightDepthValue - bias;
			
			if(index == 0)		
			{
				depthValue = sampleDepth(depthMapTexture1, lightDepthValue, projectTexCoord, shadowPcfFactor, input.position.xy);// depthMapTexture1.Sample(SampleTypeClamp, projectTexCoord).r;

				// sample the next depth and lerp between them
				if (shadowDelta < g_shadowBlendRange)
				{
					projectTexCoord.x = input.lightViewPosition2.x / input.lightViewPosition2.w / 2.0f + 0.5f;
					projectTexCoord.y = -input.lightViewPosition2.y / input.lightViewPosition2.w / 2.0f + 0.5f;

					float lightDepthValueNext = (input.lightViewPosition2.z / input.lightViewPosition2.w) - bias;

					float nextCascadeDepth = sampleDepth(depthMapTexture2, lightDepthValueNext, projectTexCoord, shadowPcfFactor, input.position.xy);

					float lerpValue = shadowDelta / g_shadowBlendRange;

					depthValue = lerp(depthValue, nextCascadeDepth, 1.0f - lerpValue);
				}
					
			}
			else if(index == 1)
			{
				depthValue = sampleDepth(depthMapTexture2, lightDepthValue, projectTexCoord, shadowPcfFactor, input.position.xy);// depthMapTexture2.Sample(SampleTypeClamp, projectTexCoord).r;
			}
			else if (index == 2)
			{
				depthValue = sampleDepth(depthMapTexture3, lightDepthValue, projectTexCoord, shadowPcfFactor, input.position.xy);// depthMapTexture2.Sample(SampleTypeClamp, projectTexCoord).r;
			}
			else
			{
				depthValue = sampleDepth(depthMapTexture4, lightDepthValue, projectTexCoord, shadowPcfFactor, input.position.xy);// depthMapTexture3.Sample(SampleTypeClamp, projectTexCoord).r;
			}			
		}

		float3 eyeVector = normalize(g_eyePos.xyz - input.positionWS.xyz);
		float3 lightVector = normalize(input.positionWS.xyz - g_lightPosition.xyz);
		float3 worldNormal = normalize(input.normal);
		float3 lightDir = -normalize(g_lightDirection);

		// BUMP MAPPING
		if (g_objectFlags & OBJECT_FLAGS_HAS_BUMP)
		{
			// Sample the pixel in the bump map.
			float4 bumpMap = normalMap.Sample(TextureSampler, input.texcoord);

			// Expand the range of the normal value from (0, +1) to (-1, +1).
			bumpMap = (bumpMap * 2.0f) - 1.0f;

			// Calculate the normal from the data in the bump map.
			float3 bumpNormal = (bumpMap.x * input.tangent) + (bumpMap.y * input.binormal) + (bumpMap.z * input.normal);

			// Normalize the resulting bump normal.
			worldNormal = normalize(bumpNormal);
		}

		lightIntensity = saturate(dot(worldNormal, lightDir)) * g_globalLight[0];
		//float lightIntensity2 = saturate(dot(worldNormal, lightDir)) * g_globalLight[0];

		if (lightIntensity > 0.0f)
		{
			//color = float4(1,1,1,1);

			//clamp it a bit to always allow some lighting even in the shadow
			if (depthValue < 0.11f)
				depthValue = 0.11f;

			// Determine the final diffuse color based on the diffuse color and the amount of light intensity.					
			color += (albedo * lightIntensity * depthValue);

			// Saturate the final light color.
			color = saturate(color);

			// Calculate the reflection vector based on the light intensity, normal vector, and light direction.
			reflection = normalize(2 * lightIntensity * worldNormal - lightDir);

			float depthValue2 = depthValue;

			float shinyPower = g_material.shininess;

			if (shinyPower < 10.0f)
				shinyPower = 10.0f;

			// Determine the amount of specular light based on the reflection vector, viewing direction, and specular power.
			specular = pow(saturate(dot(reflection, eyeVector)), shinyPower) * g_material.shininessStrength;

			if ((g_objectFlags & OBJECT_FLAGS_HAS_SPECULAR) != 0)
			{
				float4 specularIntensity = specularMap.Sample(TextureSampler, input.texcoord);

				specular = specular * specularIntensity.r;// *depthValue2;
			}

			//color = color + specular;
		}		
		
		// Combine the light and texture color.
		//color = color * textureColor;

		// Add the specular component last to the output color.
		color = saturate(color + (specular * depthValue));// +g_material.emissiveColour);

		float fogLerp = saturate((pixelDepth - (g_frustumDepths[3] - 1500.0f)) / 1500.0f);

		color = lerp(color, float4(g_globalLight[1], g_globalLight[2], g_globalLight[3], 1.0f), fogLerp);

		return float4(color.rgb, albedo.a);
	}
}