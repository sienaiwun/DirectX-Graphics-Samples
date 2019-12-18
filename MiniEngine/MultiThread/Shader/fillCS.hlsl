RWTexture2D<float4> outputTexture : register(u1);

cbuffer uniformBlock : register(b0)
{
    float3 c_color : packoffset(c0);
}


[numthreads(16, 16, 1)]
void main(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    outputTexture[DTid.xy] = float4(c_color, 1.0);
}