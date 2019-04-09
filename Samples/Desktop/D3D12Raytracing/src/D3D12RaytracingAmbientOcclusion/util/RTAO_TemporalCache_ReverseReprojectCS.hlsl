//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#define HLSL
#include "..\RaytracingHlslCompat.h"
#include "..\RaytracingShaderHelper.hlsli"

// ToDO pack value and depth beforehand?
Texture2D<float> g_texInputCachedValue : register(t0);
Texture2D<float> g_texInputCurrentFrameValue : register(t1);
Texture2D<float> g_texInputCachedDepth : register(t2);
Texture2D<float> g_texInputCurrentFrameDepth : register(t3);

RWTexture2D<float> g_texOutputCachedValue : register(u0);
ConstantBuffer<RTAO_TemporalCache_ReverseReprojectConstantBuffer> cb : register(b0);

SamplerState LinearSampler : register(s0);

#if 0
// Retrieves pixel's position in world space.
// linearDepth - linear depth in [0, 1] range   // ToDo
float3 CalculateWorldPositionFromLinearDepth(in uint2 DTid, in float linearDepth)
{
    // Convert to non-linear depth.
#if USE_NORMALIZED_Z
    float linearDistance = linearDepth * (cb.zFar - cb.zNear) + cb.zNear;
#else
    float linearDistance = linearDepth;
#endif
    float logDepth = cb.zFar / (cb.zFar - cb.zNear) - cb.zFar * cb.zNear

    // Calculate Normalized Device Coordinates xyz = { [-1,1], [-1,1], [0,-1] }
    float2 xy = DTid + 0.5f;                            // Center in the middle of the pixel.
    float2 screenPos = 2 * xy * cb.invTextureDim - 1;   // Convert to [-1, 1]
    //screenPos.y = -screenPos.y;                         // Invert Y for DirectX-style coordinates.
    float3 ndc = float3(screenPos, logDepth);  

    float4 viewPosition = mul(float4(ndc, 1), cb.invProj);
    //viewPosition /= viewPosition.w; // Perspective division
    float4 worldPosition = mul(viewPosition, cb.invView);
    
    return worldPosition.xyz;
}
#else
// Retrieves pixel's position in world space.
// linearDepth - linear depth in [0, 1] range   // ToDo
float4 CalculateWorldPositionFromLinearDepth(in uint2 DTid, in float linearDepth)
{
    // Convert to non-linear depth.
#if USE_NORMALIZED_Z
    ToDo
    float linearDistance = linearDepth * (cb.zFar - cb.zNear) + cb.zNear;
#else
    float linearDistance = linearDepth;
#endif

    // Calculate Normalized Device Coordinates xyz = {[-1,1], [-1,1], [0,-1]}
    float2 xy = DTid + 0.5f;                            // Center in the middle of the pixel.
    float2 screenPos = 2 * xy * cb.invTextureDim - 1;   // Convert to [-1, 1]
    screenPos.y = -screenPos.y;                         // Invert Y for DirectX-style coordinates.
    float logDepth = ViewToLogDepth(linearDepth, cb.zNear, cb.zFar);
    float3 ndc = float3(screenPos, logDepth);

    float A = cb.zFar / (cb.zFar - cb.zNear);
    float B = -cb.zNear * cb.zFar / (cb.zFar - cb.zNear);
    float w = B / (logDepth - A);

    float4 projPos = float4(ndc, 1) * w; // Reverse perspective division.
#if 0 
    float4 viewPos = mul(projPos, cb.invProj);
    float4 worldPos = mul(viewPos, cb.invView);

    return worldPos.xyz;
#else
    return projPos;
#endif
}
#endif


[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    // ToDo skip missed ray pixels?
#if 1 // ToDo remove

    float linearDepth = g_texInputCurrentFrameDepth[DTid];
#if 0
    // Calculate Normalized Device Coordinates.
    float2 xy = DTid + 0.5f; // center in the middle of the pixel.
    float2 screenPos = 2 * xy * cb.invTextureDim - 1;    // ToDo test what impact passing inv tex dim makes
    screenPos.y = -screenPos.y;     // Invert Y for DirectX-style coordinates.
    float3 ndc = float3(screenPos, depth);  // 

    // Calculate Clip Space coordinates
    float w = depth * (cb.zFar - cb.zNear) + cb.zNear; // Denormalize depth.
    float3 clipCoord = ndc * w;    // Reverse perspective divide.

    // Reverse project into previous frame
    float4 cacheScreenCoord = mul(float4(clipCoord, w), cb.reverseProjectionTransform);
#else
    float4 worldPos = CalculateWorldPositionFromLinearDepth(DTid, linearDepth);

    // Reverse project into previous frame
    float4 cacheScreenCoord = mul(worldPos, cb.reverseProjectionTransform);
#endif
    cacheScreenCoord.xyz /= cacheScreenCoord.w; // Perspective division.
    cacheScreenCoord.y = -cacheScreenCoord.y;                         // Invert Y for DirectX-style coordinates.
    float cacheLinearDepth = LogToViewDepth(cacheScreenCoord.z, cb.zNear, cb.zFar);
    float2 cacheFrameTexturePos = (cacheScreenCoord.xy + 1) * 0.5f;

    float logDepth = ViewToLogDepth(linearDepth, cb.zNear, cb.zFar);
#else
    uint2 cachedFrameDTid = DTid;
#endif

    // ToDo should some tests be inclusive?
    bool isNotOutOfBounds = cacheFrameTexturePos.x > 0 && cacheFrameTexturePos.y > 0 && cacheFrameTexturePos.x < 1 && cacheFrameTexturePos.y < 1;
    bool isWithinDepthTolerance = abs(cacheLinearDepth - linearDepth) / linearDepth < 0.1;
    
    bool isCacheValueValid = isNotOutOfBounds && isWithinDepthTolerance;

    float value = g_texInputCurrentFrameValue[DTid];
    float mergedValue;
    
    if (isCacheValueValid)
    {
        float cachedValue = g_texInputCachedValue.SampleLevel(LinearSampler, cacheFrameTexturePos, 0);
        float a = max(cb.invCacheFrameAge, cb.minSmoothingFactor);
        mergedValue = lerp(cachedValue, value, a);
    }
    else
    {
        mergedValue = value;
    }
    
    g_texOutputCachedValue[DTid] = mergedValue;
    //g_texOutputCachedValue[DTid] = 0.1* abs(float2(DTid) - float2(cacheFrameDTid)).x;
}