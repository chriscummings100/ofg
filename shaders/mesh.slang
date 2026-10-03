// Portable indexed mesh diagnostic: entity/camera transform and independent material tint over a UV checker.
struct DrawParameters
{
    column_major float4x4 clipFromLocal;
};
struct MaterialParameters
{
    float4 tint;
};
ConstantBuffer<DrawParameters> draw;
ConstantBuffer<MaterialParameters> material;

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Maps local vertices into the selected camera's homogeneous clip space.
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(draw.clipFromLocal, float4(input.position, 1));
    output.uv = input.uv;
    return output;
}

// Uses UV-space cells so the diagnostic pattern follows the cube surface.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    int2 cell = int2(floor(input.uv * 6));
    float value = ((cell.x + cell.y) & 1) == 0 ? 0.22 : 0.9;
    return float4(value * material.tint.rgb, material.tint.a);
}
