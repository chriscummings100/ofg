// Area-weighted reduction between disjoint mip views; RGB is linear after sRGB texture loads.
Texture2D<float4> sourceImage;
struct MipParameters
{
    uint2 destinationSize;
    uint alphaWeighted;
};
ConstantBuffer<MipParameters> mip;

// Covers one destination level without vertex buffers.
[shader("vertex")] float4 vertexMain(uint id : SV_VertexID)
    : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

// Integrates source texels over each destination footprint, including odd-size boundary fractions.
[shader("fragment")] float4 fragmentMain(float4 position : SV_Position)
    : SV_Target
{
    uint width, height;
    sourceImage.GetDimensions(width, height);
    float2 scale = float2(width, height) / float2(mip.destinationSize);
    float2 first = floor(position.xy) * scale;
    float2 last = min(first + scale, float2(width, height));
    float4 sum = 0;
    float total = 0;
    for (int y = int(floor(first.y)); y < int(ceil(last.y)); ++y)
    {
        for (int x = int(floor(first.x)); x < int(ceil(last.x)); ++x)
        {
            float2 weight = max(0.0, min(last, float2(x + 1, y + 1)) - max(first, float2(x, y)));
            float area = weight.x * weight.y;
            float4 value = sourceImage.Load(int3(x, y, 0));
            if (mip.alphaWeighted != 0)
            {
                value.rgb *= value.a;
            }
            sum += value * area;
            total += area;
        }
    }
    float4 result = sum / total;
    if (mip.alphaWeighted != 0)
    {
        result.rgb = result.a > 0 ? result.rgb / result.a : float3(0);
    }
    return result;
}
