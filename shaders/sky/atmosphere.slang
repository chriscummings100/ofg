// Earth atmosphere in kilometres. Segment integration and multiple-scattering closure follow Hillaire 2020.
// Adapted equations: Epic Games MIT reference 183ead5bdacc701b3b626347a680a2f3cd3d4fbd. See NOTICE.md.
static const float SKY_PI = 3.14159265359;
struct SkyParameters
{
    float4 sun;
    float4 moon;
    float4 sunTransmission;
    float4 moonTransmission;
    float4 atmosphere;
    float4 camera;
    float4 clouds;
    float4 windLayers;
    float4 viewport;
    float4 controls;
    column_major float4x4 worldFromView;
};
ConstantBuffer<SkyParameters> sky;
struct SkyVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
// Generates a fullscreen triangle with top-left UV origin.
[shader("vertex")] SkyVertex vertexMain(uint id : SV_VertexID)
{
    SkyVertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
// Returns distance to the nearest positive sphere intersection, or -1.
float sphereDistance(float3 p, float3 d, float radius)
{
    float b = dot(p, d), c = dot(p, p) - radius * radius, disc = b * b - c;
    if (disc < 0)
    {
        return -1;
    }
    float q = sqrt(disc);
    float near = -b - q;
    return near >= 0 ? near : (-b + q >= 0 ? -b + q : -1);
}
// Returns Rayleigh/Mie scattering and total extinction at altitude in kilometres.
void medium(float height, out float3 ray, out float3 mie, out float3 extinction)
{
    height = max(height, 0);
    ray = float3(.005802, .013558, .0331) * exp(-height / 8) * sky.atmosphere.x;
    mie = float3(.003996) * exp(-height / 1.2) * sky.atmosphere.y;
    extinction = ray + float3(.00444) * exp(-height / 1.2) * sky.atmosphere.y +
                 float3(.000650, .001881, .000085) * saturate(1 - abs(height - 25) / 15) * sky.atmosphere.z;
}
// Maps altitude and zenith cosine into a horizon-dense transmittance lookup.
float2 transmissionUv(float height, float mu)
{
    return float2(sign(mu) * sqrt(abs(mu)) * .5 + .5, sqrt(saturate(height / 100)));
}
#if SKY_PASS >= 1 && SKY_PASS <= 3
Texture2D<float4> transmittanceLut;
SamplerState skySampler;
// Samples unit-illuminance transmission, with the planet's geometric shadow explicit.
float3 transmission(float3 p, float3 direction)
{
    if (sphereDistance(p, direction, 6360) > 0)
    {
        return 0;
    }
    return transmittanceLut.SampleLevel(skySampler, transmissionUv(length(p) - 6360, dot(normalize(p), direction)), 0)
        .rgb;
}
#endif
#if SKY_PASS >= 2 && SKY_PASS <= 3
Texture2D<float4> multipleLut;
#endif
#if SKY_PASS >= 1 && SKY_PASS <= 3
// Integrates one source; recurrence also returns the isotropic scattering fraction for the closure LUT.
float3 integrateAtmosphere(
    float3 origin,
    float3 direction,
    float3 light,
    float maxDistance,
    bool isotropic,
    out float3 throughput,
    out float3 scatteredFraction
)
{
    float end = sphereDistance(origin, direction, 6460);
    float ground = sphereDistance(origin, direction, 6360);
    if (ground > 0)
    {
        end = min(end, ground);
    }
    end = clamp(end, 0, maxDistance);
    throughput = 1;
    scatteredFraction = 0;
    float3 radiance = 0;
    float cosine = dot(direction, light);
    float rayPhase = 3 * (1 + cosine * cosine) / (16 * SKY_PI);
    float g = .8;
    float miePhase = (1 - g * g) / (4 * SKY_PI * pow(max(1 + g * g - 2 * g * cosine, .001), 1.5));
    [loop] for (uint i = 0; i < 32; ++i)
    {
        float t0 = end * pow(float(i) / 32, 2), t1 = end * pow(float(i + 1) / 32, 2), dt = t1 - t0;
        float3 p = origin + direction * (t0 + t1) * .5, ray, mie, extinction;
        medium(length(p) - 6360, ray, mie, extinction);
        float3 stepT = exp(-extinction * dt);
        float3 integral = (1 - stepT) / max(extinction, float3(1e-8));
        float3 scattering = ray + mie;
        float3 source =
            transmission(p, light) * (isotropic ? scattering / (4 * SKY_PI) : ray * rayPhase + mie * miePhase);
#if SKY_PASS >= 2
        float2 multiUv = float2(dot(normalize(p), light) * .5 + .5, saturate((length(p) - 6360) / 100));
        source += scattering * multipleLut.SampleLevel(skySampler, multiUv, 0).rgb;
#endif
        radiance += throughput * source * integral;
        scatteredFraction += throughput * scattering * integral;
        throughput *= stepT;
    }
    if (ground > 0 && ground <= maxDistance)
    {
        float3 p = origin + direction * ground;
        radiance += throughput * transmission(p + normalize(p) * .01, light) * max(dot(normalize(p), light), 0) *
                    sky.atmosphere.w / SKY_PI;
    }
    return radiance;
}
#endif
// Converts lat-long lookup coordinates to a world direction, Y up.
float3 skyDirection(float2 uv)
{
    float a = (uv.x - .5) * 2 * SKY_PI, elevation = (.5 - uv.y) * SKY_PI;
    return float3(sin(a) * cos(elevation), sin(elevation), cos(a) * cos(elevation));
}
// Converts a world direction to the corresponding lat-long lookup coordinate.
float2 skyUv(float3 d)
{
    return float2(atan2(d.x, d.z) / (2 * SKY_PI) + .5, .5 - asin(clamp(d.y, -1, 1)) / SKY_PI);
}
// Reconstructs the perspective view ray without a per-pixel matrix inverse.
float3 cameraRay(float2 uv)
{
    float3 d = float3((uv.x * 2 - 1) * sky.viewport.z * sky.controls.x, (1 - uv.y * 2) * sky.controls.x, 1);
    return normalize(mul(sky.worldFromView, float4(d, 0)).xyz);
}
