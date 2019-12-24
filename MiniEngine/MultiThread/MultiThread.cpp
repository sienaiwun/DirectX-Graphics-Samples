#pragma region Header
#include "MultiThread.hpp"
#include "CompiledShaders/drawQuadVS.h"
#include "CompiledShaders/drawQuadPS.h"
#include "CompiledShaders/fillCS.h"
#pragma endregion




struct BufferElement
{
    U32x2 StartIndex;
    F32x3 color;
};

struct PerThreadData
{
    ColorBuffer quadBuffer;
    Color quadColor;
    size_t index;
    U32x2 StartIndex;
    BufferElement GetGpuElement()
    {
        BufferElement element;
        element.StartIndex = StartIndex,
        element.color = {quadColor.R(), quadColor.G(),quadColor.B()};
        return element;
    }
};

namespace {
    RootSignature  s_ComputeSig;
    RootSignature s_GraphicsSig;
    GraphicsPSO s_graphicsPSO;
    ComputePSO s_ComputePSO;
    uint32_t s_width, s_height;
    D3D12_VIEWPORT s_MainViewport;
    D3D12_RECT s_MainScissor;
    U32x2 s_tileNum;
    U32x3 s_ThreadGroupSize = { 8,8,8 };
    std::vector<PerThreadData> s_workLoads;
    std::vector<BufferElement> s_bufferElements;
    StructuredBuffer s_mappedBuffer;
    ColorBuffer s_PixelBuffer;
    
    enum ComputeRootParams :unsigned char
    {
        UniformBufferParam,
        UAVParam,
        StructBufferParam,
        NumComputeParams,
    };

    enum GraphicRootParams :unsigned char
    {
        InputTextureSRV,
        NumGraphicsParams,
    };

    enum SLOT :unsigned char
    {
        COMPUTE_BUFFER_SLOT,
        UAV_SLOT,
        NumSlotParams,
    };

    __declspec(align(16))struct WorldBufferConstants
    {
        F32x3 c_color;
        float tap;
        U32x2 tile_num;
    } gUniformData;
};


CREATE_APPLICATION(MultiThread)

