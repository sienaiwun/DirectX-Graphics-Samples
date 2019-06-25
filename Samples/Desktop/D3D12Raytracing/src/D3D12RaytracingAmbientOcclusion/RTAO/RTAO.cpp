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

#include "stdafx.h"
#include "RTAO.h"
#include "GameInput.h"
#include "EngineTuning.h"
#include "EngineProfiling.h"
#include "GpuTimeManager.h"
#include "D3D12RaytracingAmbientOcclusion.h"
#include "CompiledShaders\RTAO.hlsl.h"


// ToDo prune unused
using namespace std;
using namespace DX;
using namespace DirectX;
using namespace SceneEnums;

// Shader entry points.
const wchar_t* RTAO::c_rayGenShaderNames[] = { L"RayGenShader", L"RayGenShader_sortedRays" };
const wchar_t* RTAO::c_closestHitShaderName = L"ClosestHitShader";
const wchar_t* RTAO::c_missShaderName = L"MissShader";

// Hit groups.
const wchar_t* RTAO::c_hitGroupName = L"HitGroup_Triangle";

// Singleton instance.
RTAO* global_pRTAO;
UINT RTAO::s_numInstances = 0;

namespace SceneArgs
{
    void OnRecreateRTAORaytracingResources(void*)
    {
        global_pRTAO->RequestRecreateRaytracingResources();
    }

    void OnRecreateSamples(void*)
    {
        global_pRTAO->RequestRecreateAOSamples();
    }

    IntVar AOTileX(L"Render/AO/Tile X", 1, 1, 128, 1);
    IntVar AOTileY(L"Render/AO/Tile Y", 1, 1, 128, 1);

    BoolVar RTAOUseRaySorting(L"Render/AO/RTAO/Ray Sorting/Enabled", false);
    NumVar RTAORayBinDepthSizeMultiplier(L"Render/AO/RTAO/Ray Sorting/Ray bin depth size (multiplier of MaxRayHitTime)", 0.1f, 0.01f, 10.f, 0.01f);
    BoolVar RTAORaySortingUseOctahedralRayDirectionQuantization(L"Render/AO/RTAO/Ray Sorting/Octahedral ray direction quantization", true);

    // RTAO
    // Adaptive Sampling.
    BoolVar RTAOAdaptiveSampling(L"Render/AO/RTAO/Adaptive Sampling/Enabled", false);
    NumVar RTAOAdaptiveSamplingMaxFilterWeight(L"Render/AO/RTAO/Adaptive Sampling/Filter weight cutoff for max sampling", 0.995f, 0.0f, 1.f, 0.005f);
    BoolVar RTAOAdaptiveSamplingMinMaxSampling(L"Render/AO/RTAO/Adaptive Sampling/Only min/max sampling", false);
    NumVar RTAOAdaptiveSamplingScaleExponent(L"Render/AO/RTAO/Adaptive Sampling/Sampling scale exponent", 0.3f, 0.0f, 10, 0.1f);
    BoolVar RTAORandomFrameSeed(L"Render/AO/RTAO/Random per-frame seed", false);

    // ToDo remove
    NumVar RTAOTraceRayOffsetAlongNormal(L"Render/AO/RTAO/TraceRay/Ray origin offset along surface normal", 0.001f, 0, 0.1f, 0.0001f);
    NumVar RTAOTraceRayOffsetAlongRayDirection(L"Render/AO/RTAO/TraceRay/Ray origin offset fudge along ray direction", 0, 0, 0.1f, 0.0001f);



    const WCHAR* FloatingPointFormatsR[TextureResourceFormatR::Count] = { L"R32_FLOAT", L"R16_FLOAT", L"R8_UNORM" };
    EnumVar RTAO_AmbientCoefficientResourceFormat(L"Render/Texture Formats/AO/RTAO/Ambient Coefficient", TextureResourceFormatR::R8_UNORM, TextureResourceFormatR::Count, FloatingPointFormatsR, OnRecreateRTAORaytracingResources);

  
    // ToDo cleanup RTAO... vs RTAO_..
    IntVar RTAOAdaptiveSamplingMinSamples(L"Render/AO/RTAO/Adaptive Sampling/Min samples", 1, 1, AO_SPP_N* AO_SPP_N, 1);

    // ToDo remove
    IntVar AOSampleCountPerDimension(L"Render/AO/RTAO/Samples per pixel NxN", AO_SPP_N, 1, AO_SPP_N_MAX, 1, OnRecreateSamples, nullptr);
    IntVar AOSampleSetDistributedAcrossPixels(L"Render/AO/RTAO/Sample set distribution across NxN pixels ", 8, 1, 8, 1, OnRecreateSamples, nullptr);
#if LOAD_PBRT_SCENE
    NumVar RTAOMaxRayHitTime(L"Render/AO/RTAO/Max ray hit time", AO_RAY_T_MAX, 0.0f, 50.0f, 0.2f);
#else
    NumVar RTAOMaxRayHitTime(L"Render/AO/RTAO/Max ray hit time", AO_RAY_T_MAX, 0.0f, 1000.0f, 4);
#endif
    BoolVar RTAOApproximateInterreflections(L"Render/AO/RTAO/Approximate Interreflections/Enabled", true);
    NumVar RTAODiffuseReflectanceScale(L"Render/AO/RTAO/Approximate Interreflections/Diffuse Reflectance Scale", 0.5f, 0.0f, 1.0f, 0.1f);
    NumVar  RTAO_MinimumAmbientIllumination(L"Render/AO/RTAO/Minimum Ambient Illumination", 0.07f, 0.0f, 1.0f, 0.01f);
    BoolVar RTAOIsExponentialFalloffEnabled(L"Render/AO/RTAO/Exponential Falloff", true);
    NumVar RTAO_ExponentialFalloffDecayConstant(L"Render/AO/RTAO/Exponential Falloff Decay Constant", 2.f, 0.0f, 20.f, 0.25f);
    NumVar RTAO_ExponentialFalloffMinOcclusionCutoff(L"Render/AO/RTAO/Exponential Falloff Min Occlusion Cutoff", 0.4f, 0.0f, 1.f, 0.05f);       // ToDo Finetune document perf.
};



