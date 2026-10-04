// Raster entry points for optical-depth, multiple-scattering and sky-view lookup generation.
#if SKY_PASS == 0
// Integrates extinction only; the mapping allocates extra samples near the horizon and ground.
[shader("fragment")] float4 fragmentMain(SkyVertex input)
    : SV_Target
{
    float mu = input.uv.x * 2 - 1;
    mu = sign(mu) * mu * mu;
    float3 origin = float3(0, 6360.001 + 100 * input.uv.y * input.uv.y, 0);
    float3 direction = float3(sqrt(saturate(1 - mu * mu)), mu, 0);
    if (sphereDistance(origin, direction, 6360) > 0)
    {
        return 0;
    }
    float end = max(sphereDistance(origin, direction, 6460), 0);
    float3 opticalDepth = 0;
    [loop] for (uint i = 0; i < 64; ++i)
    {
        float3 ray, mie, extinction;
        medium(length(origin + direction * end * (i + .5) / 64) - 6360, ray, mie, extinction);
        opticalDepth += extinction * end / 64;
    }
    return float4(exp(-opticalDepth), 1);
}
#elif SKY_PASS == 1
// Hillaire's isotropic higher-order closure, integrated over a fixed sphere sample set.
[shader("fragment")] float4 fragmentMain(SkyVertex input)
    : SV_Target
{
    float mu = input.uv.x * 2 - 1;
    float3 light = float3(sqrt(saturate(1 - mu * mu)), mu, 0), origin = float3(0, 6360.01 + input.uv.y * 99.98, 0);
    float3 radiance = 0, fraction = 0;
    [loop] for (uint i = 0; i < 16; ++i)
    {
        float y = 1 - 2 * (i + .5) / 16, angle = i * 2.39996323;
        float3 d = float3(sqrt(1 - y * y) * cos(angle), y, sqrt(1 - y * y) * sin(angle)), t, f;
        radiance += integrateAtmosphere(origin, d, light, 20000, true, t, f) / 16;
        fraction += f / 16;
    }
    return float4(radiance / max(1 - fraction, float3(.01)), 1);
}
#elif SKY_PASS == 2
// Captures clear-air sky radiance for both celestial sources without their direct disks.
[shader("fragment")] float4 fragmentMain(SkyVertex input)
    : SV_Target
{
    float3 origin = float3(0, 6360 + max(sky.camera.y * .001, .001), 0), d = skyDirection(input.uv), t, f;
    float3 color = integrateAtmosphere(origin, d, sky.sun.xyz, 20000, false, t, f) * sky.sun.w;
    color += integrateAtmosphere(origin, d, sky.moon.xyz, 20000, false, t, f) * sky.moon.w;
    return float4(min(color, float3(65504)), 1);
}
#endif
