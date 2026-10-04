// Affine all-influence skinning into OFG's 72-byte vertex layout; no float3 storage-buffer alignment assumptions.
ByteAddressBuffer sourceVertices;
ByteAddressBuffer influences; // Set-major, vertex-major: uint4 joints followed by float4 weights (32 bytes).
ByteAddressBuffer palette;    // Four packed float4 columns per mesh-local joint matrix (64 bytes).
RWByteAddressBuffer outputVertices;

// Applies every imported influence set, preserving attributes that are independent of deformation.
[shader("compute")][numthreads(64, 1, 1)] void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint vertexBytes, influenceBytes;
    sourceVertices.GetDimensions(vertexBytes);
    influences.GetDimensions(influenceBytes);
    uint count = vertexBytes / 72;
    uint vertex = thread.x;
    if (vertex >= count)
    {
        return;
    }
    uint sets = influenceBytes / 32 / count;
    float4 c0 = 0, c1 = 0, c2 = 0, c3 = 0;
    float totalWeight = 0;
    for (uint set = 0; set < sets; ++set)
    {
        uint address = (set * count + vertex) * 32;
        uint4 joints = influences.Load4(address);
        float4 weights = asfloat(influences.Load4(address + 16));
        for (uint i = 0; i < 4; ++i)
        {
            uint jointAddress = joints[i] * 64;
            float weight = weights[i];
            c0 += asfloat(palette.Load4(jointAddress)) * weight;
            c1 += asfloat(palette.Load4(jointAddress + 16)) * weight;
            c2 += asfloat(palette.Load4(jointAddress + 32)) * weight;
            c3 += asfloat(palette.Load4(jointAddress + 48)) * weight;
            totalWeight += weight;
        }
    }
    c0 /= totalWeight;
    c1 /= totalWeight;
    c2 /= totalWeight;
    c3 /= totalWeight;
    uint base = vertex * 72;
    for (uint word = 0; word < 18; ++word)
    {
        outputVertices.Store(base + word * 4, sourceVertices.Load(base + word * 4));
    }
    float3 position = asfloat(sourceVertices.Load3(base));
    position = c0.xyz * position.x + c1.xyz * position.y + c2.xyz * position.z + c3.xyz;
    outputVertices.Store3(base, asuint(position));

    // Cofactor columns give inverse transpose up to determinant. Keep its sign when normalizing.
    float3 co0 = cross(c1.xyz, c2.xyz), co1 = cross(c2.xyz, c0.xyz), co2 = cross(c0.xyz, c1.xyz);
    float determinant = dot(c0.xyz, co0);
    float3 normal = asfloat(sourceVertices.Load3(base + 12));
    float3 transformedNormal = co0 * normal.x + co1 * normal.y + co2 * normal.z;
    float normalLength = length(transformedNormal);
    normal = 0;
    if (determinant != 0 && isfinite(determinant) && normalLength > 0 && isfinite(normalLength))
    {
        normal = transformedNormal / normalLength * (determinant < 0 ? -1 : 1);
    }
    outputVertices.Store3(base + 12, asuint(normal));

    float4 tangent = asfloat(sourceVertices.Load4(base + 32));
    if (tangent.w != 0)
    {
        float3 direction = c0.xyz * tangent.x + c1.xyz * tangent.y + c2.xyz * tangent.z;
        float magnitude = length(direction);
        tangent.xyz = magnitude > 0 && isfinite(magnitude) ? direction / magnitude : float3(0);
        tangent.w *= determinant < 0 ? -1 : 1;
        outputVertices.Store4(base + 32, asuint(tangent));
    }
}
