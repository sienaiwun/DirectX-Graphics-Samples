#pragma region Header
#include "Compute.hpp"
#include "CompiledShaders/displayVS.h"
#include "CompiledShaders/displayPS.h"
#include "CompiledShaders/compute.h"
#pragma endregion


namespace {
    RootSignature  s_ComputeSig;
    RootSignature s_GraphicsSig;
    GraphicsPSO s_GraphicsPSO;
    ComputePSO s_computePSO;

    D3D12_VIEWPORT s_MainViewport;
    D3D12_RECT s_MainScissor;
    ColorBuffer s_PixelBuffer(Color(1.0f, 1.0f, 1.0f));

    enum ComputeRootParams :unsigned char
    {
        UniformBufferParam,
        UAVParam,
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
        
        int mWidth;
        int mHeight;
        int mRenderSoftShadows;
        int tap;
        float mEpsilon;
        float time;
    } gUniformData;



    int   gRenderSoftShadows = 1;
    float gEpsilon = 0.003f;


};


CREATE_APPLICATION(Compute)

void Compute::Startup(void)
{
    {
        s_ComputeSig.Reset(ComputeRootParams::NumComputeParams, 0);
        s_ComputeSig[ComputeRootParams::UniformBufferParam].InitAsConstantBuffer(SLOT::COMPUTE_BUFFER_SLOT, D3D12_SHADER_VISIBILITY_ALL);
        s_ComputeSig[ComputeRootParams::UAVParam].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UAV_SLOT, 1);
        s_ComputeSig.Finalize(L"Compute");
        s_computePSO.SetRootSignature(s_ComputeSig);
        s_computePSO.SetComputeShader(SHADER_ARGS(g_pcompute));
        s_computePSO.Finalize();
    }
    {
        SamplerDesc DefaultSamplerDesc;
        DefaultSamplerDesc.MaxAnisotropy = 8;
        s_GraphicsSig.Reset(GraphicRootParams::NumGraphicsParams, 1);
        s_GraphicsSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        s_GraphicsSig[GraphicRootParams::InputTextureSRV].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        s_GraphicsSig.Finalize(L"Graphic");


        DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();

        s_GraphicsPSO.SetRootSignature(s_GraphicsSig);
        D3D12_RASTERIZER_DESC resater = RasterizerTwoSided;
        resater.DepthClipEnable = FALSE;
        s_GraphicsPSO.SetRasterizerState(resater);
        s_GraphicsPSO.SetBlendState(BlendDisable);
        s_GraphicsPSO.SetDepthStencilState(DepthStateReadWrite);
        s_GraphicsPSO.SetInputLayout(0,nullptr);
        s_GraphicsPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        s_GraphicsPSO.SetRenderTargetFormats(1, &ColorFormat, DXGI_FORMAT_UNKNOWN);
        s_GraphicsPSO.SetVertexShader(SHADER_ARGS(g_pdisplayVS));
        s_GraphicsPSO.SetPixelShader(SHADER_ARGS(g_pdisplayPS));
        s_GraphicsPSO.Finalize();
       
    }
   

  
    const Vector3 eye = Vector3(.5f, 0.0f, 0.0f);
    s_PixelBuffer.Create(L"pixel buffer", g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight(), 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void Compute::Cleanup(void)
{




}

void Compute::Update(float)
{
}

uint32_t ThreadGroupSize[2] = { 16,16 };
void Compute::RenderScene(void)
{

    gUniformData.mRenderSoftShadows = gRenderSoftShadows;
    gUniformData.mEpsilon = gEpsilon;
    gUniformData.mWidth = g_SceneColorBuffer.GetWidth();
    gUniformData.mHeight = g_SceneColorBuffer.GetHeight();
    

   
    GraphicsContext& gfxContext = GraphicsContext::Begin(L"Compute");
    ComputeContext& computeContext = gfxContext.GetComputeContext();
    computeContext.SetRootSignature(s_ComputeSig);
    computeContext.SetPipelineState(s_computePSO);
   
    computeContext.TransitionResource(s_PixelBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    computeContext.SetDynamicDescriptor(ComputeRootParams::UAVParam, 0, s_PixelBuffer.GetUAV());
    computeContext.SetDynamicConstantBufferView(ComputeRootParams::UniformBufferParam, sizeof(gUniformData), &gUniformData);
    computeContext.Dispatch2D(s_PixelBuffer.GetWidth(), s_PixelBuffer.GetHeight(),16, 16);
    
    


    gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    gfxContext.SetRootSignature(s_GraphicsSig);
    gfxContext.SetPipelineState(s_GraphicsPSO);
    gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
    gfxContext.Draw(3);


   
        
    gfxContext.Finish();


}