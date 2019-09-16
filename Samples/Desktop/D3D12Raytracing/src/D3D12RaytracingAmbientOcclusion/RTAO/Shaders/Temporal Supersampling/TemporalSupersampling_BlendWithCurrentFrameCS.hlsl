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
#include "RaytracingHlslCompat.h"
#include "RaytracingShaderHelper.hlsli"
#include "RTAO\Shaders\RTAO.hlsli"

// ToDo standardize in vs input, out vs output
Texture2D<float> g_texInputCurrentFrameValue : register(t0);
Texture2D<float2> g_texInputCurrentFrameLocalMeanVariance : register(t1);
Texture2D<float> g_texInputCurrentFrameRayHitDistance : register(t2);
Texture2D<uint4> g_texInputReprojected_FrameAge_Value_SquaredMeanValue_RayHitDistance : register(t3);

RWTexture2D<float> g_texInputOutputValue : register(u0);
RWTexture2D<uint2> g_texInputOutputFrameAge : register(u1);
RWTexture2D<float> g_texInputOutputSquaredMeanValue : register(u2);
RWTexture2D<float> g_texInputOutputRayHitDistance : register(u3);
RWTexture2D<float> g_texOutputVariance : register(u4);
RWTexture2D<float> g_texOutputBlurStrength: register(u5);

RWTexture2D<float4> g_texOutputDebug1 : register(u10);
RWTexture2D<float4> g_texOutputDebug2 : register(u11);

ConstantBuffer<TemporalSupersampling_BlendWithCurrentFrameConstantBuffer> cb : register(b0);

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    uint4 encodedCachedValues = g_texInputReprojected_FrameAge_Value_SquaredMeanValue_RayHitDistance[DTid];
    uint packedFrameAgeRaysToGenerate = encodedCachedValues.x;
    uint frameAge;
    uint numRaysToGenerateOrDenoisePasses;
    Unpack_R16_to_R8G8_UINT(packedFrameAgeRaysToGenerate, frameAge, numRaysToGenerateOrDenoisePasses);

    // ToDo remove
    bool isRayCountValue = !(numRaysToGenerateOrDenoisePasses & 0x80);
    uint numRaysToGenerate = isRayCountValue ? numRaysToGenerateOrDenoisePasses : 0;
    uint numDenoisePasses = 0x7F & numRaysToGenerateOrDenoisePasses;

    float4 cachedValues = float4(frameAge, f16tof32(encodedCachedValues.yzw));

    bool isCurrentFrameRayActive = true;
    if (cb.doCheckerboardSampling)
    {
        bool isEvenPixel = ((DTid.x + DTid.y) & 1) == 0;
        isCurrentFrameRayActive = cb.areEvenPixelsActive == isEvenPixel;
    }

    float value = isCurrentFrameRayActive ? g_texInputCurrentFrameValue[DTid] : RTAO::InvalidAOValue;
    bool isValidValue = value != RTAO::InvalidAOValue;
    float valueSquaredMean = isValidValue ? value * value : RTAO::InvalidAOValue;
    float rayHitDistance = RTAO::InvalidAOValue;
    float variance = RTAO::InvalidAOValue;
    
    if (frameAge > 0)
    {     
        uint maxFrameAge = 1 / cb.minSmoothingFactor;
        frameAge = isValidValue ? min(frameAge + 1, maxFrameAge) : frameAge;

        float cachedValue = cachedValues.y;

        float2 localMeanVariance = g_texInputCurrentFrameLocalMeanVariance[DTid];
        float localMean = localMeanVariance.x;
        float localVariance = localMeanVariance.y;
        if (cb.clampCachedValues)
        {
            float localStdDev = max(cb.stdDevGamma * sqrt(localVariance), cb.minStdDevTolerance);
            float nonClampedCachedValue = cachedValue;

            // Clamp value to mean +/- std.dev of local neighborhood to surpress ghosting on value changing due to other occluder movements.
            // Ref: Salvi2016, Temporal Super-Sampling
            cachedValue = clamp(cachedValue, localMean - localStdDev, localMean + localStdDev);

            // Scale down the frame age based on how strongly the cached value got clamped to give more weight to new samples.
            float frameAgeScale = saturate(cb.clampDifferenceToFrameAgeScale * abs(cachedValue - nonClampedCachedValue));
            frameAge = lerp(frameAge, 0, frameAgeScale);
        }
        float invFrameAge = 1.f / frameAge;
        float a = cb.forceUseMinSmoothingFactor ? cb.minSmoothingFactor : max(invFrameAge, cb.minSmoothingFactor);
        float MaxSmoothingFactor = 1;
        a = min(a, MaxSmoothingFactor);

        // Suggestion/ToDo: use average weighting instead of exponential for the first few samples 
        // to even out the weights for the noisy start instead of weighting first samples much more.
        // Ref: Koskela2019, Blockwise Multi-Order Feature Regression for Real-Time Path-Tracing Reconstruction

        // Value.
        value = isValidValue ? lerp(cachedValue, value, a) : cachedValue;

        // Value Squared Mean.
        float cachedSquaredMeanValue = cachedValues.z; 
        valueSquaredMean = isValidValue ? lerp(cachedSquaredMeanValue, valueSquaredMean, a) : cachedSquaredMeanValue;

        // Variance.
        float temporalVariance = valueSquaredMean - value * value;
        temporalVariance = max(0, temporalVariance);    // Ensure variance doesn't go negative due to imprecision.
        variance = frameAge >= cb.minFrameAgeToUseTemporalVariance ? temporalVariance : localVariance;
        variance = max(0.1, variance);

        // RayHitDistance.
        rayHitDistance = isValidValue ? g_texInputCurrentFrameRayHitDistance[DTid] : 0; // ToDO use a common const.
        float cachedRayHitDistance = cachedValues.w;
        rayHitDistance = isValidValue ? lerp(cachedRayHitDistance, rayHitDistance, a) : cachedRayHitDistance;

#if RTAO_MARK_CACHED_VALUES_NEGATIVE
        value = isValidValue ? value : -value;
#endif
    }
    else if (isValidValue)
    {
        frameAge = 1;
        value = value;

        rayHitDistance = g_texInputCurrentFrameRayHitDistance[DTid];
        variance = g_texInputCurrentFrameLocalMeanVariance[DTid].y;
        valueSquaredMean = valueSquaredMean;
    }
    numRaysToGenerateOrDenoisePasses = 33;  // ToDo remove

    float frameAgeRatio = min(frameAge, cb.blurStrength_MaxFrameAge) / float(cb.blurStrength_MaxFrameAge);
    float blurStrength = pow(1 - frameAgeRatio, cb.blurDecayStrength);

    g_texInputOutputFrameAge[DTid] = uint2(frameAge, numRaysToGenerateOrDenoisePasses);
    g_texInputOutputValue[DTid] = value;
    g_texInputOutputSquaredMeanValue[DTid] = valueSquaredMean;
    g_texInputOutputRayHitDistance[DTid] = rayHitDistance;
    g_texOutputVariance[DTid] = variance; 
    g_texOutputBlurStrength[DTid] = blurStrength;
}