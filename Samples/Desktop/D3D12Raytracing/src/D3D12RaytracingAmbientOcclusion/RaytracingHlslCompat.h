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

#ifndef RAYTRACINGHLSLCOMPAT_H
#define RAYTRACINGHLSLCOMPAT_H

/*
//ToDo
- cleanup filter shaders, remove obsolete

- finetune clamping/remove ghosting (test gliding spaceship)
- Adaptive kernel size - overblur under roof edge
- Full res - adaptive kernel size visible horizontal lines
- finetune adaptive kernel size
- improve the multi-blur - skip higher iter blur on higher frame age.
- depth aware variance calculation
- Cleanup UI paths
- Total GPU time >> sum of component gpu times??

- Double check
- Double check that passes either check whether a pixel value is valid from previous pass/frame or the value gets ignored.
- fix the temporal variance shimmer on boundaries with skybox.
- map local variance to only valid AO values
- TAO fails on dragons surface on small rotaions
    - clean up PIX /GPU validation warnings on Debug
    - Debug break on FS on 1080 display resolution.
    - White halo under tiers.
    - Upsampling artifacts on 1080(540p)
    - RTAO invisible wall inside kitchen on long rays
    PIX shows empty rows (~as many as valid rows) in between entries in multi frame cb.

    - Finetune
   - Fine tune min std dev tolerance in clamping


- Cleanup:
    double check all CS for out of bounds.
    clean up scoped timer names.
    - Add/revise comments. Incl file decs
    - Move global defines in RaytracingSceneDefines.h locally for RTAO and Denoiser.
    - Build with higher warning bar and cleanup
    - move shader dependencies to components?
    standarddize ddxy vs dxdy
    // make sure no shaders are writing to debug resources
    remove obsolete filtering hlsl kernels
    cleanup CBs, remove obsolete entries
    remove obsolete GPukernel vars
    remove RTAO_ from names in RTAO component
    remove obsolete composition modes
    rename maxFrameAge to tspp

    - cleanup this file

- Sample generic
    - Add device removal support or remove it being announced

    Readme:
    -   Open issues/that can be improved:
        - Variable rate ray tracing
        - Improve disocclusion detection (trailing below car AO)

*/

//**********************************************************************************************
//
// RaytracingHLSLCompat.h
//
// A header with shared definitions for C++ and HLSL source files. 
//
//**********************************************************************************************



#define FOVY 45.f
#define NEAR_PLANE 0.001f
#define FAR_PLANE 1000.0f

#define RAYTRACING_MANUAL_KERNEL_STEP_SHIFTS 1      // ToDo cleanup

#define RTAO_MARK_CACHED_VALUES_NEGATIVE  1   // ToDo cleanup
#define RTAO_GAUSSIAN_BLUR_AFTER_Temporal 0
#if RTAO_GAUSSIAN_BLUR_AFTER_Temporal && RTAO_MARK_CACHED_VALUES_NEGATIVE
Incompatible macros
#endif

#define SAMPLER_FILTER D3D12_FILTER_ANISOTROPIC

// ToDo
#define ENABLE_PROFILING 0
#define ENABLE_LAZY_RENDER 0

#define DISTANCE_ON_MISS 65504  // ~FLT_MAX within 16 bit format // ToDo explain

#define PRINT_OUT_CAMERA_CONFIG 0

#ifdef HLSL
typedef uint NormalDepthTexFormat;
#else
#define COMPACT_NORMAL_DEPTH_DXGI_FORMAT DXGI_FORMAT_R32_UINT
#endif

#define ENABLE_VSYNC 1

#ifdef _DEBUG
#define LOAD_ONLY_ONE_PBRT_MESH 1 
#else
#define LOAD_ONLY_ONE_PBRT_MESH 0 
#endif

#define AO_RAY_T_MAX 22

namespace ReduceSumCS {
	namespace ThreadGroup {
		enum Enum { Width = 8, Height = 16, Size = Width * Height, NumElementsToLoadPerThread = 10 };	
	}
}

