#pragma region 
#include "pch.h"
#include "SkyPass.hpp"
#include "GraphicsCore.h"
#include "CommandContext.h"
#include "BufferManager.h"
#include "Camera.h"
#include "CompiledShaders/SkyVS.h"
#include "CompiledShaders/SkyPS.h"
#pragma endregion

using namespace Graphics;
using namespace Math;

namespace
{
    RootSignature s_RootSignature;
    GraphicsPSO s_skypassPSO;
    enum RootParams
    {
        VSCameraCBuffer,
        SkyTextureSRVs,
        NumPassRootParams,
    };
}




namespace SkyPass
{
    void Initialize(void)
    {
        s_RootSignature.Reset(NumPassRootParams, 2);
        s_RootSignature.InitStaticSampler(0, SamplerLinearClampDesc);
        s_RootSignature.InitStaticSampler(1, SamplerLinearBorderDesc);
        s_RootSignature[VSCameraCBuffer].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
        s_RootSignature[SkyTextureSRVs].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        s_RootSignature.Finalize(L"SkyPass");

        const DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
        const DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();

        s_skypassPSO.SetRootSignature(s_RootSignature);
        s_skypassPSO.SetRasterizerState(RasterizerDefault);
        s_skypassPSO.SetBlendState(BlendDisable);
        s_skypassPSO.SetDepthStencilState(DepthStateReadWrite);
        s_skypassPSO.SetInputLayout(0, nullptr);
        s_skypassPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        s_skypassPSO.SetRenderTargetFormat(ColorFormat, DepthFormat);
        s_skypassPSO.SetVertexShader(SHADER_ARGS(g_pSkyVS));
        s_skypassPSO.SetPixelShader(SHADER_ARGS(g_pSkyPS));
        s_skypassPSO.Finalize();
    }

    void Shutdown(void)
    {

    }

    void Render(GraphicsContext& Context, const Math::Camera& cam)
    {
        ScopedTimer _prof(L"skypass", Context);
        __declspec(align(16))struct VSConstants
        {
            Matrix4 modelToProjection;
            Matrix4 modelToShadow;
            XMFLOAT3 viewerPos;
        } vsConstants;
        const Matrix4& camViewProjMat = cam.GetViewProjMatrix();
        vsConstants.modelToProjection = camViewProjMat;
        vsConstants.modelToShadow = camViewProjMat;
        XMStoreFloat3(&vsConstants.viewerPos, cam.GetPosition());

        Context.SetRootSignature(s_RootSignature);
        Context.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context.SetPipelineState(s_skypassPSO);
        Context.SetDynamicConstantBufferView(RootParams::VSCameraCBuffer, sizeof(vsConstants), &vsConstants);
        const auto& sky_srv = cam.GetSky().GetTexture()->GetSRV();
        const D3D12_CPU_DESCRIPTOR_HANDLE Handles[] = { sky_srv };
        Context.SetDynamicDescriptors(RootParams::SkyTextureSRVs, 0, 1, Handles);
        Context.SetRenderTarget(g_SceneColorBuffer.GetRTV(), g_SceneDepthBuffer.GetDSV_DepthReadOnly());
        Context.Draw(240u);

    }
}
