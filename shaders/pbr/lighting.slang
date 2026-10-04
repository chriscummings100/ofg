// Scene lighting ABI, matched environment sampling and Khronos layer evaluation. See NOTICE.md.
struct LightParameters
{
    float4 positionRange;
    float4 directionType;
    float4 colorIntensity;
    float2 coneCosines;
};
struct FrameParameters
{
    float3 cameraPosition;
    uint lightCount;
    LightParameters lights[4];
    float environmentIntensity;
    float environmentRotation;
    float environmentMips;
    float exposure;
    uint alphaMode;
    uint displayOutput;
    uint encodeSrgb;
    uint linearOutput;
    uint debugView;
};
ConstantBuffer<FrameParameters> frame;
TextureCube<float4> diffuseEnvironment;
TextureCube<float4> specularEnvironment;
TextureCube<float4> sheenEnvironment;
Texture2D<float4> brdfLookup;
SamplerState environmentSampler;
// Applies the same right-handed Y rotation to all environment directions (scene itself remains LH).
float3 environmentDirection(float3 d)
{
    float s=sin(frame.environmentRotation), c=cos(frame.environmentRotation);
    return float3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
}
// Returns filtered specular incident radiance at the bake's perceptual-roughness mip coordinate.
float3 radiance(float3 n,float3 v,float roughness)
{
    return specularEnvironment.SampleLevel(environmentSampler,environmentDirection(reflect(-v,n)),
        roughness*(frame.environmentMips-1)).rgb*frame.environmentIntensity;
}
// Khronos/Fdez-Aguera split sum with multiscattering energy compensation; LUT and runtime share GGX visibility.
float3 iblFresnel(float nv,float roughness,float3 f0,float weight)
{
    float2 ab=brdfLookup.SampleLevel(environmentSampler,float2(nv,roughness),0).rg;
    float3 ks=f0+(max(float3(1-roughness),f0)-f0)*pow(1-nv,5);
    float3 single=weight*(ks*ab.x+ab.y);
    float missing=1-(ab.x+ab.y);
    float3 average=weight*(f0+(1-f0)/21);
    return single+missing*single*average/max(1-average*missing,float3(1e-5));
}
// Reads the offline integral of the runtime sheen lobe for energy-preserving layer scaling.
float sheenEnergy(float nd,float roughness)
{
    return brdfLookup.SampleLevel(environmentSampler,float2(nd,roughness),0).a;
}
struct Surface
{
    float3 base, emission, normal, tangent, bitangent, coatNormal;
    float roughness, metallic, alpha, ao, specular;
    float3 f0, sheen;
    float coat, coatRoughness, sheenRoughness, iridescence, filmIor, thickness, anisotropy;
};
// Combines diffuse/metal dielectric Fresnel responses with optional thin-film interference.
float3 baseLayer(Surface s,float3 diffuse,float3 specular,float3 fd,float3 fm,float3 filmD,float3 filmM)
{
    float3 dielectric=lerp(diffuse,specular,fd);
    float3 metal=specular*fm;
    dielectric=lerp(dielectric,lerp(diffuse,specular,filmD),s.iridescence);
    metal=lerp(metal,specular*filmM,s.iridescence);
    return lerp(dielectric,metal,s.metallic);
}
// Evaluates environment and up to four punctual lights in one explicit world-space frame.
float3 shadeSurface(Surface s,float3 position)
{
    float3 n=s.normal, v=safeNormal(frame.cameraPosition-position,n);
    float nv=max(saturate(dot(n,v)),1e-4);
    float3 filmD=0,filmM=0;
    if (s.iridescence>0 && s.thickness>0)
    {
        filmD=evalIridescence(1,s.filmIor,nv,s.thickness,s.f0);
        filmM=evalIridescence(1,s.filmIor,nv,s.thickness,s.base);
    }
    else s.iridescence=0;
    float coatF=F_Schlick(0.04,1.0,max(saturate(dot(s.coatNormal,v)),1e-4));
    float3 bent=n;
    if (s.anisotropy>0)
    {
        float3 an=safeNormal(cross(cross(s.bitangent,v),s.bitangent),n);
        float bend=1-s.anisotropy*(1-s.roughness);
        bent=safeNormal(lerp(an,n,bend*bend*bend*bend),n);
    }
    float3 diffuse=diffuseEnvironment.SampleLevel(environmentSampler,environmentDirection(n),0).rgb*
        frame.environmentIntensity*s.base;
    float3 spec=radiance(bent,v,s.roughness);
    float3 color=baseLayer(s,diffuse,spec,iblFresnel(nv,s.roughness,s.f0,s.specular),
        iblFresnel(nv,s.roughness,s.base,1),filmD,filmM);
    float sheenScale=1-max3(s.sheen)*sheenEnergy(nv,s.sheenRoughness);
    float3 sheenLight=sheenEnvironment.SampleLevel(environmentSampler,environmentDirection(reflect(-v,n)),
        s.sheenRoughness*(frame.environmentMips-1)).rgb*frame.environmentIntensity;
    float sheenBrdf=brdfLookup.SampleLevel(environmentSampler,float2(nv,s.sheenRoughness),0).b;
    color=color*sheenScale+sheenLight*s.sheen*sheenBrdf;
    color=lerp(color,radiance(s.coatNormal,v,s.coatRoughness),s.coat*coatF)*s.ao;
    for (uint i=0;i<frame.lightCount;++i)
    {
        LightParameters light=frame.lights[i];
        uint type=uint(light.directionType.w);
        float3 delta=type==0 ? -light.directionType.xyz : light.positionRange.xyz-position;
        float distanceSquared=max(dot(delta,delta),1e-6);
        float3 l=safeNormal(delta,n), h=safeNormal(l+v,n);
        float attenuation=type==0 ? 1 : 1/distanceSquared;
        if (type!=0 && light.positionRange.w>0)
            attenuation*=saturate(1-sq(distanceSquared/sq(light.positionRange.w)));
        if (type==2)
        {
            float cone=dot(-l,safeNormal(light.directionType.xyz,float3(0,0,1)));
            float spot=saturate((cone-light.coneCosines.y)/max(light.coneCosines.x-light.coneCosines.y,1e-6));
            attenuation*=spot*spot;
        }
        float3 intensity=light.colorIntensity.rgb*light.colorIntensity.w*attenuation;
        float nl=saturate(dot(n,l)), nh=saturate(dot(n,h)), vh=saturate(dot(v,h));
        float3 diffuseDirect=s.base/M_PI*nl;
        float alpha=s.roughness*s.roughness;
        float3 specDirect=nl*(s.anisotropy>0 ? BRDF_specularGGXAnisotropy(alpha,s.anisotropy,n,v,l,h,s.tangent,s.bitangent)
            : BRDF_specularGGX(alpha,nl,nv,nh));
        float3 direct=baseLayer(s,diffuseDirect,specDirect,F_Schlick(s.f0*s.specular,float3(s.specular),vh),
            F_Schlick(s.base,float3(1),vh),filmD,filmM);
        float scale=min(sheenScale,1-max3(s.sheen)*sheenEnergy(nl,s.sheenRoughness));
        direct=direct*scale+nl*BRDF_specularSheen(s.sheen,s.sheenRoughness,max(nl,1e-4),nv,nh);
        float cnl=saturate(dot(s.coatNormal,l)), cnv=max(saturate(dot(s.coatNormal,v)),1e-4);
        float cnh=saturate(dot(s.coatNormal,h));
        float3 coat=cnl*BRDF_specularGGX(s.coatRoughness*s.coatRoughness,cnl,cnv,cnh);
        color+=lerp(direct,coat,s.coat*coatF)*intensity;
    }
    return color+s.emission*(1-s.coat*coatF);
}
