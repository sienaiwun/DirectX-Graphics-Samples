#include "ModelViewerRS.hlsli"
Texture2D<float3> ColorTex : register(t32);
Texture2D<float2> NormalTex : register(t33);
Texture2D<float3> MaterialTex:register(t34);


float3 DecodeNormal_CryEngine(float2 G)
{
	//¡°A bit more Deferred¡± - CryEngine 3
	float z = dot(G.xy, G.xy) * 2.0f - 1.0f;
	float2 xy = normalize(G.xy)*sqrt(1 - z * z);
	return float3(xy, z);
}

[RootSignature(ModelViewer_RootSig)]
float3 main(float4 position : SV_Position) : SV_Target0
{
	uint2 pixelPos = position.xy;
	float3 colorSum = 0;
	float3 diffuseAlbedo = ColorTex[pixelPos];
	{
	 // float ao = texSSAO[pixelPos];
	//  colorSum += ApplyAmbientLight(diffuseAlbedo, ao, AmbientColor);
	}
	float gloss = 128.0;
	float3 normal = DecodeNormal_CryEngine(NormalTex[pixelPos]);
	float3 specularAlbedo = float3(0.56, 0.56, 0.56);
	float2 test = NormalTex[(int2)position.xy];
	return normal;
	//float3 LinearRGB = RemoveDisplayProfile(ColorTex[(int2)position.xy], LDR_COLOR_FORMAT);
	//return ApplyDisplayProfile(LinearRGB, DISPLAY_PLANE_FORMAT);
}