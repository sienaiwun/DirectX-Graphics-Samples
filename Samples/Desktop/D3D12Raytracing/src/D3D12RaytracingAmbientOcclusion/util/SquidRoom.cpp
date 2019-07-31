﻿//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "../stdafx.h"
#include "DXSampleHelper.h"
#include "..\RaytracingSceneDefines.h"
#include "SquidRoom.h"

using namespace SquidRoomAssets;
using namespace std;


// Load geometry from a file into buffers.
void SquidRoomAssets::LoadGeometry(
	ID3D12Device5* device,
	ID3D12GraphicsCommandList4* commandList,
	DX::DescriptorHeap* descriptorHeap,
	LPCWSTR assetPath,
	D3DGeometry* geometry,
    std::vector<D3DTexture>* textures,
    std::vector<PrimitiveMaterialBuffer>* materials,
	vector<GeometryInstance>* geometryInstances)
{
	UINT fileSize = 0;
	UINT8* pAssetData = nullptr;

	// Convert handedness by flipping z coordinate.
	auto ConvertRHtoLHGeometry = [&]()
	{
		VertexPositionNormalTextureTangent* vertices = reinterpret_cast<VertexPositionNormalTextureTangent*>(pAssetData + SquidRoomAssets::VertexDataOffset);
		UINT nVertices = SquidRoomAssets::VertexDataSize / sizeof(VertexPositionNormalTextureTangent);

		for (UINT i = 0; i < nVertices; i++)
		{
			auto& vertex = vertices[i];
			vertex.position.z = -vertex.position.z;
		}
	};

	auto DeactivateGeometry = [&](UINT geometryID)
	{
		auto& geometryDesc = SquidRoomAssets::Draws[geometryID];
		VertexPositionNormalTextureTangent* geometryVertices = reinterpret_cast<VertexPositionNormalTextureTangent*>(pAssetData + SquidRoomAssets::VertexDataOffset) + geometryDesc.VertexBase;
		UINT* geometryIndices = reinterpret_cast<UINT*>(pAssetData + SquidRoomAssets::IndexDataOffset) + geometryDesc.IndexStart;

		// Deactivate vertices by setting x-coordinate to NaN.
		for (UINT i = 0; i < geometryDesc.IndexCount; i++)
		{
			auto& vertex = geometryVertices[geometryIndices[i]];
			vertex.position.x = nanf("");
		}
	};

	// Load scene assets.
	ThrowIfFailed(ReadDataFromFile(assetPath, &pAssetData, &fileSize));
	ConvertRHtoLHGeometry();

	// Create the index buffer.
	{
		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(SquidRoomAssets::IndexDataSize),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&geometry->ib.buffer.resource)));
		geometry->ib.buffer.resource->SetName(L"Index buffer: Squid room");

		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(SquidRoomAssets::IndexDataSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&geometry->ib.upload)));
		geometry->ib.upload->SetName(L"Index buffer upload: Squid room");

		// Copy data to the upload heap and then schedule a copy 
		// from the upload heap to the index buffer.
		D3D12_SUBRESOURCE_DATA indexData = {};
		indexData.pData = pAssetData + SquidRoomAssets::IndexDataOffset;
		indexData.RowPitch = SquidRoomAssets::IndexDataSize;
		indexData.SlicePitch = indexData.RowPitch;

		PIXBeginEvent(commandList, 0, L"Copy index buffer data to default resource...");

		UpdateSubresources<1>(commandList, geometry->ib.buffer.resource.Get(), geometry->ib.upload.Get(), 0, 0, 1, &indexData);
#if VBIB_AS_NON_PIXEL_SHADER_RESOURCE
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(geometry->ib.buffer.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
#else
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(geometry->ib.buffer.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));
#endif
		PIXEndEvent(commandList);
#if INDEX_FORMAT_UINT
		UINT elementStride = sizeof(Index);
		UINT numElements = SquidRoomAssets::IndexDataSize / sizeof(elementStride);