RTAO::RTAO()
{
    ThrowIfFalse(++s_numInstances == 1, L"There can be only one RTAO instance.");
    global_pRTAO = this;

    for (auto& rayGenShaderTableRecordSizeInBytes : m_rayGenShaderTableRecordSizeInBytes)
    {
        rayGenShaderTableRecordSizeInBytes = UINT_MAX;
    }
    m_generatorURNG.seed(1729);
}


void RTAO::Setup(shared_ptr<DeviceResources> deviceResources, shared_ptr<DX::DescriptorHeap> descriptorHeap, UINT maxInstanceContributionToHitGroupIndex)
{
    m_deviceResources = deviceResources;
    m_cbvSrvUavHeap = descriptorHeap;

    CreateDeviceDependentResources(maxInstanceContributionToHitGroupIndex);
}

void RTAO::ReleaseDeviceDependentResources()
{ 
    // ToDo 
}

// Create resources that depend on the device.
void RTAO::CreateDeviceDependentResources(UINT maxInstanceContributionToHitGroupIndex)
{
    auto device = m_deviceResources->GetD3DDevice();
    
    CreateAuxilaryDeviceResources();

    // Initialize raytracing pipeline.

    // Create root signatures for the shaders.
    CreateRootSignatures();

    // Create a raytracing pipeline state object which defines the binding of shaders, state and resources to be used during raytracing.
    CreateRaytracingPipelineStateObject();

    // Create constant buffers for the geometry and the scene.
    CreateConstantBuffers();

    // Build shader tables, which define shaders and their local root arguments.
    BuildShaderTables(maxInstanceContributionToHitGroupIndex);

    // ToDo rename
    CreateSamplesRNG();
}


// ToDo rename
void RTAO::CreateAuxilaryDeviceResources()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandQueue = m_deviceResources->GetCommandQueue();
    auto commandList = m_deviceResources->GetCommandList();
    auto FrameCount = m_deviceResources->GetBackBufferCount();

    EngineProfiling::RestoreDevice(device, commandQueue, FrameCount);
    ResourceUploadBatch resourceUpload(device);
    resourceUpload.Begin();

    // ToDo move?
    m_reduceSumKernel.Initialize(device, GpuKernels::ReduceSum::Uint);
    m_raySorter.Initialize(device, FrameCount);

    // Upload the resources to the GPU.
    auto finish = resourceUpload.End(commandQueue);

    // Wait for the upload thread to terminate
    finish.wait();
}

// Create constant buffers.
void RTAO::CreateConstantBuffers()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto FrameCount = m_deviceResources->GetBackBufferCount();

    m_CB.Create(device, FrameCount, L"RTAO Constant Buffer");
}


void RTAO::CreateRootSignatures()
{
    auto device = m_deviceResources->GetD3DDevice();

    // Global Root Signature
    // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
    {
        using namespace RTAOGlobalRootSignature;

        // ToDo use slot index in ranges everywhere
        CD3DX12_DESCRIPTOR_RANGE ranges[Slot::Count]; // Perfomance TIP: Order from most frequent to least frequent.
                                                      // ToDo reorder
        ranges[Slot::AOResourcesOut].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 10);      // 2 output AO textures
        ranges[Slot::AORayHitDistance].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 15);    // 1 output ray hit distance texture
        ranges[Slot::AORayDirectionOriginDepthHitUAV].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 22);  // 1 output AO ray direction and origin depth texture

        ranges[Slot::RayOriginPosition].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);  // 1 input surface hit position texture
        ranges[Slot::RayOriginSurfaceNormalDepth].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);  // 1 input surface normal depth
        ranges[Slot::FilterWeightSum].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 13);  // 1 input filter weight sum texture
        ranges[Slot::AOFrameAge].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 14);  // 1 input AO frame age
        ranges[Slot::AORayDirectionOriginDepthHitSRV].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 22);  // 1 AO ray direction and origin depth texture
        ranges[Slot::AOSortedToSourceRayIndex].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 23);  // 1 input AO ray group thread offsets
        ranges[Slot::AOSurfaceAlbedo].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 24);  // 1 input AO surface diffuse texture

        CD3DX12_ROOT_PARAMETER rootParameters[Slot::Count];
        rootParameters[Slot::RayOriginPosition].InitAsDescriptorTable(1, &ranges[Slot::RayOriginPosition]);
        rootParameters[Slot::RayOriginSurfaceNormalDepth].InitAsDescriptorTable(1, &ranges[Slot::RayOriginSurfaceNormalDepth]);
        rootParameters[Slot::AOResourcesOut].InitAsDescriptorTable(1, &ranges[Slot::AOResourcesOut]);
        rootParameters[Slot::FilterWeightSum].InitAsDescriptorTable(1, &ranges[Slot::FilterWeightSum]);
        rootParameters[Slot::AORayHitDistance].InitAsDescriptorTable(1, &ranges[Slot::AORayHitDistance]);
        rootParameters[Slot::AOFrameAge].InitAsDescriptorTable(1, &ranges[Slot::AOFrameAge]);
        rootParameters[Slot::AORayDirectionOriginDepthHitSRV].InitAsDescriptorTable(1, &ranges[Slot::AORayDirectionOriginDepthHitSRV]);
        rootParameters[Slot::AOSortedToSourceRayIndex].InitAsDescriptorTable(1, &ranges[Slot::AOSortedToSourceRayIndex]);
        rootParameters[Slot::AORayDirectionOriginDepthHitUAV].InitAsDescriptorTable(1, &ranges[Slot::AORayDirectionOriginDepthHitUAV]);
        rootParameters[Slot::AOSurfaceAlbedo].InitAsDescriptorTable(1, &ranges[Slot::AOSurfaceAlbedo]);

        rootParameters[Slot::AccelerationStructure].InitAsShaderResourceView(0);
        rootParameters[Slot::ConstantBuffer].InitAsConstantBufferView(0);		// ToDo rename to ConstantBuffer
        rootParameters[Slot::SampleBuffers].InitAsShaderResourceView(4);

        CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
        SerializeAndCreateRootSignature(device, globalRootSignatureDesc, &m_raytracingGlobalRootSignature, L"RTAO Global root signature");
    }
}


