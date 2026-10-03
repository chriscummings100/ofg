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
    return sourceImage.Load(int3(int2(position.xy), 0));
}
