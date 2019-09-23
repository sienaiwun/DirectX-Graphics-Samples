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

//
// GpuKernels used in RTAO paths (raytracing + denoising)
//

#pragma once

namespace RTAOGpuKernels
{
    class GaussianFilter
    {
    public:
        enum FilterType {
            Filter3x3 = 0,
            Filter3x3RG,
            Count
        };

        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            UINT width,
            UINT height,
            FilterType type,
            ID3D12DescriptorHeap* descriptorHeap,
            D3D12_GPU_DESCRIPTOR_HANDLE inputResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputResourceHandle);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObjects[FilterType::Count];

        ConstantBuffer<TextureDimConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };

    class FillInMissingValuesFilter
    {
    public:
        enum FilterType {
            GaussianFilter7x7 = 0,
            DepthAware_GaussianFilter7x7,
            Count
        };

        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            UINT width,
            UINT height,
            FilterType type,
            UINT filterStep,
            ID3D12DescriptorHeap* descriptorHeap,
            D3D12_GPU_DESCRIPTOR_HANDLE inputResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputDepthResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputResourceHandle);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObjects[FilterType::Count];

        ConstantBuffer<FilterConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };

    class BilateralFilter
    {
    public:
        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            UINT filterStep,
            float normalWeightExponent,
            float minNormalWeightStrength,
            ID3D12DescriptorHeap* descriptorHeap,
            D3D12_GPU_DESCRIPTOR_HANDLE inputResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputDepthResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputBlurStrengthResourceHandle,
            GpuResource* outputResource,
            bool readWriteUAV_and_skipPassthrough = true);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObject;

        ConstantBuffer<BilateralFilterConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };

    class AtrousWaveletTransformCrossBilateralFilter
    {
    public:
        enum Mode {
            OutputFilteredValue,
            OutputPerPixelFilterWeightSum
        };

        enum FilterType {
            EdgeStoppingGaussian3x3 = 0,
            EdgeStoppingGaussian5x5,
            Count
        };

        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            ID3D12DescriptorHeap* descriptorHeap,
            FilterType type,
            D3D12_GPU_DESCRIPTOR_HANDLE inputValuesResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputNormalsResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputVarianceResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputHitDistanceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputPartialDistanceDerivativesResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputTsppResourceHandle,
            GpuResource* outputResource,
            float valueSigma,
            float depthSigma,
            float normalSigma,
            float weightScale,
            UINT passNumberToOutputToIntermediateResource = 1,
            Mode filterMode = OutputFilteredValue,
            bool perspectiveCorrectDepthInterpolation = false,
            bool useAdaptiveKernelSize = false,
            float kernelRadiusLerfCoef = 0.f,
            float rayHitDistanceToKernelWidthScale = 1.f,
            float rayHitDistanceToKernelSizeScaleExponent = 2.f,
            UINT minKernelWidth = 3,
            UINT maxKernelWidth = 101,
            float varianceSigmaScaleOnSmallKernels = 2.f,
            bool usingBilateralDownsampledBuffers = false,
            float minVarianceToDenoise = 0,
            float staleNeighborWeightScale = 1,
            float depthWeightCutoff = 0.5f,
            bool weightByTspp = false);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObjects[FilterType::Count];
        ConstantBuffer<AtrousWaveletTransformFilterConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };

    class FillInCheckerboard
    {
    public:
        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            ID3D12DescriptorHeap* descriptorHeap,
            UINT width,
            UINT height,
            D3D12_GPU_DESCRIPTOR_HANDLE inputOutputResourceHandle,
            bool fillEvenPixels = false);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObject;
        ConstantBuffer<CalculateMeanVarianceConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };

    class CalculateMeanVariance
    {
    public:
        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            ID3D12DescriptorHeap* descriptorHeap,
            UINT width,
            UINT height,
            D3D12_GPU_DESCRIPTOR_HANDLE inputValuesResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputMeanVarianceResourceHandle,
            UINT kernelWidth,
            bool doCheckerboardSampling = false,
            bool checkerboardLoadEvenPixels = false);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObject;
        ConstantBuffer<CalculateMeanVarianceConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };

    class TemporalSupersampling_ReverseReproject
    {
    public:
        // ToDo set default parameters
        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            UINT width,
            UINT height,
            ID3D12DescriptorHeap* descriptorHeap,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCurrentFrameNormalDepthResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCurrentFrameLinearDepthDerivativeResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputReprojectedNormalDepthResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputTextureSpaceMotionVectorResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCachedValueResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCachedNormalDepthResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCachedTsppResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCachedSquaredMeanValue,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCachedRayHitDistanceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputReprojectedCacheTsppResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputReprojectedCacheValuesResourceHandle,
            float minSmoothingFactor,
            float depthTolerance,
            bool useDepthWeights,
            bool useNormalWeights,
            float floatEpsilonDepthTolerance,
            float depthDistanceBasedDepthTolerance,
            float depthSigma,
            bool usingBilateralDownsampledBuffers,
            bool perspectiveCorrectDepthInterpolation,
            GpuResource debugResources[2],
            UINT maxTspp);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObject;
        ConstantBuffer<TemporalSupersampling_ReverseReprojectConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };


    class TemporalSupersampling_BlendWithCurrentFrame
    {
    public:

        // ToDo set default parameters
        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            UINT width,
            UINT height,
            ID3D12DescriptorHeap* descriptorHeap,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCurrentFrameValueResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCurrentFrameLocalMeanVarianceResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputCurrentFrameRayHitDistanceResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputOutputValueResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputOutputTsppResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputOutputSquaredMeanValueResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputOutputRayHitDistanceResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputReprojectedCacheValuesResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputVarianceResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputBlurStrengthResourceHandle,
            float minSmoothingFactor,
            bool forceUseMinSmoothingFactor,
            bool clampCachedValues,
            float clampStdDevGamma,
            float clampMinStdDevTolerance,
            UINT minTsppToUseTemporalVariance,
            float clampDifferenceToTsppScale,   // ToDo remove?
            GpuResource debugResources[2],
            UINT lowTsppBlurStrengthMaxTspp,
            float lowTsppBlurStrengthDecayConstant,
            bool doCheckerboardSampling = false,
            bool checkerboardLoadEvenPixels = false);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObject;
        ConstantBuffer<TemporalSupersampling_BlendWithCurrentFrameConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };

    class SortRays
    {
    public:
        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            float binDepthSize,
            UINT width,
            UINT height,
            bool useOctahedralRayDirectionQuantization,
            ID3D12DescriptorHeap* descriptorHeap,
            D3D12_GPU_DESCRIPTOR_HANDLE inputRayDirectionOriginDepthResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputSortedToSourceRayIndexOffsetResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE outputDebugResourceHandle);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObject;

        ConstantBuffer<SortRaysConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };

    class AORayGenerator
    {
    public:
        void Initialize(ID3D12Device5* device, UINT frameCount, UINT numCallsPerFrame = 1);
        void Run(
            ID3D12GraphicsCommandList4* commandList,
            UINT width,
            UINT height,
            UINT seed,
            UINT numSamplesPerSet,
            UINT numSampleSets,
            UINT numPixelsPerDimPerSet,
            bool doCheckerboardRayGeneration,
            bool checkerboardGenerateRaysForEvenPixels,
            ID3D12DescriptorHeap* descriptorHeap,
            D3D12_GPU_DESCRIPTOR_HANDLE inputRayOriginSurfaceNormalDepthResourceHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE inputRayOriginPositionResourceHandle,
            D3D12_GPU_VIRTUAL_ADDRESS inputAlignedHemisphereSamplesBufferAddress,
            D3D12_GPU_DESCRIPTOR_HANDLE outputRayDirectionOriginDepthResourceHandle);

    private:
        ComPtr<ID3D12RootSignature>         m_rootSignature;
        ComPtr<ID3D12PipelineState>         m_pipelineStateObject;

        ConstantBuffer<RayGenConstantBuffer> m_CB;
        UINT                                m_CBinstanceID = 0;
    };
}