#else
		// Using raw typeless buffer. 
		// - numElements - number of 32bit dwords.
		// - elementSize - 0.
		UINT numElements = SquidRoomAssets::IndexDataSize / sizeof(UINT);
		UINT elementStride = 0;
#endif
		CreateBufferSRV(device, numElements, elementStride, descriptorHeap, &geometry->ib.buffer);
	}

	// Create the vertex buffer.
	{
		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(SquidRoomAssets::VertexDataSize),
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&geometry->vb.buffer.resource)));
		geometry->vb.buffer.resource->SetName( L"VertexPositionNormalTextureTangent buffer: Squid room");

		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(SquidRoomAssets::VertexDataSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&geometry->vb.upload)));
		geometry->vb.upload->SetName(L"VertexPositionNormalTextureTangent buffer upload: Squid room");

		// Copy data to the upload heap and then schedule a copy 
		// from the upload heap to the vertex buffer.
		D3D12_SUBRESOURCE_DATA vertexData = {};
		vertexData.pData = pAssetData + SquidRoomAssets::VertexDataOffset;
		vertexData.RowPitch = SquidRoomAssets::VertexDataSize;
		vertexData.SlicePitch = vertexData.RowPitch;

		// Disable side panels on the squid container to make the squid visible.
		{
			const UINT sidePanelsGeometryID = 848;
			DeactivateGeometry(sidePanelsGeometryID);
		}

		PIXBeginEvent(commandList, 0, L"Copy vertex buffer data to default resource...");

		UpdateSubresources<1>(commandList, geometry->vb.buffer.resource.Get(), geometry->vb.upload.Get(), 0, 0, 1, &vertexData);
