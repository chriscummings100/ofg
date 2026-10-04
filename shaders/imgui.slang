// ImGui's packed display-space colours are decoded before compositing with display-linear scene images.
Texture2D<float4> image;
SamplerState imageSampler;
struct Parameters
{
    float2 scale;
    float2 offset;
};
ConstantBuffer<Parameters> ui;
struct VertexInput
{
    float2 position : POSITION0;
    float2 uv : TEXCOORD1;
    float4 color : COLOR2;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR1;
};

// Maps logical ImGui coordinates into clip space; explicit input semantic indices match the WebGPU ABI.
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position * ui.scale + ui.offset, 0, 1);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

// Font atlas RGB is white; external scene images are already display-linear and must not be decoded again.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    float3 c = input.color.rgb;
    float3 linear = select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
    return float4(linear, input.color.a) * image.Sample(imageSampler, input.uv);
}