// DXIL library
// This contains the shaders and their entrypoints for the state object.
// Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.
void RTAO::CreateDxilLibrarySubobject(CD3DX12_STATE_OBJECT_DESC* raytracingPipeline)
{
    auto lib = raytracingPipeline->CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((void*)g_pRTAO, ARRAYSIZE(g_pRTAO));
    lib->SetDXILLibrary(&libdxil);
    // Use default shader exports for a DXIL library/collection subobject ~ surface all shaders.
}

// Hit groups
// A hit group specifies closest hit, any hit and intersection shaders 
// to be executed when a ray intersects the geometry.
void RTAO::CreateHitGroupSubobjects(CD3DX12_STATE_OBJECT_DESC* raytracingPipeline)
{
    // Triangle geometry hit groups
    {
        auto hitGroup = raytracingPipeline->CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();

        hitGroup->SetClosestHitShaderImport(c_closestHitShaderName);
        hitGroup->SetHitGroupExport(c_hitGroupName);
        hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);
    }
}


// Local root signature and shader association
// This is a root signature that enables a shader to have unique arguments that come from shader tables.
void RTAO::CreateLocalRootSignatureSubobjects(CD3DX12_STATE_OBJECT_DESC* raytracingPipeline)
{
    // RTAO shaders in this sample are not using a local root signature and thus one is not associated with them.

    // ToDo remove?
    // Hit groups
    // Triangle geometry
    {
        auto localRootSignature = raytracingPipeline->CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        localRootSignature->SetRootSignature(m_raytracingLocalRootSignature.Get());
    }
}

// Create a raytracing pipeline state object (RTPSO).
// An RTPSO represents a full set of shaders reachable by a DispatchRays() call,
// with all configuration options resolved, such as local signatures and other state.
void RTAO::CreateRaytracingPipelineStateObject()
{
    auto device = m_deviceResources->GetD3DDevice();
    // Ambient Occlusion state object.
    {
        // ToDo review
        // Create 18 subobjects that combine into a RTPSO:
        // Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
        // Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
        // This simple sample utilizes default shader association except for local root signature subobject
        // which has an explicit association specified purely for demonstration purposes.
        // 1 - DXIL library
        // 8 - Hit group types - 4 geometries (1 triangle, 3 aabb) x 2 ray types (ray, shadowRay)
        // 1 - Shader config
        // 6 - 3 x Local root signature and association
        // 1 - Global root signature
        // 1 - Pipeline config
        CD3DX12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

        bool loadShadowShadersOnly = true;
        // DXIL library
        CreateDxilLibrarySubobject(&raytracingPipeline);

        // Hit groups
        CreateHitGroupSubobjects(&raytracingPipeline);

        // ToDo try 2B float payload
#define AO_4B_RAYPAYLOAD 0

        // Shader config
        // Defines the maximum sizes in bytes for the ray rayPayload and attribute structure.
        auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
#if AO_4B_RAYPAYLOAD
        UINT payloadSize = static_cast<UINT>(sizeof(ShadowRayPayload));		// ToDo revise
#else
        UINT payloadSize = static_cast<UINT>(max(max(sizeof(RayPayload), sizeof(ShadowRayPayload)), sizeof(GBufferRayPayload)));		// ToDo revise
#endif
        UINT attributeSize = sizeof(XMFLOAT2);  // float2 barycentrics
        shaderConfig->Config(payloadSize, attributeSize);

        // Local root signature and shader association
        // This is a root signature that enables a shader to have unique arguments that come from shader tables.
        CreateLocalRootSignatureSubobjects(&raytracingPipeline);

        // Global root signature
        // This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
        auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
        globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

        // Pipeline config
        // Defines the maximum TraceRay() recursion depth.
        auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
        // PERFOMANCE TIP: Set max recursion depth as low as needed
        // as drivers may apply optimization strategies for low recursion depths.
        UINT maxRecursionDepth = 1;
        pipelineConfig->Config(maxRecursionDepth);

        PrintStateObjectDesc(raytracingPipeline);

        // Create the state object.
        ThrowIfFailed(device->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObject)), L"Couldn't create DirectX Raytracing state object.\n");
    }
}


