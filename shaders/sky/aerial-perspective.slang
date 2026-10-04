// View-aligned camera-distance slices, RGB in-scattering and mean RGB opacity in alpha.
#if SKY_PASS == 3
// A quadratic distance distribution retains precision close to the observer.
[shader("fragment")] float4 fragmentMain(SkyVertex input)
    : SV_Target
{
    float distance = pow(sky.viewport.w / 31, 2) * sky.controls.y * .001;
    float3 origin = float3(0, 6360 + max(sky.camera.y * .001, .001), 0), d = cameraRay(input.uv), t, f;
    float3 color = integrateAtmosphere(origin, d, sky.sun.xyz, distance, false, t, f) * sky.sun.w;
    color += integrateAtmosphere(origin, d, sky.moon.xyz, distance, false, t, f) * sky.moon.w;
    return float4(min(color, float3(65504)), 1 - dot(t, float3(1.0 / 3)));
}
#endif