#if VBIB_AS_NON_PIXEL_SHADER_RESOURCE
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(geometry->vb.buffer.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
#else
		commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(geometry->vb.buffer.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
#endif
		PIXEndEvent(commandList);

		UINT numElements = SquidRoomAssets::VertexDataSize / SquidRoomAssets::StandardVertexStride;
		UINT elementStride = SquidRoomAssets::StandardVertexStride;	
		CreateBufferSRV(device, numElements, elementStride, descriptorHeap, &geometry->vb.buffer);
	}
	ThrowIfFalse(geometry->vb.buffer.heapIndex == geometry->ib.buffer.heapIndex + 1, L"Vertex Buffer descriptor index must follow that of Index Buffer descriptor index");

    // Create shader resources.
    {
        // Create each texture and SRV descriptor.
        const UINT srvCount = _countof(SquidRoomAssets::Textures);
        textures->resize(srvCount);

        PIXBeginEvent(commandList, 0, L"Copy diffuse and normal texture data to default resources...");
        for (UINT i = 0; i < srvCount; i++)
        {
            // Describe and create a Texture2D.
            const SquidRoomAssets::TextureResource &tex = SquidRoomAssets::Textures[i];
            CD3DX12_RESOURCE_DESC texDesc(
                D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                0,
                tex.Width,
                tex.Height,
                1,
                static_cast<UINT16>(tex.MipLevels),
                tex.Format,
                1,
                0,
                D3D12_TEXTURE_LAYOUT_UNKNOWN,
                D3D12_RESOURCE_FLAG_NONE);

            ThrowIfFailed(device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&(*textures)[i].resource)));
            SetNameIndexed((*textures)[i].resource.Get(), L"Squid room scene texture ", i);

            {
                const UINT subresourceCount = texDesc.DepthOrArraySize * texDesc.MipLevels;
                UINT64 uploadBufferSize = GetRequiredIntermediateSize((*textures)[i].resource.Get(), 0, subresourceCount);
                ThrowIfFailed(device->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                    D3D12_HEAP_FLAG_NONE,
                    &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(&(*textures)[i].upload)));

                // Copy data to the intermediate upload heap and then schedule a copy
                // from the upload heap to the Texture2D.
                D3D12_SUBRESOURCE_DATA textureData = {};
                textureData.pData = pAssetData + tex.Data->Offset;
                textureData.RowPitch = tex.Data->Pitch;
                textureData.SlicePitch = tex.Data->Size;

                UpdateSubresources(commandList, (*textures)[i].resource.Get(), (*textures)[i].upload.Get(), 0, 0, subresourceCount, &textureData);
                commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition((*textures)[i].resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
            }

            CreateTextureSRV(device, (*textures)[i].resource.Get(), descriptorHeap, &(*textures)[i].heapIndex, &(*textures)[i].cpuDescriptorHandle, &(*textures)[i].gpuDescriptorHandle);
        }
        PIXEndEvent(commandList);
    }

    // Create a shared material.
    PrimitiveMaterialBuffer materialCB;
    materialCB.Kd = XMFLOAT3(1, 1, 1);
    materialCB.Ks = XMFLOAT3(1, 1, 1);
    materialCB.opacity = XMFLOAT3(1, 1, 1);
    materialCB.hasDiffuseTexture = true;
    materialCB.hasNormalTexture = true;
    materialCB.hasPerVertexTangents = true;
    materialCB.roughness = 0.1f;
    materialCB.type = MaterialType::Default;

    UINT size = sizeof(PrimitiveMaterialBuffer);
    UINT materialID = static_cast<UINT>(materials->size());
    materials->push_back(materialCB);

	// Generate geometry instances.
	geometryInstances->resize(ARRAYSIZE(SquidRoomAssets::Draws));
	for (UINT i = 0; i < ARRAYSIZE(SquidRoomAssets::Draws); i++)
	{
        auto& instance = (*geometryInstances)[i];
		auto& ib = instance.ib;
		auto& vb = instance.vb;
		instance.materialID = materialID;
        instance.diffuseTexture = (*textures)[SquidRoomAssets::Draws[i].DiffuseTextureIndex].gpuDescriptorHandle;
        instance.normalTexture = (*textures)[SquidRoomAssets::Draws[i].NormalTextureIndex].gpuDescriptorHandle;

		ib.startIndex = SquidRoomAssets::Draws[i].IndexStart;
		ib.count = SquidRoomAssets::Draws[i].IndexCount;
		ib.indexBuffer = geometry->ib.buffer.resource->GetGPUVirtualAddress() + ib.startIndex * SquidRoomAssets::StandardIndexStride;

		vb.startIndex = SquidRoomAssets::Draws[i].VertexBase;
		vb.count = SquidRoomAssets::VertexDataSize / SquidRoomAssets::StandardVertexStride - SquidRoomAssets::Draws[i].VertexBase;
		vb.vertexBuffer.StrideInBytes = SquidRoomAssets::StandardVertexStride;
		vb.vertexBuffer.StartAddress = geometry->vb.buffer.resource->GetGPUVirtualAddress() + vb.startIndex * SquidRoomAssets::StandardVertexStride;

		D3D12_CPU_DESCRIPTOR_HANDLE dummyCpuHandle;
		UINT ibHeapIndex = UINT_MAX;
		UINT vbHeapIndex = UINT_MAX;
        // ToDo why create SRVs here again?
		CreateBufferSRV(geometry->ib.buffer.resource.Get(), device, ib.count, SquidRoomAssets::StandardIndexStride, descriptorHeap, &dummyCpuHandle, &ib.gpuDescriptorHandle, &ibHeapIndex, ib.startIndex);
		CreateBufferSRV(geometry->vb.buffer.resource.Get(), device, vb.count, SquidRoomAssets::StandardVertexStride, descriptorHeap, &dummyCpuHandle, &vb.gpuDescriptorHandle, &vbHeapIndex, vb.startIndex);
		ThrowIfFalse(vbHeapIndex == ibHeapIndex + 1, L"Vertex Buffer descriptor index must follow that of Index Buffer descriptor index");
	}

	free(pAssetData);
}