void RTAO::CreateTextureResources()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto backbufferFormat = m_deviceResources->GetBackBufferFormat();

    // ToDo change this to non-PS resouce since we use CS?
    D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // ToDo remove obsolete resources, QuarterResAO event triggers this so we may not need all low/gbuffer width AO resources.

    {
        // Preallocate subsequent descriptor indices for both SRV and UAV groups.
        m_AOResources[0].uavDescriptorHeapIndex = m_cbvSrvUavHeap->AllocateDescriptorIndices(AOResource::Count);
        m_AOResources[0].srvDescriptorHeapIndex = m_cbvSrvUavHeap->AllocateDescriptorIndices(AOResource::Count);
        for (UINT i = 0; i < AOResource::Count; i++)
        {
            m_AOResources[i].rwFlags = ResourceRWFlags::AllowWrite | ResourceRWFlags::AllowRead;
            m_AOResources[i].uavDescriptorHeapIndex = m_AOResources[0].uavDescriptorHeapIndex + i;
            m_AOResources[i].srvDescriptorHeapIndex = m_AOResources[0].srvDescriptorHeapIndex + i;
        }

        // ToDo pack some resources.

        // ToDo cleanup raytracing resolution - twice for coefficient.
        CreateRenderTargetResource(device, TextureResourceFormatR::ToDXGIFormat(SceneArgs::RTAO_AmbientCoefficientResourceFormat), m_raytracingWidth, m_raytracingHeight, m_cbvSrvUavHeap.get(), &m_AOResources[AOResource::Coefficient], initialResourceState, L"Render/AO Coefficient");
        
        // ToDo move outside of RTAO
        CreateRenderTargetResource(device, TextureResourceFormatR::ToDXGIFormat(SceneArgs::RTAO_AmbientCoefficientResourceFormat), m_raytracingWidth, m_raytracingHeight, m_cbvSrvUavHeap.get(), &m_AOResources[AOResource::Smoothed], initialResourceState, L"Render/AO Coefficient");

        // ToDo 8 bit hit count?
        CreateRenderTargetResource(device, DXGI_FORMAT_R32_UINT, m_raytracingWidth, m_raytracingHeight, m_cbvSrvUavHeap.get(), &m_AOResources[AOResource::HitCount], initialResourceState, L"Render/AO Hit Count");

        // ToDo use lower bit float?
        CreateRenderTargetResource(device, DXGI_FORMAT_R32_FLOAT, m_raytracingWidth, m_raytracingHeight, m_cbvSrvUavHeap.get(), &m_AOResources[AOResource::FilterWeightSum], initialResourceState, L"Render/AO Filter Weight Sum");
        CreateRenderTargetResource(device, DXGI_FORMAT_R32_FLOAT, m_raytracingWidth, m_raytracingHeight, m_cbvSrvUavHeap.get(), &m_AOResources[AOResource::RayHitDistance], initialResourceState, L"Render/AO Hit Distance");
    }

    
    m_sourceToSortedRayIndexOffset.rwFlags = ResourceRWFlags::AllowWrite | ResourceRWFlags::AllowRead;
    CreateRenderTargetResource(device, DXGI_FORMAT_R8G8_UINT, m_raytracingWidth, m_raytracingHeight, m_cbvSrvUavHeap.get(), &m_sourceToSortedRayIndexOffset, initialResourceState, L"Source To Sorted Ray Index");

    m_sortedToSourceRayIndexOffset.rwFlags = ResourceRWFlags::AllowWrite | ResourceRWFlags::AllowRead;
    CreateRenderTargetResource(device, DXGI_FORMAT_R8G8_UINT, m_raytracingWidth, m_raytracingHeight, m_cbvSrvUavHeap.get(), &m_sortedToSourceRayIndexOffset, initialResourceState, L"Sorted To Source Ray Index");

    m_sortedRayGroupDebug.rwFlags = ResourceRWFlags::AllowWrite | ResourceRWFlags::AllowRead;
    CreateRenderTargetResource(device, DXGI_FORMAT_R32G32B32A32_FLOAT, m_raytracingWidth, m_raytracingHeight, m_cbvSrvUavHeap.get(), &m_sortedRayGroupDebug, initialResourceState, L"Sorted Ray Group Debug");

    m_AORayDirectionOriginDepth.rwFlags = ResourceRWFlags::AllowWrite | ResourceRWFlags::AllowRead;
    // ToDo test precision
    CreateRenderTargetResource(device, DXGI_FORMAT_R11G11B10_FLOAT, m_raytracingWidth, m_raytracingHeight, m_cbvSrvUavHeap.get(), &m_AORayDirectionOriginDepth, initialResourceState, L"AO Rays Direction, Origin Depth and Hit");
}