namespace AtrousWaveletTransformFilterCS {
    namespace ThreadGroup {
        enum Enum { Width = 16, Height = 16, Size = Width * Height };
    }
}

namespace DefaultComputeShaderParams {
    namespace ThreadGroup {
        enum Enum { Width = 8, Height = 8, Size = Width * Height };
    }
}


#ifdef HLSL
#include "HlslCompat.hlsli"
typedef UINT Index;
#else
using namespace DirectX;

typedef UINT Index;
#endif



struct ProceduralPrimitiveAttributes
{
    XMFLOAT3 normal;
};

struct Ray
{
    XMFLOAT3 origin;
    XMFLOAT3 direction;
};

struct AmbientOcclusionGBuffer
{
    float tHit;
    XMFLOAT3 hitPosition;           // Position of the hit for which to calculate Ambient coefficient.
    UINT diffuseByte3;              // Diffuse reflectivity of the hit surface.
    XMFLOAT2 encodedNormal;         // Normal of the hit surface. // ToDo encode as 16bit

    // Members for Motion Vector calculation.
    XMFLOAT3 _virtualHitPosition;   // virtual hitPosition in the previous frame.
                                    // For non-reflected points this is a true world position of a hit.
                                    // For reflected points, this is a world position of a hit reflected across the reflected surface 
                                    //   ultimately giving the same screen space coords when projected and the depth corresponding to the ray depth.
    XMFLOAT2 _encodedNormal;        // normal in the previous frame
};


// ToDo rename To Pathtracer
struct PathtracerRayPayload
{
    UINT rayRecursionDepth;
    XMFLOAT3 radiance;
    AmbientOcclusionGBuffer AOGBuffer;
};

struct ShadowRayPayload
{
    float tHit;         // Hit time <0,..> on Hit. -1 on miss.
};

struct AtrousWaveletTransformFilterConstantBuffer
{
    XMUINT2 textureDim;
    UINT kernelStepShift;
    UINT kernelWidth;

    float valueSigma;
    float depthSigma;
    float normalSigma;
    UINT useCalculatedVariance;
    
    UINT useApproximateVariance;
    BOOL outputFilteredValue;
    BOOL outputFilteredVariance;
    BOOL outputFilterWeightSum;

    BOOL perspectiveCorrectDepthInterpolation;
    BOOL useAdaptiveKernelSize;
    float minHitDistanceToKernelWidthScale;
    UINT minKernelWidth;

    UINT maxKernelWidth;
    float varianceSigmaScaleOnSmallKernels;
    bool usingBilateralDownsampledBuffers;
    UINT DepthNumMantissaBits;

    float minVarianceToDenoise;
    float weightScale;
    float staleNeighborWeightScale;
    float depthWeightCutoff;

    BOOL useProjectedDepthTest;
    BOOL forceDenoisePass;
    float kernelStepScale;
    float weightByFrameAge;
};

struct CalculateVariance_BilateralFilterConstantBuffer
{
    XMUINT2 textureDim;
    float normalSigma;
    float depthSigma;

    BOOL outputMean;
    BOOL useDepthWeights;
    BOOL useNormalWeights;
    UINT kernelWidth;

    UINT kernelRadius;
    float padding[3];
};

struct CalculateMeanVarianceConstantBuffer
{
    XMUINT2 textureDim;
    UINT kernelWidth;
    UINT kernelRadius;

    BOOL doCheckerboardSampling;
    BOOL areEvenPixelsActive;
    UINT pixelStepY;
    float padding;
};

struct RayGenConstantBuffer
{
    XMUINT2 textureDim;
    BOOL doCheckerboardRayGeneration;
    BOOL checkerboardGenerateRaysForEvenPixels;
    
    UINT seed;
    UINT numSamplesPerSet;
    UINT numSampleSets;
    UINT numPixelsPerDimPerSet;
};

struct SortRaysConstantBuffer
{
    XMUINT2 dim;

    BOOL useOctahedralRayDirectionQuantization;

    // Depth for a bin within which to sort further based on direction.
    float binDepthSize;
};

namespace SortRays {
    namespace ThreadGroup {
        enum Enum { Width = 64, Height = 16, Size = Width * Height };
    }

