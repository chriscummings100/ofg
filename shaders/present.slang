// Host image presentation without uniforms, samplers or staging maps; source and destination sizes match.
Texture2D<float4> sourceImage;

// Covers the target with one generated fullscreen triangle.
[shader("vertex")]
float4 vertexMain(uint vertexId : SV_VertexID) : SV_Position
{
    float2 position = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(position * 2.0 - 1.0, 0.0, 1.0);
}

// Loads the corresponding pixel directly; sRGB views perform their ordinary decode/encode conversions.
[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target
{
    float4 color = sourceImage.Load(int3(int2(position.xy), 0));
#ifdef ENCODE_SRGB
    float3 c = max(color.rgb, 0.0);
    color.rgb = select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
#endif
    return color;
}
