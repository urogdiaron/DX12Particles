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

#define DEBUG_PARTICLE_DATA

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

    static const UINT ParticleBufferSize = 50;
    static const int FrameCount = 2;

	virtual void OnInit();
	virtual void OnUpdate();
    void RunComputeShader(int readableBufferIndex, int writableBufferIndex);
    void WaitForFence(bool waitOnCpu, bool bCompute);
    void RenderParticles(int readableBufferIndex);
    virtual void OnRender();
	virtual void OnDestroy();
	virtual void OnKeyDown(UINT8 key);
	virtual void OnKeyUp(UINT8 key);

    void LoadPipeline();
    void CreateParticleBuffers(ID3D12Resource* pParticleUpdateBuffer);
    void LoadAssets();

	struct ParticleVertex
	{
		XMFLOAT4 color;
	};

	struct ParticleData
	{
		XMFLOAT3 pos;
        float fTimeLeft;
        XMFLOAT4 velocity;
        XMFLOAT4 color;
	};

    enum class DescOffset
    {
        StaticConstantBuffer,
        ParticleSRV0,
        ParticleSRV1,
        ParticleUAV0,
        ParticleUAV1,
        PerFrameConstantBuffer0,
        PerFrameConstantBuffer1,
        DeadListUAV,
        OffsetCounterUAV,
        OffsetPerTilesUAV,
        ParticleIndicesForTilesUAV,
        Count
    };

    struct DeadListBufferData
    {
        UINT m_nParticleCount;
        UINT m_availableIndices[ParticleBufferSize];
    };

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
    ComPtr<ID3D12PipelineState> m_pipelineStateDebug;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;

    //Compute
    enum class ComputePass
    {
        Generate,
        Move,
        Destroy,
        Count
    };

	ComPtr<ID3D12RootSignature >m_rootSignatureCompute;
	ComPtr<ID3D12CommandAllocator> m_commandAllocatorCompute;
	ComPtr<ID3D12CommandQueue> m_commandQueueCompute;
    ComPtr<ID3D12PipelineState> m_computePipelineStates[(int)ComputePass::Count] = {};
	ComPtr<ID3D12GraphicsCommandList> m_commandListCompute;

    ComPtr<ID3D12RootSignature> m_tileRootSignature;
    ComPtr<ID3D12PipelineState> m_tilePipelineState;
    ComPtr<ID3D12Resource> m_tileOffsets;
    ComPtr<ID3D12Resource> m_ParticleIndicesForTiles;

	// App resources.
	ComPtr<ID3D12Resource> m_particleBuffers[FrameCount];
    ComPtr<ID3D12Resource> m_deadListBuffer;
	ComPtr<ID3D12Resource> m_constantBufferGS;
    
    ComPtr<ID3D12Resource> m_constantBufferPerFrame[FrameCount];
    UINT* m_constantBufferPerFrameData[FrameCount];     //We constantly have the buffer mapped since it's in the upload heap.

	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    UINT m_currentParticleBufferIndex = 0;
    UINT m_cbvSrvDescriptorSize;
	UINT m_rtvDescriptorSize;


	// Synchronization objects.
	UINT m_frameIndex;
	UINT m_frameCounter;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

    // Setting variables
    bool m_computeFirst = false;
    bool m_waitForComputeOnGPU = false;

    // Runtime variables
    bool m_bPaused = false;
    bool m_bDebug = false;

    StepTimer m_timer;
    UINT m_nEmitCountNextFrame = 0;
    SimpleCamera m_camera;
    std::mt19937 m_randomNumberEngine;

    // Debug variables
#ifdef DEBUG_PARTICLE_DATA
    std::unique_ptr<DeadListBufferData> m_LastFrameDeadListBufferData;
    ComPtr<ID3D12Resource> m_deadListReadback;
#endif
};
