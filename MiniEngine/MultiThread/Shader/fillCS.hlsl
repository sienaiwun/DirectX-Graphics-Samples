#define QUAD_WIDTH 16
#define QUAD_HEIGHT QUAD_WIDTH

struct ElementData
{
    uint2 index;
    float3 color;
};

RWTexture2D<float4> outputTexture : register(u1);

StructuredBuffer<ElementData> g_ElementBuffer : register(t1);


cbuffer uniformBlock : register(b0)
{
    float3 c_color : packoffset(c0);
    float padding : packoffset(c0.w);
    uint2 tile_num :packoffset(c1);
    uint2 padding2 :packoffset(c1.z);
}


[numthreads(QUAD_WIDTH, QUAD_HEIGHT, 1)]
void main(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    int groupIdx = Gid.y * tile_num.x + Gid.x;
    outputTexture[DTid.xy] = float4(0.005* c_color + g_ElementBuffer[groupIdx].color, 1.0);
}