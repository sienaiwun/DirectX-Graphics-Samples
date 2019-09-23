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

// Atrous Wavelet Transform Cross Bilateral Filter
// Ref: Dammertz 2010, Edge-Avoiding A-Trous Wavelet Transform for Fast Global Illumination Filtering
// Ref: Schied 2007, Spatiotemporal Variance-Guided Filtering

#define HLSL
#include "RaytracingHlslCompat.h"
#include "RaytracingShaderHelper.hlsli"
#include "Kernels.hlsli"
#include "RTAO/Shaders/RTAO.hlsli"


Texture2D<float> g_inValue : register(t0);

Texture2D<NormalDepthTexFormat> g_inNormalDepth : register(t1);
Texture2D<float> g_inVariance : register(t4); 
Texture2D<float> g_inSmoothedVariance : register(t5); 
Texture2D<float> g_inHitDistance : register(t6);
Texture2D<float2> g_inPartialDistanceDerivatives : register(t7);
Texture2D<uint> g_inTspp : register(t8);

RWTexture2D<float> g_outFilteredValue : register(u0);
RWTexture2D<float4> g_outDebug1 : register(u3);
RWTexture2D<float4> g_outDebug2 : register(u4);

ConstantBuffer<AtrousWaveletTransformFilterConstantBuffer> cb: register(b0);

float DepthThreshold(float depth, float2 ddxy, float2 pixelOffset)
{
    float depthThreshold;

    if (cb.perspectiveCorrectDepthInterpolation)
    {
        float2 newDdxy = RemapDdxy(depth, ddxy, pixelOffset);
        depthThreshold = dot(1, abs(newDdxy));
    }
    else
    {
        depthThreshold = dot(1, abs(pixelOffset * ddxy));
    }

    return depthThreshold;
}

void AddFilterContribution(
    inout float weightedValueSum, 
    inout float weightSum, 
    in float value, 
    in float stdDeviation,
    in float depth, 
    in float3 normal, 
    in float2 ddxy,
    in uint row, 
    in uint col,
    in uint2 kernelStep,
    in uint2 DTid)
{
    const float valueSigma = cb.valueSigma;
    const float normalSigma = cb.normalSigma;
    const float depthSigma = cb.depthSigma;
 
    int2 pixelOffset;
    float kernelWidth;
    float varianceScale = 1;

    pixelOffset = int2(row - FilterKernel::Radius, col - FilterKernel::Radius) * kernelStep;
    int2 id = int2(DTid) + pixelOffset;

    if (IsWithinBounds(id, cb.textureDim))
    {
        float iDepth;
        float3 iNormal;
        DecodeNormalDepth(g_inNormalDepth[id], iNormal, iDepth);
        float iValue = g_inValue[id];

        bool iIsValidValue = iValue != RTAO::InvalidAOCoefficientValue;
        if (!iIsValidValue || iDepth == 0)
        {
            return;
        }

        // ToDo explain/remove
        float w_s = 1;
#if RTAO_MARK_CACHED_VALUES_NEGATIVE
        if (iValue < 0)
        {
            w_s = cb.staleNeighborWeightScale;
            iValue = -iValue;
        }
#endif


        // Enforce tspp of at least 1 for reprojected values.
        // This is because the denoiser will fill in invalid values with filtered 
        // ones if it can. But it doesn't increase the tspp.
        uint iTspp = g_inTspp[id].x;
        iTspp = max(iTspp, 1);

        // Confidence based weight
        // This helps prevent recently disoccluded pixels with fewer samples
        // contribute as much as samples with higher accumulated samples,
        // and hence lowers the visible firefly/sparkly artifacts on disocclusions on creases an such.
        float w_c = 1;
        w_c = cb.weightByTspp ? iTspp : 1;

        // Value based weight.
            // Ref: Dammertz2010
            // Tighten value range smoothing for higher passes.
        // ToDo m_CB->valueSigma = i > 0 ? valueSigma * powf(2.f, -float(i)) : 1;
        const float errorOffset = 0.005f;
        // Lower value tolerance for the neighbors further apart.
        float valueSigmaDistCoef = 1.0 / length(pixelOffset);
        float e_x = -abs(value - iValue) / (valueSigmaDistCoef * valueSigma * stdDeviation + errorOffset);
        float w_x = exp(e_x);
 
        // Normal based weight.
        float w_n = pow(max(0, dot(normal, iNormal)), normalSigma);

        // Depth based weight.
        float w_d;
        {
            float2 pixelOffsetForDepth = pixelOffset;

            // Account for sample offset in bilateral downsampled partial depth derivative buffer.
            if (cb.usingBilateralDownsampledBuffers)
            {
                float2 offsetSign = sign(pixelOffset);
                pixelOffsetForDepth = pixelOffset + offsetSign * float2(0.5, 0.5);
            }

            // ToDo dedupe with CrossBilateralWeights.hlsli?
            float depthFloatPrecision = FloatPrecision(max(depth, iDepth), cb.DepthNumMantissaBits);
            float depthThreshold = DepthThreshold(depth, ddxy, pixelOffsetForDepth);
            float depthTolerance = depthSigma * depthThreshold + depthFloatPrecision;
            float delta = abs(depth - iDepth);
            delta = max(0, delta - depthFloatPrecision); // Avoid distinguising initial values up to the float precision. Gets rid of banding due to low depth precision format.
            w_d = exp(-delta / depthTolerance);

            // Scale down contributions for samples beyond tolerance, but completely disable contribution for samples too far away.
            w_d *= w_d >= cb.depthWeightCutoff;
        }

        // Filter kernel weight.
        float w_h = FilterKernel::Kernel[row][col];
        
        // Final weight.
        float w = w_c * w_s * w_h * w_n * w_x * w_d;

        weightedValueSum += w * iValue;
        weightSum += w;
    }
}