    // ToDo comment ray group's heigh can only go up to 64 as the most significant bit is used to test if the cached value is valid.
    namespace RayGroup {
        enum Enum { NumElementPairsPerThread = 4, Width = ThreadGroup::Width, Height = NumElementPairsPerThread * 2 * ThreadGroup::Height, Size = Width * Height };
    }
#ifndef HLSL
    static_assert( RayGroup::Width < 128 
                && RayGroup::Height < 256
                && RayGroup::Size <= 8192, "Ray group dimensions are outside the supported limits set by the Counting Sort shader.");
#endif
}

struct PathtracerConstantBuffer
{
    // ToDo rename to world to view matrix and drop (0,0,0) note.
    XMMATRIX projectionToWorldWithCameraEyeAtOrigin;	// projection to world matrix with Camera at (0,0,0).
    XMFLOAT3 cameraPosition;
    BOOL     useDiffuseFromMaterial;
    XMFLOAT3 lightPosition;     
    BOOL     useNormalMaps;
    XMFLOAT3 lightColor;
    float    defaultAmbientIntensity;

    XMMATRIX prevViewProj;
    XMMATRIX prevProjToWorldWithCameraEyeAtOrigin;	// projection to world matrix with Camera at (0,0,0).
    XMFLOAT3 prevCameraPosition;
    float    padding;

	float Znear;
	float Zfar;
    UINT  maxRadianceRayRecursionDepth;
    UINT  maxShadowRayRecursionDepth;

};

struct RTAOConstantBuffer
{
    UINT seed;
    UINT numSamplesPerSet;
    UINT numSampleSets;
    UINT numPixelsPerDimPerSet;

    // ToDo rename to AOray
    float maxShadowRayHitTime;             // Max shadow ray hit time used for tMax in TraceRay.
    BOOL approximateInterreflections;      // Approximate interreflections. 
    float diffuseReflectanceScale;              // Diffuse reflectance from occluding surfaces. 
    float minimumAmbientIllumination;       // Ambient illumination coef when a ray is occluded.

    // toDo rename shadow to AO
    float maxTheoreticalShadowRayHitTime;  // Max shadow ray hit time used in falloff computation accounting for
                                                // RTAO_ExponentialFalloffMinOcclusionCutoff and maxShadowRayHitTime.    
    BOOL RTAO_UseSortedRays;
    XMUINT2 raytracingDim;

    BOOL isExponentialFalloffEnabled;               // Apply exponential falloff to AO coefficient based on ray hit distance.    
    float exponentialFalloffDecayConstant;
    BOOL doCheckerboardSampling;
    BOOL areEvenPixelsActive;

    UINT rpp;
    float padding[3];
};

 
// Final render output composition modes.
enum CompositionType {
    PBRShading = 0,
    AmbientOcclusionOnly_Denoised,
    AmbientOcclusionOnly_TemporallySupersampled,
    AmbientOcclusionOnly_RawOneFrame,
    AmbientOcclusionAndDisocclusionMap, // ToDo quarter res support
    AmbientOcclusionVariance,
    AmbientOcclusionLocalVariance,  // ToDo rename spatial to local variance references
    RTAOHitDistance,    // ToDo standardize naming
    NormalsOnly,
    DepthOnly,
    Diffuse,
    DisocclusionMap,
    Count
};

namespace TextureResourceFormatRGB
{
    enum Type {
        R32G32B32A32_FLOAT = 0,
        R16G16B16A16_FLOAT,
        R11G11B10_FLOAT,
        Count
    };
#ifndef HLSL
    inline DXGI_FORMAT ToDXGIFormat(UINT type)
    {
        switch (type)
        {
        case R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case R11G11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
        }
        return DXGI_FORMAT_UNKNOWN;
    }
#endif
}

namespace TextureResourceFormatR
{
    enum Type {
        R32_FLOAT = 0,
        R16_FLOAT,
        R8_UNORM,
        Count
    };
#ifndef HLSL
    inline DXGI_FORMAT ToDXGIFormat(UINT type)
    {
        switch (type)
        {
        case R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
        case R8_UNORM: return DXGI_FORMAT_R8_UNORM;
        }
        return DXGI_FORMAT_UNKNOWN;
    }
#endif
}


