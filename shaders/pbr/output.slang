// Fullscreen HDR mapping; exact display encoding is selected for the actual attachment format.
Texture2D<float4> sourceImage;
struct OutputParameters { float exposure; uint linearOutput; uint encodeSrgb; uint debugView; };
ConstantBuffer<OutputParameters> outputSettings;
// Generates one fullscreen triangle without vertex resources.
[shader("vertex")] float4 vertexMain(uint id:SV_VertexID):SV_Position
{
    float2 p=float2((id<<1)&2,id&2);return float4(p*2-1,0,1);
}
// Maps HDR after opaque lighting. Unlit and sorted transparency compose afterward in display-linear space.
[shader("fragment")] float4 fragmentMain(float4 p:SV_Position):SV_Target
{
    float3 c=sourceImage.Load(int3(int2(p.xy),0)).rgb;
    if (outputSettings.debugView==0)
        c=outputSettings.linearOutput!=0 ? c*outputSettings.exposure : pbrNeutral(c*outputSettings.exposure);
    if (outputSettings.encodeSrgb!=0) c=linearToSrgb(c);
    return float4(c,1);
}
