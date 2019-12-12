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
    Camera s_Camera;

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
        Matrix4 mProjectView;
        Vector4 mDiffuseColor;
        Vector4 mMu;
        float mEpsilon;
        float mZoom;

        int mWidth;
        int mHeight;
        int mRenderSoftShadows;
       
    } gUniformData;

    std::unique_ptr<CameraController> s_CameraController;


    float gZoom = 1;
    int   gRenderSoftShadows = 1;
    float gEpsilon = 0.003f;
    float gColorT = 0.0f;
    float gColorA[4] = { 0.25f, 0.45f, 1.0f, 1.0f };
    float gColorB[4] = { 0.25f, 0.45f, 1.0f, 1.0f };
    float gColorC[4] = { 0.25f, 0.45f, 1.0f, 1.0f };
    float gMuT = 0.0f;
    float gMuA[4] = { -.278f, -.479f, 0.0f, 0.0f };
    float gMuB[4] = { 0.278f, 0.479f, 0.0f, 0.0f };
    float gMuC[4] = { -.278f, -.479f, -.231f, .235f };

};


CREATE_APPLICATION(Compute)

void Compute::Startup(void)
{
    initNoise();
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
    s_Camera.SetEyeAtUp(eye, Vector3(kZero), Vector3(kYUnitVector));
    s_Camera.SetZRange(1.0f, 10000.0f);
    s_CameraController.reset(new CameraController(s_Camera, Vector3(kYUnitVector)));
    s_PixelBuffer.Create(L"pixel buffer", g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight(), 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void Compute::Cleanup(void)
{




}

void Compute::Update(float deltaT)
{
    UpdateMu(deltaT, &gMuT, gMuA, gMuB);
    Interpolate(gMuC, gMuT, gMuA, gMuB);
    UpdateColor(deltaT, &gColorT, gColorA, gColorB);
    Interpolate(gColorC, gColorT, gColorA, gColorB);
    s_CameraController->Update(deltaT);
}

uint32_t ThreadGroupSize[2] = { 16,16 };
void Compute::RenderScene(void)
{

    const Matrix4& camViewProjMat =s_Camera.GetViewProjMatrix();
    gUniformData.mProjectView = camViewProjMat;
    gUniformData.mDiffuseColor = Vector4(gColorC[0], gColorC[1], gColorC[2], gColorC[3]);
    gUniformData.mRenderSoftShadows = gRenderSoftShadows;
    gUniformData.mEpsilon = gEpsilon;
    gUniformData.mZoom = gZoom;
    gUniformData.mMu = Vector4(gMuC[0], gMuC[1], gMuC[2], gMuC[3]);
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

    D3D12_VIEWPORT m_MainViewport;
    D3D12_RECT m_MainScissor;

    m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
    m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
    m_MainViewport.MinDepth = 0.0f;
    m_MainViewport.MaxDepth = 1.0f;
    m_MainViewport.TopLeftX = 0.0f;
    m_MainViewport.TopLeftY = 0.0f;

    m_MainScissor.left = 0;
    m_MainScissor.top = 0;
    m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
    m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

    gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
    gfxContext.Draw(3);


   
        
    gfxContext.Finish();


}