void MultiThread::Startup(void)
{

    s_PixelBuffer.Create(L"temp pixel buffer", g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight(), 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
    {
        s_ComputeSig.Reset(ComputeRootParams::NumComputeParams, 0);
        s_ComputeSig[ComputeRootParams::UniformBufferParam].InitAsConstantBuffer(SLOT::COMPUTE_BUFFER_SLOT, D3D12_SHADER_VISIBILITY_ALL);
        s_ComputeSig[ComputeRootParams::UAVParam].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UAV_SLOT, 1);
        s_ComputeSig[ComputeRootParams::StructBufferParam].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
        s_ComputeSig.Finalize(L"Compute");
        s_ComputePSO.SetRootSignature(s_ComputeSig);
        s_ComputePSO.SetComputeShader(SHADER_ARGS(g_pfillCS));
        s_ComputePSO.Finalize();
    }
    {
        SamplerDesc DefaultSamplerDesc;
        DefaultSamplerDesc.MaxAnisotropy = 8;
        s_GraphicsSig.Reset(GraphicRootParams::NumGraphicsParams, 1);
        s_GraphicsSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        s_GraphicsSig[GraphicRootParams::InputTextureSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        s_GraphicsSig.Finalize(L"Graphic");


        DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();

        s_graphicsPSO.SetRootSignature(s_GraphicsSig);
        D3D12_RASTERIZER_DESC resater = RasterizerTwoSided;
        resater.DepthClipEnable = FALSE;
        s_graphicsPSO.SetRasterizerState(resater);
        s_graphicsPSO.SetBlendState(BlendDisable);
        s_graphicsPSO.SetDepthStencilState(DepthStateReadWrite);
        s_graphicsPSO.SetInputLayout(0, nullptr);
        s_graphicsPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        s_graphicsPSO.SetRenderTargetFormats(1, &ColorFormat, DXGI_FORMAT_UNKNOWN);
        s_graphicsPSO.SetVertexShader(SHADER_ARGS(g_pdrawQuadVS));
        s_graphicsPSO.SetPixelShader(SHADER_ARGS(g_pdrawQuadPS));
        s_graphicsPSO.Finalize();
        ID3D12ShaderReflection* d3d12reflection = NULL;
        D3DReflect(SHADER_ARGS(g_pfillCS), IID_PPV_ARGS(&d3d12reflection));
        D3D12_SHADER_DESC shaderDesc;
        d3d12reflection->GetDesc(&shaderDesc);
        d3d12reflection->GetThreadGroupSize(
            &s_ThreadGroupSize[0], &s_ThreadGroupSize[1], &s_ThreadGroupSize[2]);

    }
    {
        s_width = g_SceneColorBuffer.GetWidth();
        s_height = g_SceneColorBuffer.GetHeight();
        s_tileNum = { Math::DivideByMultiple(s_width,s_ThreadGroupSize[0]), Math::DivideByMultiple(s_height,s_ThreadGroupSize[1]) };
        const uint32_t jobs_num = s_tileNum.product();
        s_workLoads = std::vector<PerThreadData>(jobs_num);
        s_bufferElements = std::vector<BufferElement>(jobs_num);
        for (size_t n = 0; n < s_workLoads.size(); n++)
        {
            s_workLoads[n].index = n;
            s_workLoads[n].StartIndex = { (uint32_t)n % s_tileNum[0], (uint32_t)n / s_tileNum[0] };
            s_workLoads[n].quadBuffer.Create(L"Quad Buffer" + n, s_ThreadGroupSize[0], s_ThreadGroupSize[1], 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
            RandomColor((float*)&s_workLoads[n].quadColor);
            s_bufferElements[n] = s_workLoads[n].GetGpuElement();
        }
        s_mappedBuffer.Create(L"mapped buffer", jobs_num, sizeof(BufferElement), s_bufferElements.data());

    }


}

void MultiThread::Cleanup(void)
{
}

void MultiThread::Update(float )
{
}

void MultiThread::RenderScene(void)
{

    gUniformData.c_color = { 0, 1, 0 };
    gUniformData.tile_num = s_tileNum;


    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Compute");
    ComputeContext& computeContext = gfxContext.GetComputeContext();
    computeContext.SetRootSignature(s_ComputeSig);
    computeContext.SetPipelineState(s_ComputePSO);


    computeContext.TransitionResource(s_mappedBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    computeContext.TransitionResource(s_PixelBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    computeContext.SetDynamicDescriptor(ComputeRootParams::UAVParam, 0, s_PixelBuffer.GetUAV());
    computeContext.SetDynamicDescriptor(ComputeRootParams::StructBufferParam, 0, s_mappedBuffer.GetSRV());
    computeContext.SetDynamicConstantBufferView(ComputeRootParams::UniformBufferParam, sizeof(gUniformData), &gUniformData);
    computeContext.Dispatch2D(s_PixelBuffer.GetWidth(), s_PixelBuffer.GetHeight(), s_ThreadGroupSize[0], s_ThreadGroupSize[1]);




    gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.SetRootSignature(s_GraphicsSig);
    gfxContext.SetPipelineState(s_graphicsPSO);
    gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    gfxContext.ClearColor(g_SceneColorBuffer);
    gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV());

    gfxContext.TransitionResource(s_PixelBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
    gfxContext.SetDynamicDescriptor(GraphicRootParams::InputTextureSRV, 0, s_PixelBuffer.GetSRV());


    s_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
    s_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
    s_MainViewport.MinDepth = 0.0f;
    s_MainViewport.MaxDepth = 1.0f;
    s_MainViewport.TopLeftX = 0.0f;
    s_MainViewport.TopLeftY = 0.0f;

    s_MainScissor.left = 0;
    s_MainScissor.top = 0;
    s_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
    s_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

    gfxContext.SetViewportAndScissor(s_MainViewport, s_MainScissor);
    gfxContext.Draw(4); // draw quad
    gfxContext.Finish();


}