// Build shader tables.
// This encapsulates all shader records - shaders and the arguments for their local root signatures.
// For AO, the shaders are simple with only one shader type per shader table.
// maxInstanceContributionToHitGroupIndex - since BLAS instances in this sample specify non-zero InstanceContributionToHitGroupIndex, 
//  the sample needs to add as many shader records to all hit group shader tables so that DXR shader addressing lands on a valid shader record for all BLASes.
void RTAO::BuildShaderTables(UINT maxInstanceContributionToHitGroupIndex)
{
    auto device = m_deviceResources->GetD3DDevice();

    void* rayGenShaderIDs[RTAORayGenShaderType::Count];
    void* missShaderID;
    void* hitGroupShaderID;

    // A shader name look-up table for shader table debug print out.
    unordered_map<void*, wstring> shaderIdToStringMap;

    auto GetShaderIDs = [&](auto* stateObjectProperties)
    {
        for (UINT i = 0; i < RTAORayGenShaderType::Count; i++)
        {
            rayGenShaderIDs[i] = stateObjectProperties->GetShaderIdentifier(c_rayGenShaderNames[i]);
            shaderIdToStringMap[rayGenShaderIDs[i]] = c_rayGenShaderNames[i];
        }

        missShaderID = stateObjectProperties->GetShaderIdentifier(c_missShaderName);
        shaderIdToStringMap[missShaderID] = c_missShaderName;

        hitGroupShaderID = stateObjectProperties->GetShaderIdentifier(c_hitGroupName);
        shaderIdToStringMap[hitGroupShaderID] = c_hitGroupName;
    };

    // Get shader identifiers.
    UINT shaderIDSize;
    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    ThrowIfFailed(m_dxrStateObject.As(&stateObjectProperties));
    GetShaderIDs(stateObjectProperties.Get());
    shaderIDSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    // ToDo review
    /*************--------- Shader table layout -------*******************
    | -------------------------------------------------------------------
    | -------------------------------------------------------------------
    |Shader table - RayGenShaderTable: 32 | 32 bytes
    | [0]: MyRaygenShader, 32 + 0 bytes
    | -------------------------------------------------------------------

    | -------------------------------------------------------------------
    |Shader table - MissShaderTable: 32 | 64 bytes
    | [0]: MyMissShader, 32 + 0 bytes
    | [1]: MyMissShader_ShadowRay, 32 + 0 bytes
    | -------------------------------------------------------------------

    | -------------------------------------------------------------------
    |Shader table - HitGroupShaderTable: 96 | 196800 bytes
    | [0]: MyHitGroup_Triangle, 32 + 56 bytes
    | [1]: MyHitGroup_Triangle_ShadowRay, 32 + 56 bytes
    | [2]: MyHitGroup_Triangle, 32 + 56 bytes
    | [3]: MyHitGroup_Triangle_ShadowRay, 32 + 56 bytes
    | ...
    | --------------------------------------------------------------------
    **********************************************************************/

    // RayGen shader tables.
    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIDSize; // No root arguments

        for (UINT i = 0; i < RTAORayGenShaderType::Count; i++)
        {
            ShaderTable rayGenShaderTable(device, numShaderRecords, shaderRecordSize, L"RTAO RayGenShaderTable");
            rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIDs[i], shaderIDSize, nullptr, 0));
            rayGenShaderTable.DebugPrint(shaderIdToStringMap);
            m_rayGenShaderTables[i] = rayGenShaderTable.GetResource();
        }
    }

    // Miss shader table.
    {
        UINT numShaderRecords = 1;
        UINT shaderRecordSize = shaderIDSize; // No root arguments

        ShaderTable missShaderTable(device, numShaderRecords, shaderRecordSize, L"RTAO MissShaderTable");
        missShaderTable.push_back(ShaderRecord(missShaderID, shaderIDSize, nullptr, 0));

        missShaderTable.DebugPrint(shaderIdToStringMap);
        m_missShaderTableStrideInBytes = missShaderTable.GetShaderRecordSize();
        m_missShaderTable = missShaderTable.GetResource();
    }
    
    // Hit group shader table.
    {
        UINT numShaderRecords = maxInstanceContributionToHitGroupIndex + 1;
        UINT shaderRecordSize = shaderIDSize; // No root arguments

        ShaderTable hitGroupShaderTable(device, numShaderRecords, shaderRecordSize, L"RTAO HitGroupShaderTable");

        for (UINT i = 0; i < numShaderRecords; i++)
        {
            hitGroupShaderTable.push_back(ShaderRecord(hitGroupShaderID, shaderIDSize, nullptr, 0));
        }
        hitGroupShaderTable.DebugPrint(shaderIdToStringMap);
        m_hitGroupShaderTableStrideInBytes = hitGroupShaderTable.GetShaderRecordSize();
        m_hitGroupShaderTable = hitGroupShaderTable.GetResource();
    }
}


// ToDo rename
void RTAO::CreateSamplesRNG()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto FrameCount = m_deviceResources->GetBackBufferCount();

    UINT spp = SceneArgs::AOSampleCountPerDimension * SceneArgs::AOSampleCountPerDimension;
    UINT samplesPerSet = spp * SceneArgs::AOSampleSetDistributedAcrossPixels * SceneArgs::AOSampleSetDistributedAcrossPixels;
    UINT NumSampleSets = 83;
    m_randomSampler.Reset(samplesPerSet, NumSampleSets, Samplers::HemisphereDistribution::Cosine);



    // Create shader resources
    {
        // ToDo rename GPU from resource names?
        m_samplesGPUBuffer.Create(device, m_randomSampler.NumSamples() * m_randomSampler.NumSampleSets(), FrameCount, L"GPU buffer: Random unit square samples");
        m_hemisphereSamplesGPUBuffer.Create(device, m_randomSampler.NumSamples() * m_randomSampler.NumSampleSets(), FrameCount, L"GPU buffer: Random hemisphere samples");

        for (UINT i = 0; i < m_randomSampler.NumSamples() * m_randomSampler.NumSampleSets(); i++)
        {
            XMFLOAT3 p = m_randomSampler.GetHemisphereSample3D();
            // Convert [-1,1] to [0,1].
            m_samplesGPUBuffer[i].value = XMFLOAT2(p.x * 0.5f + 0.5f, p.y * 0.5f + 0.5f);
            m_hemisphereSamplesGPUBuffer[i].value = p;
        }
    }
}


