"InputLayout"
{
    PosTexColour
}
"VertexShaderIncludes"
{
    UICommon
}
"PixelShaderIncludes"
{
    UICommon
}
"VertexShader"
{
    UIPixelInput ShaderMain(UIVertexInput input)
    {
        UIPixelInput output;

        output.position = input.position;
        output.texcoord = input.texcoord;

        return output;
    }
}
"PixelShader"
{
    GBUFFER_RESOURCE(0, 1, 2, 3, 4);

    Texture2D shaderTexture : register(t5);

    SamplerState TextureSampler : register(s0);
    SamplerState PointSampler : register(s2);

    static const float weight[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };

    float4 ShaderMain(UIPixelInput input) : SV_Target
    {
        float4 colour = shaderTexture.Sample(PointSampler, input.texcoord) * g_material.diffuseColour;

        //float Pi = 6.28318530718; // Pi*2

        //float4 normalAndPos = GBUFFER_NORMAL.Sample(PointSampler, input.texcoord);

        int blurAmount = 5;

        // don't blur the background
        //if (normalAndPos.w == -1)
        //    blurAmount = 3;

        //// GAUSSIAN BLUR SETTINGS {{{
        //float Directions = 22.0; // BLUR DIRECTIONS (Default 16.0 - More is better but slower)
        //float Quality = 8.0; // BLUR QUALITY (Default 4.0 - More is better but slower)
        //float Size = 20.0; // BLUR SIZE (Radius)
        //// GAUSSIAN BLUR SETTINGS }}}

        //float2 Radius = Size / float2(g_screenWidth, g_screenHeight);

        //// Normalized pixel coordinates (from 0 to 1)
        //float2 uv = input.position.xy / float2(g_screenWidth, g_screenHeight);
        //// Pixel colour
        //float4 Color = colour;

        //int samples = 0;

        //// Blur calculations
        //for (float d = 0.0; d < Pi; d += Pi / Directions)
        //{
        //    for (float i = 1.0 / Quality; i <= 1.0; i += 1.0 / Quality)
        //    {
        //        Color += shaderTexture.Sample(PointSampler, uv + float2(cos(d), sin(d)) * Radius * i);
        //        samples++;
        //    }
        //}

        //// Output to screen
        //Color /= (float)samples;// Quality* Directions - 15.0;

        //return float4(Color.rgb, colour.a);

        //float2 TexCoords = input.position.xy / float2(g_screenWidth / 2, g_screenHeight / 2);

        float2 tex_offset = 1.0 / float2(g_screenWidth, g_screenHeight); // gets size of single texel
        float3 result = colour.rgb * weight[0];// texture(image, TexCoords).rgb* weight[0]; // current fragment's contribution

        for (int i = 1; i < blurAmount; ++i)
        {
            result += shaderTexture.Sample(PointSampler, input.texcoord.xy + float2(tex_offset.x * i, 0.0)).rgb * weight[i];
            result += shaderTexture.Sample(PointSampler, input.texcoord.xy - float2(tex_offset.x * i, 0.0)).rgb * weight[i];
        }

        colour = float4(result, 1.0);

        return float4(colour.rgb, colour.a);
    }
}