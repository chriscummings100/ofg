// Deterministic layered cloud optical depth, shared by visible sky, environment capture and surface shadows.
// Hash noise is evaluated in world XZ so camera movement cannot drag the cloud field.
float cloudHash(float2 p)
{
    float3 q = frac(float3(p.x, p.y, p.x) * .1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}
// Smooth value noise with reproducible neighbouring lattice values.
float cloudNoise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    f = f * f * (3 - 2 * f);
    return lerp(
        lerp(cloudHash(i), cloudHash(i + float2(1, 0)), f.x),
        lerp(cloudHash(i + float2(0, 1)), cloudHash(i + 1), f.x),
        f.y
    );
}
// Computes transmission through the two horizontal layers; near-horizontal rays fade with their source light.
float cloudTransmission(float3 origin, float3 direction, float4 settings, float4 windLayers)
{
    if (settings.x <= 0 || settings.y <= 0 || direction.y <= 0)
    {
        return 1;
    }
    float opticalDepth = 0;
    [unroll] for (uint layer = 0; layer < 2; ++layer)
    {
        float height = layer == 0 ? windLayers.z : windLayers.w;
        float t = (height - origin.y) / max(direction.y, .01);
        if (t <= 0)
        {
            continue;
        }
        float2 p = (origin.xz + direction.xz * t + windLayers.xy) * (settings.z * (layer == 0 ? 1 : .6));
        p += float(layer) * 71.3;
        float n = cloudNoise(p) * .57 + cloudNoise(p * 2.03) * .28 + cloudNoise(p * 4.09) * .15;
        float density = smoothstep(1 - settings.x, 1 - settings.x + .2, n);
        // Blend unresolved horizon detail toward its mean to avoid a flickering lattice at grazing angles.
        density = lerp(pow(settings.x, 3), density, smoothstep(.015, .08, direction.y));
        opticalDepth += density * settings.y / max(direction.y, .15);
    }
    return exp(-opticalDepth);
}
