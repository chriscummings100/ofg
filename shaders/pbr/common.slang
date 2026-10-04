// Shared numeric guards and output transfer; Khronos Neutral adapted from cc27919 (Apache-2.0).
static const float M_PI = 3.14159265358979323846;
// Squares scalar and RGB values used by the reference's thin-film model.
float sq(float x) { return x*x; }
float3 sq(float3 x) { return x*x; }
// Preserves a known geometric direction for degenerate view/half/tangent vectors.
float3 safeNormal(float3 x, float3 fallback) { float d=dot(x,x); return d>1e-12 ? x*rsqrt(d) : fallback; }
// Returns the largest RGB component for energy scaling.
float max3(float3 x) { return max(x.x,max(x.y,x.z)); }
// Maps scene-linear HDR to display-linear RGB without a transfer encoding.
float3 pbrNeutral(float3 color)
{
    color=max(color,0);
    float x=min(color.x,min(color.y,color.z));
    float offset=x<0.08 ? x-6.25*x*x : 0.04;
    color-=offset;
    float peak=max3(color);
    if (peak<0.76) return color;
    float newPeak=1-0.24*0.24/(peak+0.24-0.76);
    color*=newPeak/peak;
    float g=1-1/(0.15*(peak-newPeak)+1);
    return lerp(color,float3(newPeak),g);
}
// Exact IEC sRGB transfer; alpha is never encoded.
float3 linearToSrgb(float3 c)
{
    return select(c <= 0.0031308, c*12.92, 1.055*pow(max(c,0),float3(1.0/2.4))-0.055);
}
