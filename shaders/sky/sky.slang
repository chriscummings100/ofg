// Visible sky and environment capture share atmosphere/cloud evaluation; only the visible sky adds disks.
#if SKY_PASS >= 4
Texture2D<float4> skyViewLut;
SamplerState skySampler;
// Returns the established D3D/WebGPU cubemap face direction for a top-left UV coordinate.
float3 cubeDirection(uint face, float2 uv)
{
    float2 p = uv * 2 - 1;
    if (face == 0)
    {
        return normalize(float3(1, -p.y, -p.x));
    }
    if (face == 1)
    {
        return normalize(float3(-1, -p.y, p.x));
    }
    if (face == 2)
    {
        return normalize(float3(p.x, 1, p.y));
    }
    if (face == 3)
    {
        return normalize(float3(p.x, -1, -p.y));
    }
    if (face == 4)
    {
        return normalize(float3(p.x, -p.y, 1));
    }
    return normalize(float3(-p.x, -p.y, -1));
}
// Combines clear-air radiance, night stars and the approximate illuminated cloud layers.
float3 background(float3 direction, bool disks)
{
    float3 color = skyViewLut.SampleLevel(skySampler, skyUv(direction), 0).rgb;
    float night = 1 - smoothstep(-.2, .05, sky.sun.y);
    float star = step(.9994, cloudHash(floor(skyUv(direction) * float2(4096, 2048))));
    color += star * .008 * night * max(direction.y, 0);
    if (disks && sky.controls.z > 0)
    {
        float sun = smoothstep(cos(.0048), cos(.0045), dot(direction, sky.sun.xyz));
        float moon = smoothstep(cos(.0048), cos(.0045), dot(direction, sky.moon.xyz));
        // Disk solid angle normalizes illuminance; scene exposure is applied before FP16 storage.
        float horizon = smoothstep(-.0048, .0048, direction.y);
        color += (sun * sky.sun.w * sky.sunTransmission.rgb + moon * sky.moon.w * sky.moonTransmission.rgb) / .000068 *
                 horizon;
    }
    float transmission = cloudTransmission(sky.camera.xyz, direction, sky.clouds, sky.windLayers);
    // Two-dimensional layers use sky fill plus attenuated direct irradiance; no artificial dusk light source.
    float3 ambient = skyViewLut.SampleLevel(skySampler, float2(.5, 0), 0).rgb;
    float3 direct = sky.sun.w * sky.sunTransmission.rgb * max(sky.sun.y, 0) +
                    sky.moon.w * sky.moonTransmission.rgb * max(sky.moon.y, 0);
    float3 cloudColor = ambient * .8 + direct * .05;
    return color * transmission + cloudColor * (1 - transmission);
}
// Captures unexposed disk-free sky or renders the exposed background for the active camera.
[shader("fragment")] float4 fragmentMain(SkyVertex input)
    : SV_Target
{
#if SKY_PASS == 5
    return float4(min(background(cubeDirection(uint(sky.viewport.w), input.uv), false), float3(65504)), 1);
#else
    // Preserve finite FP16 storage even when manual exposure puts the solar disk far above display white.
    return float4(min(background(cameraRay(input.uv), true) * sky.camera.w, float3(65504)), 1);
#endif
}
#endif