namespace TextureResourceFormatRG
{
    enum Type {
        R32G32_FLOAT = 0,
        R16G16_FLOAT,
        R8G8_SNORM,
        Count
    };
#ifndef HLSL
    inline DXGI_FORMAT ToDXGIFormat(UINT type)
    {
        switch (type)
        {
        case R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
        case R16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
        case R8G8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
        }
        return DXGI_FORMAT_UNKNOWN;
    }
#endif
}

struct ComposeRenderPassesConstantBuffer
{
    CompositionType compositionType;
    UINT isAOEnabled;
    float RTAO_MaxRayHitDistance;
    float defaultAmbientIntensity;
    
    BOOL variance_visualizeStdDeviation;
    float variance_scale;
    float padding[2];
};

struct TextureDimConstantBuffer
{
    XMUINT2 textureDim;
    XMFLOAT2 invTextureDim;
};

struct FilterConstantBuffer
{
    XMUINT2 textureDim;
    UINT step;
    float padding;
};


// ToDo rename be more specific
struct BilateralFilterConstantBuffer
{
    XMUINT2 textureDim;
    UINT step;
    BOOL readWriteUAV_and_skipPassthrough;

    float normalWeightExponent;
    float minNormalWeightStrength;
    float padding[2];
};

struct TemporalSupersampling_ReverseReprojectConstantBuffer
{
    // ToDo pix missinterprets the format
    XMUINT2 textureDim;
    XMFLOAT2 invTextureDim;

    XMMATRIX projectionToWorldWithCameraEyeAtOrigin;
    XMMATRIX prevProjectionToWorldWithCameraEyeAtOrigin;

    BOOL useDepthWeights;
    BOOL useNormalWeights;
    float depthSigma;
    float depthTolerance;

    BOOL useWorldSpaceDistance;
    UINT DepthNumMantissaBits;      // Number of Mantissa Bits in the floating format of the input depth resources format.
    BOOL usingBilateralDownsampledBuffers;
    BOOL perspectiveCorrectDepthInterpolation;

    float floatEpsilonDepthTolerance;
    float depthDistanceBasedDepthTolerance;
    UINT numRaysToTraceAfterTemporalAtMaxFrameAge;
    UINT maxFrameAge;
};

struct TemporalSupersampling_BlendWithCurrentFrameConstantBuffer
{
    XMUINT2 textureDim;
    XMFLOAT2 invTextureDim;

    BOOL forceUseMinSmoothingFactor;
    BOOL clampCachedValues;
    float minSmoothingFactor;
    float stdDevGamma;

    UINT minFrameAgeToUseTemporalVariance;
    float minStdDevTolerance;
    float frameAgeAdjustmentDueClamping;
    float clampDifferenceToFrameAgeScale;

    UINT numFramesToDenoiseAfterLastTracedRay;
    UINT blurStrength_MaxFrameAge;
    float blurDecayStrength;
    float padding;

    BOOL doCheckerboardSampling;
    BOOL areEvenPixelsActive;
    float padding2[2];
};

struct CalculatePartialDerivativesConstantBuffer
{
    XMUINT2 textureDim;
    float padding[2];
};

struct DownAndUpsampleFilterConstantBuffer
{
    XMFLOAT2 invHiResTextureDim;
    XMFLOAT2 invLowResTextureDim;

    // ToDo remove
    BOOL useNormalWeights;
    BOOL useDepthWeights;
    BOOL useBilinearWeights;
    BOOL useDynamicDepthThreshold;
};

struct GenerateGrassStrawsConstantBuffer_AppParams
{
    XMUINT2 activePatchDim; // Dimensions of active grass straws.
    XMUINT2 maxPatchDim;    // Dimensions of the whole vertex buffer.

    XMFLOAT2 timeOffset;
    float grassHeight;
    float grassScale;
    
    XMFLOAT3 patchSize;
    float grassThickness;

    XMFLOAT3 windDirection;
    float windStrength;

