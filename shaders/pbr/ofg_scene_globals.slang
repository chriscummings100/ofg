// Common scene type imported by every PBR variant; one immutable object is shared by a pass.
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
    uint displayOutput;
    uint encodeSrgb;
    uint linearOutput;
    uint debugView;
};
struct OutdoorParameters
{
    float4 enabledExposure;
    float4 viewPositionNear;
    float4 viewForwardFar;
    float4 splitDistances;
    float4 texelSizes;
    float4 cloudSettings;
    float4 windLayers;
    float4 lightDirection;
    float4 shadowOptions;
    float4 ibl;
    float4 iblExposure; // Old/new generation exposure compensation during incremental updates.
    float4 viewport;    // Width, height, aerial-perspective enabled, reserved.
    column_major float4x4 shadowMatrices[4];
};
struct SceneGlobals
{
    FrameParameters frame;
    OutdoorParameters outdoor;
    TextureCubeArray<float4> filteredEnvironment;
    Texture2D<float4> brdfLookup;
    DepthTexture2DArray shadowMaps;
    SamplerComparisonState shadowSampler;
    Texture2DArray<float4> aerialPerspective;
    SamplerState environmentSampler;
};
