// Mesh transforms, independent normal-map frames and surface texture channels for the portable PBR family.
struct DrawParameters
{
    column_major float4x4 clipFromLocal;
    column_major float4x4 worldFromLocal;
    column_major float4x4 normalFromLocal;
    float orientation;
};
ConstantBuffer<DrawParameters> draw;
// WGSL uses numeric semantic indices as locations; these match the dedicated PBR RHI layout.
struct VertexInput
{
    float3 position:POSITION;
    float3 normal:NORMAL1;
    float2 uv:TEXCOORD2;
    float4 tangent:TANGENT3;
    float2 uv1:TEXCOORD4;
    float4 color:COLOR5;
};
struct VertexOutput
{
    float4 position:SV_Position;
    float3 world:TEXCOORD0;
    float3 normal:TEXCOORD1;
    float4 tangent:TEXCOORD2;
    float2 uv:TEXCOORD3;
    float2 uv1:TEXCOORD4;
    float4 color:COLOR;
};
// Uses inverse transpose normals, forward tangents and determinant-corrected tangent handedness.
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position=mul(draw.clipFromLocal,float4(input.position,1));
    output.world=mul(draw.worldFromLocal,float4(input.position,1)).xyz;
    output.normal=mul(draw.normalFromLocal,float4(input.normal,0)).xyz;
    output.tangent=float4(mul(draw.worldFromLocal,float4(input.tangent.xyz,0)).xyz,input.tangent.w*draw.orientation);
    output.uv=input.uv;output.uv1=input.uv1;output.color=input.color;
    return output;
}
// Reconstructs a tangent frame for this texture's transformed UVs, including mirrored UVs and UV1.
float3 mappedNormal(float3 texel,float scale,float3 n,float3 world,float2 uv)
{
    float3 qx=ddx(world),qy=ddy(world);
    float2 dx=ddx(uv),dy=ddy(uv);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    if (abs(determinant)<1e-10) return n;
    float3 t=safeNormal((qx*dy.y-qy*dx.y)/determinant,float3(1,0,0));
    t=safeNormal(t-n*dot(n,t),float3(1,0,0));
    float3 rawB=(qy*dx.x-qx*dy.x)/determinant;
    float signB=dot(cross(n,t),rawB)<0 ? -1 : 1;
    float3 b=cross(n,t)*signB;
    float3 tn=texel*2-1;tn.xy*=scale;
    return safeNormal(t*tn.x+b*tn.y+n*tn.z,n);
}
// Fully initializes factors before multiplying the glTF-defined texture channels.
Surface evaluateSurface(VertexOutput input,bool front)
{
    Surface s;
    // Derivatives are evaluated before any attribute-dependent branch, as required by WGSL uniformity.
    float3 qx=ddx(input.world),qy=ddy(input.world);
    float2 dx=ddx(input.uv),dy=ddy(input.uv);
    float4 base=material.baseColor*input.color;
#ifdef SLOT0
    base*=slot0Texture.Sample(slot0Sampler,uv0(input.uv,input.uv1));
#endif
    s.base=base.rgb;s.alpha=base.a;
    s.roughness=material.roughness;s.metallic=material.metallic;
#ifdef SLOT1
    float4 mr=slot1Texture.Sample(slot1Sampler,uv1(input.uv,input.uv1));
    s.roughness*=mr.g;s.metallic*=mr.b;
#endif
    float3 geometric=safeNormal(cross(qx,qy),float3(0,0,-1));
    s.normal=dot(input.normal,input.normal)>1e-12 ? normalize(input.normal)*(front?1:-1) : geometric;
    s.coatNormal=s.normal;
#ifdef SLOT2
    s.normal=mappedNormal(slot2Texture.Sample(slot2Sampler,uv2(input.uv,input.uv1)).rgb,
        material.normalScale,s.normal,input.world,uv2(input.uv,input.uv1));
#endif
#ifdef SLOT9
    s.coatNormal=mappedNormal(slot9Texture.Sample(slot9Sampler,uv9(input.uv,input.uv1)).rgb,
        material.clearcoatNormalScale,s.coatNormal,input.world,uv9(input.uv,input.uv1));
#endif
    s.tangent=safeNormal(input.tangent.xyz-s.normal*dot(s.normal,input.tangent.xyz),
        safeNormal(cross(float3(0,1,0),s.normal),float3(1,0,0)));
    float handedness=input.tangent.w;
    if (abs(handedness)<0.5)
    {
        float determinant=dx.x*dy.y-dx.y*dy.x;
        if (abs(determinant)>1e-10)
        {
            float3 t=(qx*dy.y-qy*dx.y)/determinant;
            float3 b=(qy*dx.x-qx*dy.x)/determinant;
            s.tangent=safeNormal(t-s.normal*dot(s.normal,t),s.tangent);
            handedness=dot(cross(s.normal,s.tangent),b)<0 ? -1 : 1;
        }
        else handedness=1;
    }
    s.bitangent=cross(s.normal,s.tangent)*handedness;
    s.ao=1;
#ifdef SLOT3
    s.ao=lerp(1,slot3Texture.Sample(slot3Sampler,uv3(input.uv,input.uv1)).r,material.occlusionStrength);
#endif
    s.emission=material.emissive*material.emissiveStrength;
#ifdef SLOT4
    s.emission*=slot4Texture.Sample(slot4Sampler,uv4(input.uv,input.uv1)).rgb;
#endif
    s.specular=material.specular;
    float3 specColor=material.specularColor;
#ifdef SLOT5
    s.specular*=slot5Texture.Sample(slot5Sampler,uv5(input.uv,input.uv1)).a;
#endif
#ifdef SLOT6
    specColor*=slot6Texture.Sample(slot6Sampler,uv6(input.uv,input.uv1)).rgb;
#endif
    s.f0=min(sq((material.ior-1)/(material.ior+1))*specColor,float3(1));
    s.coat=material.clearcoat;s.coatRoughness=material.clearcoatRoughness;
#ifdef SLOT7
    s.coat*=slot7Texture.Sample(slot7Sampler,uv7(input.uv,input.uv1)).r;
#endif
#ifdef SLOT8
    s.coatRoughness*=slot8Texture.Sample(slot8Sampler,uv8(input.uv,input.uv1)).g;
#endif
    s.sheen=material.sheenColor;s.sheenRoughness=material.sheenRoughness;
#ifdef SLOT10
    s.sheen*=slot10Texture.Sample(slot10Sampler,uv10(input.uv,input.uv1)).rgb;
#endif
#ifdef SLOT11
    s.sheenRoughness*=slot11Texture.Sample(slot11Sampler,uv11(input.uv,input.uv1)).a;
#endif
    s.iridescence=material.iridescence;s.filmIor=material.iridescenceIor;s.thickness=material.thicknessMaximum;
#ifdef SLOT12
    s.iridescence*=slot12Texture.Sample(slot12Sampler,uv12(input.uv,input.uv1)).r;
#endif
#ifdef SLOT13
    s.thickness=lerp(material.thicknessMinimum,material.thicknessMaximum,
        slot13Texture.Sample(slot13Sampler,uv13(input.uv,input.uv1)).g);
#endif
    s.anisotropy=material.anisotropy;
    float2 direction=float2(1,0);
#ifdef SLOT14
    float3 aniso=slot14Texture.Sample(slot14Sampler,uv14(input.uv,input.uv1)).rgb;
    s.anisotropy*=aniso.b;
    float2 encoded=aniso.rg*2-1;
    direction=dot(encoded,encoded)>1e-8?normalize(encoded):float2(1,0);
#endif
    float c=cos(material.anisotropyRotation),sn=sin(material.anisotropyRotation);
    direction=float2(c*direction.x-sn*direction.y,sn*direction.x+c*direction.y);
    float3 t=s.tangent*direction.x+s.bitangent*direction.y;
    s.bitangent=s.bitangent*direction.x-s.tangent*direction.y;s.tangent=t;
    // Authored zero remains visible in debug; evaluation is regularized to avoid a delta distribution on finite pixels.
    s.roughness=clamp(s.roughness,0.045,1);s.coatRoughness=clamp(s.coatRoughness,0.045,1);
    s.sheenRoughness=clamp(s.sheenRoughness,0.045,1);
    return s;
}
// Outputs HDR for lit opaque surfaces, or display-linear color for the sorted composition pass.
[shader("fragment")]
float4 fragmentMain(VertexOutput input,bool front:SV_IsFrontFace):SV_Target
{
    Surface s=evaluateSurface(input,front);
    float3 color;
#ifdef UNLIT
    color=s.base;
#else
    color=shadeSurface(s,input.world);
    if (frame.displayOutput!=0 && frame.debugView==0)
        color=frame.linearOutput!=0 ? color*frame.exposure : pbrNeutral(color*frame.exposure);
#endif
    if (frame.debugView==1) color=s.normal*.5+.5;
    if (frame.debugView==2) color=float3(s.roughness);
    if (frame.debugView==3) color=float3(s.metallic);
    if (frame.debugView==4) color=s.base;
    // Late discard preserves derivative validity across mask boundaries.
    if (frame.alphaMode==1 && s.alpha<material.alphaCutoff) discard;
    if (frame.displayOutput!=0 && frame.encodeSrgb!=0) color=linearToSrgb(color);
    return float4(color,frame.alphaMode==2?s.alpha:1);
}