    float positionJitterStrength;
    float bendStrengthAlongTangent;
    float padding[2];
};

// ToDo move?
#define N_GRASS_TRIANGLES 5
#define N_GRASS_VERTICES 7
#define MAX_GRASS_STRAWS_1D 100
struct GenerateGrassStrawsConstantBuffer
{
    XMFLOAT2 invActivePatchDim;
    float padding1; // ToDo doing float p[2]; instead adds extra padding - as per PIX.
    float padding2;
    GenerateGrassStrawsConstantBuffer_AppParams p;
};


// Attributes per primitive type.
struct PrimitiveConstantBuffer
{
	UINT     materialID;          
    UINT     isVertexAnimated; 
    UINT     padding[2];
};

namespace MaterialType {
    enum Type {
        Default,
        Matte,  // Lambertian scattering
        Mirror,   // Specular reflector that isn't modified by the Fersnel equations.
        AnalyticalCheckerboardTexture
    };
}

// ToDO use same naming as in PBR Material
struct PrimitiveMaterialBuffer
{
	XMFLOAT3 Kd;
	XMFLOAT3 Ks;
    XMFLOAT3 Kr;
    XMFLOAT3 Kt;
    XMFLOAT3 opacity;
    XMFLOAT3 eta;
    float roughness;
    UINT hasDiffuseTexture; // ToDO use BOOL?
    UINT hasNormalTexture;
    UINT hasPerVertexTangents;
    MaterialType::Type type;
    float padding;
};

// Attributes per primitive instance.
struct PrimitiveInstanceConstantBuffer
{
    UINT instanceIndex;  
    UINT primitiveType; // Procedural primitive type
    float padding[2];
};

// Dynamic attributes per primitive instance.
struct PrimitiveInstancePerFrameBuffer
{
    XMMATRIX localSpaceToBottomLevelAS;   // Matrix from local primitive space to bottom-level object space.
    XMMATRIX bottomLevelASToLocalSpace;   // Matrix from bottom-level object space to local primitive space.
};

struct AlignedUnitSquareSample2D
{
    XMFLOAT2 value;
    XMUINT2 padding;  // Padding to 16B
};

struct AlignedHemisphereSample3D
{
    XMFLOAT3 value;
    UINT padding;  // Padding to 16B
};

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
};

struct VertexPositionNormalTexture
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 uv;
};

// ToDo dedupe with Vertex in PBRT.
struct VertexPositionNormalTextureTangent
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 textureCoordinate;
	XMFLOAT3 tangent;
};


// Ray types traced in this sample.
namespace RayType {
    enum Enum {
        Radiance = 0,	// ~ Radiance ray generating color and GBuffer data.
        Shadow,         // ~ Shadow/visibility rays, only testing for occlusion
        Count
    };
}

namespace RTAORayType {
    enum Enum {
        AO = 0,	
        Count
    };
}


namespace TraceRayParameters
{
    static const UINT InstanceMask = ~0;   // Everything is visible.
    namespace HitGroup {
        static const UINT Offset[RayType::Count] =
        {
            0, // Radiance ray
            1, // Shadow ray
        };
		static const UINT GeometryStride = RayType::Count;
    }
    namespace MissShader {
        static const UINT Offset[RayType::Count] =
        {
            0, // Radiance ray
            1, // Shadow ray
        };
    }
}

namespace RTAOTraceRayParameters
{
    static const UINT InstanceMask = ~0;   // Everything is visible.
    namespace HitGroup {
        static const UINT Offset[RTAORayType::Count] =
        {
            0, // AO ray
        };
        // Since there is only one closest hit shader across shader records in RTAO, 
        // always access the first shader record of each BLAS instance shader record range.
        static const UINT GeometryStride = 0;
    }
    namespace MissShader {
        static const UINT Offset[RTAORayType::Count] =
        {
            0, // AO ray
        };
    }
}

static const XMFLOAT4 BackgroundColor = XMFLOAT4(0.79f, 0.88f, 0.98f, 1.0f);

#endif // RAYTRACINGHLSLCOMPAT_H