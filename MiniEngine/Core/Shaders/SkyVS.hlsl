#include "icosphere.hlsli"
#include "SkyRS.hlsli"
struct PSInputWorldPosition {
    float4 p            : SV_POSITION;
    float3 p_world      : POSITION0;
};

cbuffer VSConstants : register(b0)
{
    float4x4 modelToProjection;
    float4x4 modelToShadow;
    float3 ViewerPos;
};


[RootSignature(Sky_RootSig)]
PSInputWorldPosition main(uint vertex_id : SV_VertexID) {
    PSInputWorldPosition output;
    output.p_world = g_icosphere[vertex_id]*9999.0f;
    float4 p_proj =   mul(modelToProjection, float4(output.p_world, 1.0));
    p_proj.z = 0.0f;
    output.p = p_proj;;
    return output;
}