// Calculate adaptive per-pixel sampling counts.
// Ref: Bauszat et al. 2011, Adaptive Sampling for Geometry-Aware Reconstruction Filters
// The per-pixel sampling counts are calculated in two steps:
//  - Computing a per-pixel filter weight. This value represents how much of neighborhood contributes
//    to a pixel when filtering. Pixels with small values benefit from filtering only a little and thus will 
//    benefit from increased sampling the most.
//  - Calculating per-pixel sampling count based on the filter weight;
void RTAO::CalculateAdaptiveSamplingCounts()
{
    ThrowIfFalse(false, L"ToDo. Should this be part of AO or result passed in from outside?");
#if 0
    auto commandList = m_deviceResources->GetCommandList();

    RWGpuResource* m_AOResources = SceneArgs::QuarterResAO ? m_AOLowResResources : m_AOResources;
    RWGpuResource* GBufferResources = SceneArgs::QuarterResAO ? m_GBufferLowResResources : m_GBufferResources;
    RWGpuResource& NormalDeptLowPrecisionResource = SceneArgs::QuarterResAO ?
        m_normalDepthLowResLowPrecision[m_normalDepthCurrentFrameResourceIndex]
        : m_normalDepthLowPrecision[m_normalDepthCurrentFrameResourceIndex];

    // Transition the output resource to UAV state.
    {
        D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_AOResources[AOResource::FilterWeightSum].resource.Get(), before, after));
    }
    UINT offsets[5] = { 0, 1, 2, 3, 4 };    // ToDo
    // Calculate filter weight sum for each pixel. 
    {
        ScopedTimer _prof(L"CalculateFilterWeights", commandList);
        m_atrousWaveletTransformFilter.Execute(
            commandList,
            m_cbvSrvUavHeap->GetHeap(),
            GpuKernels::AtrousWaveletTransformCrossBilateralFilter::EdgeStoppingGaussian5x5,
            m_AOResources[AOResource::Coefficient].gpuDescriptorReadAccess,
            NormalDeptLowPrecisionResource.gpuDescriptorReadAccess,
            GBufferResources[GBufferResource::Distance].gpuDescriptorReadAccess,
#if PACK_MEAN_VARIANCE
            m_smoothedMeanVarianceResource.gpuDescriptorReadAccess,
#else
            m_smoothedVarianceResource.gpuDescriptorReadAccess,
#endif
            m_AOResources[AOResource::RayHitDistance].gpuDescriptorReadAccess,
            GBufferResources[GBufferResource::PartialDepthDerivatives].gpuDescriptorReadAccess,
            &m_AOResources[AOResource::FilterWeightSum],
            SceneArgs::AODenoiseValueSigma,
            SceneArgs::AODenoiseDepthSigma,
            SceneArgs::AODenoiseNormalSigma,
            static_cast<TextureResourceFormatRGB::Type>(static_cast<UINT>(SceneArgs::RTAO_TemporalCache_NormalDepthResourceFormat)),
            offsets,
            1,
            GpuKernels::AtrousWaveletTransformCrossBilateralFilter::Mode::OutputPerPixelFilterWeightSum,
            SceneArgs::ReverseFilterOrder,
            SceneArgs::UseSpatialVariance,
            SceneArgs::RTAODenoisingPerspectiveCorrectDepthInterpolation,
            false,
            SceneArgs::RTAO_Denoising_AdaptiveKernelSize_MinHitDistanceScaleFactor,
            SceneArgs::RTAODenoisingFilterMinKernelWidth,
            static_cast<UINT>((SceneArgs::RTAODenoisingFilterMaxKernelWidthPercentage / 100) * m_raytracingWidth),
            SceneArgs::RTAODenoisingFilterVarianceSigmaScaleOnSmallKernels,
            SceneArgs::QuarterResAO);
    }

    // Transition the output to SRV.
    {
        D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_AOResources[AOResource::FilterWeightSum].resource.Get(), before, after));
    }
#endif
}

DXGI_FORMAT RTAO::GetAOCoefficientFormat()
{
    return TextureResourceFormatR::ToDXGIFormat(SceneArgs::RTAO_AmbientCoefficientResourceFormat);
}

void RTAO::DispatchRays(ID3D12Resource* rayGenShaderTable, UINT width, UINT height)
{
    auto commandList = m_deviceResources->GetCommandList();
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();

    ScopedTimer _prof(L"DispatchRays", commandList);

    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
    dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;
    dispatchDesc.HitGroupTable.StrideInBytes = m_hitGroupShaderTableStrideInBytes;
    dispatchDesc.MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;
    dispatchDesc.MissShaderTable.StrideInBytes = m_missShaderTableStrideInBytes;
    dispatchDesc.RayGenerationShaderRecord.StartAddress = rayGenShaderTable->GetGPUVirtualAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = rayGenShaderTable->GetDesc().Width;
    dispatchDesc.Width = width != 0 ? width : m_raytracingWidth;
    dispatchDesc.Height = height != 0 ? height : m_raytracingHeight;
    dispatchDesc.Depth = 1;
    commandList->SetPipelineState1(m_dxrStateObject.Get());

    commandList->DispatchRays(&dispatchDesc);
};


void RTAO::OnUpdate()
{

    if (m_isRecreateAOSamplesRequested)
    {
        m_isRecreateAOSamplesRequested = false;

        m_deviceResources->WaitForGpu();
        CreateSamplesRNG();
    }

    if (m_isRecreateRaytracingResourcesRequested)
    {
        // ToDo what if scenargs change during rendering? race condition??
        // Buffer them - create an intermediate
        m_isRecreateRaytracingResourcesRequested = false;
        m_deviceResources->WaitForGpu();

        // ToDo split to recreate only whats needed?
        CreateResolutionDependentResources();
        CreateAuxilaryDeviceResources();
    }


    uniform_int_distribution<UINT> seedDistribution(0, UINT_MAX);

    if (SceneArgs::RTAORandomFrameSeed)
    {
        m_CB->seed = seedDistribution(m_generatorURNG);
    }
    else
    {
        m_CB->seed = 1879;
    }

    m_CB->numSamplesPerSet = m_randomSampler.NumSamples();
    m_CB->numSampleSets = m_randomSampler.NumSampleSets();
    m_CB->numPixelsPerDimPerSet = SceneArgs::AOSampleSetDistributedAcrossPixels;

    // ToDo move
    m_CB->RTAO_UseAdaptiveSampling = SceneArgs::RTAOAdaptiveSampling;
    m_CB->RTAO_AdaptiveSamplingMaxWeightSum = SceneArgs::RTAOAdaptiveSamplingMaxFilterWeight;
    m_CB->RTAO_AdaptiveSamplingMinMaxSampling = SceneArgs::RTAOAdaptiveSamplingMinMaxSampling;
    m_CB->RTAO_AdaptiveSamplingScaleExponent = SceneArgs::RTAOAdaptiveSamplingScaleExponent;
    m_CB->RTAO_AdaptiveSamplingMinSamples = SceneArgs::RTAOAdaptiveSamplingMinSamples;
    m_CB->RTAO_TraceRayOffsetAlongNormal = SceneArgs::RTAOTraceRayOffsetAlongNormal;
    m_CB->RTAO_TraceRayOffsetAlongRayDirection = SceneArgs::RTAOTraceRayOffsetAlongRayDirection;
    m_CB->RTAO_UseSortedRays = SceneArgs::RTAOUseRaySorting;

    SceneArgs::RTAOAdaptiveSamplingMinSamples.SetMaxValue(SceneArgs::AOSampleCountPerDimension * SceneArgs::AOSampleCountPerDimension);


    // ToDo move
    m_CB->useShadowRayHitTime = SceneArgs::RTAOIsExponentialFalloffEnabled;
    // ToDo standardize RTAO RTAO_ prefix, or remove it since this is RTAO class
    m_CB->RTAO_maxShadowRayHitTime = SceneArgs::RTAOMaxRayHitTime;
    m_CB->RTAO_approximateInterreflections = SceneArgs::RTAOApproximateInterreflections;
    m_CB->RTAO_diffuseReflectanceScale = SceneArgs::RTAODiffuseReflectanceScale;
    m_CB->RTAO_MinimumAmbientIllumination = SceneArgs::RTAO_MinimumAmbientIllumination;
    m_CB->RTAO_IsExponentialFalloffEnabled = SceneArgs::RTAOIsExponentialFalloffEnabled;
    m_CB->RTAO_exponentialFalloffDecayConstant = SceneArgs::RTAO_ExponentialFalloffDecayConstant;

    // Calculate a theoretical max ray distance to be used in occlusion factor computation.
    // Occlusion factor of a ray hit is computed based of its ray hit time, falloff exponent and a max ray hit time.
    // By specifying a min occlusion factor of a ray, we can skip tracing rays that would have an occlusion 
    // factor less than the cutoff to save a bit of performance (generally 1-10% perf win without visible AO result impact). // ToDo retest
    // Therefore we discern between true maxRayHitTime, used in TraceRay, 
    // and a theoretical one used of calculating the occlusion factor on hit.
    {
        float occclusionCutoff = SceneArgs::RTAO_ExponentialFalloffMinOcclusionCutoff;
        float lambda = SceneArgs::RTAO_ExponentialFalloffDecayConstant;

        // Invert occlusionFactor = exp(-lambda * t * t), where t is tHit/tMax of a ray.
        float t = sqrt(logf(occclusionCutoff) / -lambda);

        m_CB->RTAO_maxShadowRayHitTime = t * SceneArgs::RTAOMaxRayHitTime;
        m_CB->RTAO_maxTheoreticalShadowRayHitTime = SceneArgs::RTAOMaxRayHitTime;
    }

}

