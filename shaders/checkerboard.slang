// Shared diagnostic image: 64-pixel squares, dark at the top-left, in framebuffer pixel coordinates.

// Covers the viewport with one triangle; no vertex buffers or interpolated texture coordinates are needed.
[shader("vertex")]
float4 vertexMain(uint vertexId : SV_VertexID) : SV_Position
{
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

// Produces exact RGBA8 UNORM levels (32 and 224) for deterministic readback checks.
[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target
{
    uint2 square = uint2(position.xy) / 64;
    float level = ((square.x + square.y) & 1) == 0 ? 32.0 / 255.0 : 224.0 / 255.0;
    return float4(level, level, level, 1.0);
}
