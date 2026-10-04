// Test-only compute-to-vertex proof. Explicit scalar byte offsets match the existing 72-byte OFG Vertex.
#ifdef COMPUTE_PROOF
ByteAddressBuffer sourceVertices;
ByteAddressBuffer translation;
RWByteAddressBuffer deformedVertices;

// Copies every attribute exactly and translates only position; the 65th vertex exercises the group tail.
[shader("compute")][numthreads(64, 1, 1)] void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint count;
    sourceVertices.GetDimensions(count);
    if (thread.x >= count / 72)
    {
        return;
    }
    uint base = thread.x * 72;
    for (uint word = 0; word < 18; ++word)
    {
        deformedVertices.Store(base + word * 4, sourceVertices.Load(base + word * 4));
    }
    float3 position = asfloat(sourceVertices.Load3(base));
    deformedVertices.Store3(base, asuint(position + asfloat(translation.Load3(0))));
}

#else
struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

// Consumes the compute output as an ordinary vertex stream, without a storage-buffer vertex fetch.
[shader("vertex")] VertexOutput vertexMain(float3 position : POSITION0, float4 color : COLOR1)
{
    VertexOutput result;
    result.position = float4(position, 1);
    result.color = color;
    return result;
}

    // Flat fixture color makes absence or stale placement of the computed triangle obvious.
    [shader("fragment")] float4 fragmentMain(VertexOutput input)
    : SV_Target
{
    return input.color;
}
#endif