void RTAO::OnRender(
    D3D12_GPU_VIRTUAL_ADDRESS accelerationStructure,
    D3D12_GPU_DESCRIPTOR_HANDLE rayOriginSurfaceHitPositionResource,
    D3D12_GPU_DESCRIPTOR_HANDLE rayOriginSurfaceNormalDepthResource)
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandList = m_deviceResources->GetCommandList();
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();

    if (SceneArgs::RTAOAdaptiveSampling)
    {
        // ToDo move to within AO
        CalculateAdaptiveSamplingCounts();
    }


    // Copy dynamic buffers to GPU.
    {
        // ToDo copy on change
        m_CB.CopyStagingToGpu(frameIndex);
        m_hemisphereSamplesGPUBuffer.CopyStagingToGpu(frameIndex);
    }

    ScopedTimer _prof(L"CalculateAmbientOcclusion", commandList);

    // Transition AO resources to UAV state.        // ToDo check all comments
    {
        D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_AOResources[AOResource::HitCount].resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::Transition(m_AOResources[AOResource::Coefficient].resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::Transition(m_AOResources[AOResource::RayHitDistance].resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::Transition(m_sourceToSortedRayIndexOffset.resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::Transition(m_sortedToSourceRayIndexOffset.resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::Transition(m_sortedRayGroupDebug.resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::Transition(m_AORayDirectionOriginDepth.resource.Get(), before, after),
        };
        commandList->ResourceBarrier(ARRAYSIZE(barriers), barriers);
    }

    commandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());


    // Bind inputs.
    // ToDo use [enum] instead of [0]
    commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::RayOriginPosition, rayOriginSurfaceHitPositionResource);
    commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::RayOriginSurfaceNormalDepth, rayOriginSurfaceNormalDepthResource);
    commandList->SetComputeRootShaderResourceView(RTAOGlobalRootSignature::Slot::SampleBuffers, m_hemisphereSamplesGPUBuffer.GpuVirtualAddress(frameIndex));
    commandList->SetComputeRootConstantBufferView(RTAOGlobalRootSignature::Slot::ConstantBuffer, m_CB.GpuVirtualAddress(frameIndex));   // ToDo let AO have its own CB.
    commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::FilterWeightSum, m_AOResources[AOResource::FilterWeightSum].gpuDescriptorReadAccess);

    // Bind output RT.
    // ToDo remove output and rename AOout
    commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::AOResourcesOut, m_AOResources[0].gpuDescriptorWriteAccess);
    commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::AORayHitDistance, m_AOResources[AOResource::RayHitDistance].gpuDescriptorWriteAccess);
    commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::AORayDirectionOriginDepthHitUAV, m_AORayDirectionOriginDepth.gpuDescriptorWriteAccess);

    // Bind the heaps, acceleration structure and dispatch rays. 
    commandList->SetComputeRootShaderResourceView(RTAOGlobalRootSignature::Slot::AccelerationStructure, accelerationStructure);

    DispatchRays(m_rayGenShaderTables[RTAORayGenShaderType::AOFullRes].Get());

    // ToDo Remove
    //DispatchRays(m_rayGenShaderTables[SceneArgs::QuarterResAO ? RTAORayGenShaderType::AOQuarterRes : RTAORayGenShaderType::AOFullRes].Get(),
    //    &m_gpuTimers[GpuTimers::Raytracing_AO], m_raytracingWidth, m_raytracingHeight);

    // Transition AO resources to shader resource state.
    {
        D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_AORayDirectionOriginDepth.resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::UAV(m_AORayDirectionOriginDepth.resource.Get()),  // ToDo
        };
        commandList->ResourceBarrier(ARRAYSIZE(barriers), barriers);
    }

    if (SceneArgs::RTAOUseRaySorting)
    {
        float rayBinDepthSize = SceneArgs::RTAORayBinDepthSizeMultiplier * SceneArgs::RTAOMaxRayHitTime;
        m_raySorter.Execute(
            commandList,
            rayBinDepthSize,
            m_raytracingWidth,
            m_raytracingHeight,
            GpuKernels::SortRays::FilterType::CountingSort,
            //GpuKernels::SortRays::FilterType::BitonicSort,
            SceneArgs::RTAORaySortingUseOctahedralRayDirectionQuantization,
            m_cbvSrvUavHeap->GetHeap(),
            m_AORayDirectionOriginDepth.gpuDescriptorReadAccess,
            m_sortedToSourceRayIndexOffset.gpuDescriptorWriteAccess,
            m_sourceToSortedRayIndexOffset.gpuDescriptorWriteAccess,
            m_sortedRayGroupDebug.gpuDescriptorWriteAccess);

        // Transition the output to SRV state. 
        {
            D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            D3D12_RESOURCE_BARRIER barriers[] = {
                CD3DX12_RESOURCE_BARRIER::Transition(m_sourceToSortedRayIndexOffset.resource.Get(), before, after),
                CD3DX12_RESOURCE_BARRIER::Transition(m_sortedToSourceRayIndexOffset.resource.Get(), before, after),
                CD3DX12_RESOURCE_BARRIER::Transition(m_sortedRayGroupDebug.resource.Get(), before, after),
                CD3DX12_RESOURCE_BARRIER::UAV(m_sourceToSortedRayIndexOffset.resource.Get()),
                CD3DX12_RESOURCE_BARRIER::UAV(m_sortedToSourceRayIndexOffset.resource.Get())
            };
            commandList->ResourceBarrier(ARRAYSIZE(barriers), barriers);
        }

        {
            ScopedTimer _prof(L"[Sorted]CalculateAmbientOcclusion", commandList);

            commandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());


            // Bind inputs.
            // ToDo use [enum] instead of [0]
            commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::RayOriginPosition, rayOriginSurfaceHitPositionResource);
            commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::RayOriginSurfaceNormalDepth, rayOriginSurfaceNormalDepthResource);
            commandList->SetComputeRootShaderResourceView(RTAOGlobalRootSignature::Slot::SampleBuffers, m_hemisphereSamplesGPUBuffer.GpuVirtualAddress(frameIndex));
            commandList->SetComputeRootConstantBufferView(RTAOGlobalRootSignature::Slot::ConstantBuffer, m_CB.GpuVirtualAddress(frameIndex));   // ToDo let AO have its own CB.
            commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::FilterWeightSum, m_AOResources[AOResource::FilterWeightSum].gpuDescriptorReadAccess);

            // ToDo remove
            commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::AORayDirectionOriginDepthHitSRV, m_AORayDirectionOriginDepth.gpuDescriptorReadAccess);
            commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::AOSortedToSourceRayIndex, m_sortedToSourceRayIndexOffset.gpuDescriptorReadAccess);

            // Bind output RT.
            // ToDo remove output and rename AOout
            commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::AOResourcesOut, m_AOResources[0].gpuDescriptorWriteAccess);
            commandList->SetComputeRootDescriptorTable(RTAOGlobalRootSignature::Slot::AORayHitDistance, m_AOResources[AOResource::RayHitDistance].gpuDescriptorWriteAccess);

            // Bind the heaps, acceleration structure and dispatch rays. 
            // ToDo dedupe calls
            commandList->SetComputeRootShaderResourceView(RTAOGlobalRootSignature::Slot::AccelerationStructure, accelerationStructure);