[numthreads(AtrousWaveletTransformFilterCS::ThreadGroup::Width, AtrousWaveletTransformFilterCS::ThreadGroup::Height, 1)]
void main(uint2 DTid : SV_DispatchThreadID, uint2 Gid : SV_GroupID)
{
    if (!IsWithinBounds(DTid, cb.textureDim))
    {
        return;
    }

    // Initialize values to the current pixel / center filter kernel value.
    float value = g_inValue[DTid];
    float3 normal;
    float depth;
    DecodeNormalDepth(g_inNormalDepth[DTid], normal, depth);
    
    bool isValidValue = value != RTAO::InvalidAOCoefficientValue;
    float filteredValue = isValidValue && value < 0 ? -value : value;
    float variance = g_inSmoothedVariance[DTid];

    if (depth != 0)
    {
        float w_c = 1;
#if RTAO_MARK_CACHED_VALUES_NEGATIVE
        if (isValidValue && value < 0)
        {
            // ToDo clean up, document or remove
            w_c = cb.staleNeighborWeightScale;
            value = -value;
        }
#endif

        float2 ddxy = g_inPartialDistanceDerivatives[DTid];

        float weightSum = 0;
        float weightedValueSum = 0;
        float stdDeviation = 1;

        if (isValidValue)
        {
            float pixelWeight = cb.weightByTspp ? g_inTspp[DTid] : 1;
            float w = FilterKernel::Kernel[FilterKernel::Radius][FilterKernel::Radius];
            weightSum = pixelWeight * w;
            weightedValueSum = weightSum * value;
            stdDeviation = sqrt(variance);
        }

        // Adaptive kernel size
        // Scale the kernel span based on AO ray hit distance. 
        // This helps filter out lower frequency noise, a.k.a. boiling artifacts.
        uint2 kernelStep = 0;
        if (cb.useAdaptiveKernelSize && isValidValue)
        {
            float avgRayHitDistance = g_inHitDistance[DTid];

            float perPixelViewAngle = (FOVY / cb.textureDim.y) * PI / 180.0; 
            float tan_a = tan(perPixelViewAngle);
            float2 projectedSurfaceDim = ApproximateProjectedSurfaceDimensionsPerPixel(depth, ddxy, tan_a);

            // Calculate kernel width as a ratio of hitDistance / projected surface dim per pixel
            // Apply a non-linear factor based on relative rayHitDistance. This is because
            // average ray hit distance grows large fast if the closeby occluders cover only part of the hemisphere.
            // Having a smaller kernel for such cases helps preserve occlusion detail.
            float t = min(avgRayHitDistance / 22.0, 1); // 22 was selected empirically
            float k = cb.rayHitDistanceToKernelWidthScale * pow(t, cb.rayHitDistanceToKernelSizeScaleExponent);
            kernelStep = max(1, round(k * avgRayHitDistance / projectedSurfaceDim));

            uint2 targetKernelStep = clamp(kernelStep, (cb.minKernelWidth - 1) / 2, (cb.maxKernelWidth - 1) / 2);

            // ToDo try lerping radius instead of step
            // ToDo compare to use same step for both X, Y. Varying step causes visible streaking.
            // ToDo: use larger kernel on lower tspp
            // ToDo varying cycle freq dep on kernel radius?
            uint2 adjustedKernelStep = lerp(1, targetKernelStep, cb.kernelRadiusLerfCoef); 

            kernelStep = adjustedKernelStep;
        }

        if (variance >= cb.minVarianceToDenoise)
        {
            // Add contributions from the neighborhood.
            [unroll]
            for (UINT r = 0; r < FilterKernel::Width; r++)
            [unroll]
            for (UINT c = 0; c < FilterKernel::Width; c++)
                if (r != FilterKernel::Radius || c != FilterKernel::Radius)
                    AddFilterContribution(
                        weightedValueSum, 
                        weightSum, 
                        value, 
                        stdDeviation, 
                        depth, 
                        normal, 
                        ddxy, 
                        r, 
                        c, 
                        kernelStep, 
                        DTid);
        }

        float smallValue = 1e-6f;
        if (weightSum > smallValue)
        {
            filteredValue = weightedValueSum / weightSum;
        }
        else
        {
            filteredValue = RTAO::InvalidAOCoefficientValue;
        }
    }

    g_outFilteredValue[DTid] = filteredValue;
}