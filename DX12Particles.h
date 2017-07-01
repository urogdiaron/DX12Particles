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

#pragma once

#include "DXSample.h"
#include "StepTimer.h"
#include "SimpleCamera.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
// An example of this can be found in the class method: OnDestroy().
using Microsoft::WRL::ComPtr;

class DX12Particles : public DXSample
{
public:
	DX12Particles(UINT width, UINT height, std::wstring name);

	virtual void OnInit();
	virtual void OnUpdate();
    void RunComputeShader(int readableBufferIndex, int writableBufferIndex);
    void WaitForFence(bool waitOnCpu);
    void RenderParticles(int readableBufferIndex);
    virtual void OnRender();
	virtual void OnDestroy();
	virtual void OnKeyDown(UINT8 key);
	virtual void OnKeyUp(UINT8 key);

private:
	struct ParticleVertex
	{
		XMFLOAT4 color;
	};

	struct ParticleData
	{
		XMFLOAT4 pos;
	};

	static const int ParticleCount = 100000;
	static const int FrameCount = 2;

	// Pipeline objects.
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];

	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_cbvSrvHeap;

	//Rendering
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature >m_rootSignature;	
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;

	//Compute
	ComPtr<ID3D12RootSignature >m_rootSignatureCompute;
	ComPtr<ID3D12CommandAllocator> m_commandAllocatorCompute;
	ComPtr<ID3D12CommandQueue> m_commandQueueCompute;
	ComPtr<ID3D12PipelineState> m_pipelineStateCompute;
	ComPtr<ID3D12GraphicsCommandList> m_commandListCompute;

	// App resources.
	// This is the buffer that can be read
	int m_currentParticleBufferIndex = 0;
	ComPtr<ID3D12Resource> m_vertexBuffer;
	ComPtr<ID3D12Resource> m_vertexBufferUpload;
	ComPtr<ID3D12Resource> m_particleBuffers[FrameCount];
	ComPtr<ID3D12Resource> m_particleBufferUpload;
	ComPtr<ID3D12Resource> m_constantBufferGS;
    ComPtr<ID3D12Resource> m_constantBufferPerFrame[FrameCount];
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	StepTimer m_timer;
	UINT m_cbvSrvDescriptorSize;
	UINT m_rtvDescriptorSize;
	SimpleCamera m_camera;

	// Synchronization objects.
	UINT m_frameIndex;
	UINT m_frameCounter;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;
    bool m_computeFirst = false;

    void LoadPipeline();
    void CreateVertexBuffer();
    void CreateParticleBuffers();
    void LoadAssets();
};