#if RTAO_RAY_SORT_1DRAYTRACE
            DispatchRays(m_rayGenShaderTables[RTAORayGenShaderType::AOSortedRays].Get(), m_raytracingWidth * m_raytracingHeight, 1);
#else
            DispatchRays(m_rayGenShaderTables[RTAORayGenShaderType::AOSortedRays].Get(), m_raytracingWidth, m_raytracingHeight);
#endif
        }
    }

    // Transition AO resources to shader resource state.
    {
        D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_AOResources[AOResource::HitCount].resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::Transition(m_AOResources[AOResource::Coefficient].resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::Transition(m_AOResources[AOResource::RayHitDistance].resource.Get(), before, after),
            CD3DX12_RESOURCE_BARRIER::UAV(m_AOResources[AOResource::Coefficient].resource.Get()),  // ToDo
            CD3DX12_RESOURCE_BARRIER::UAV(m_AOResources[AOResource::RayHitDistance].resource.Get()),  // ToDo
        };
        commandList->ResourceBarrier(ARRAYSIZE(barriers), barriers);
    }

    // Calculate AO ray hit count.
    if (m_calculateRayHitCounts)
    {
        ScopedTimer _prof(L"CalculateAORayHitCount", commandList);
        CalculateRayHitCount();
    }

    PIXEndEvent(commandList);
}

void RTAO::CalculateRayHitCount()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto frameIndex = m_deviceResources->GetCurrentFrameIndex();
    auto commandList = m_deviceResources->GetCommandList();
    
    m_reduceSumKernel.Execute(
        commandList,
        m_cbvSrvUavHeap->GetHeap(),
        frameIndex,
        m_AOResources[AOResource::HitCount].gpuDescriptorReadAccess,
        &m_numAORayGeometryHits);
};

void RTAO::CreateResolutionDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();
    auto commandQueue = m_deviceResources->GetCommandQueue();
    auto FrameCount = m_deviceResources->GetBackBufferCount();

    CreateTextureResources();
    m_reduceSumKernel.CreateInputResourceSizeDependentResources(
        device,
        m_cbvSrvUavHeap.get(),
        FrameCount,
        m_raytracingWidth,
        m_raytracingHeight);
}


void RTAO::SetResolution(UINT width, UINT height)
{
    m_raytracingWidth = width;
    m_raytracingHeight = height;

    CreateResolutionDependentResources();
}
