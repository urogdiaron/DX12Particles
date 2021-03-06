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
#include <sstream>
#include <string>
#include <initguid.h>
#include <dxgidebug.h>
#include "DX12Particles.h"
#include "TileConstants.h"

#define InterlockedGetValue(object) InterlockedCompareExchange(object, 0, 0)

DX12Particles::DX12Particles(UINT width, UINT height, std::wstring name) :
    DXSample(width, height, name),
    m_frameIndex(0),
    m_viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)),
    m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
    m_frameCounter(0),
    m_fenceValue(0),
    m_rtvDescriptorSize(0)
{
    std::random_device r;
    m_randomNumberEngine.seed(std::seed_seq{ r(), r(), r(), r(), r() });
}

DX12Particles::~DX12Particles()
{
	/*ComPtr<IDXGIDebug1> dxgiDebug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
	{
		dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL));
	}*/
}

void DX12Particles::OnInit()
{
    m_camera.Init({ 8, 8, 30 });

    LoadPipeline();
    LoadAssets();
}

// Load the rendering pipeline dependencies.
void DX12Particles::LoadPipeline()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
    	ComPtr<ID3D12Debug> debugController;
    	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    	{
    		debugController->EnableDebugLayer();
    		
    		// Enable additional debug layers.
    		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    	}
    }
#endif
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    if (m_useWarpDevice)
    {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_12_1,
            IID_PPV_ARGS(&m_device)
        ));
    }
    else
    {
        ComPtr<IDXGIAdapter1> hardwareAdapter;
        GetHardwareAdapter(factory.Get(), &hardwareAdapter);

        ThrowIfFailed(D3D12CreateDevice(
            hardwareAdapter.Get(),
            D3D_FEATURE_LEVEL_12_1,
            IID_PPV_ARGS(&m_device)
        ));
    }

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
    NAME_D3D12_OBJECT(m_commandQueue);

    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueueCompute)));
    NAME_D3D12_OBJECT(m_commandQueueCompute);

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),		// Swap chain needs the queue so that it can force a flush on it.
        Win32Application::GetHwnd(),
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ));

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(), DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        // Describe and create a shader resource view (SRV) and constant 
        // buffer view (CBV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
        cbvSrvHeapDesc.NumDescriptors = (int)DescOffset::Count;
        cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&cbvSrvHeapDesc, IID_PPV_ARGS(&m_cbvSrvHeap)));
        NAME_D3D12_OBJECT(m_cbvSrvHeap);

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        m_cbvSrvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_commandAllocatorCompute)));

    NAME_D3D12_OBJECT(m_commandAllocator);
    NAME_D3D12_OBJECT(m_commandAllocatorCompute);

    ThrowIfFailed(m_commandQueueCompute->GetTimestampFrequency(&m_nComputeTimestampFreq));
}

std::vector<float> particleData[(int)DX12Particles::ParticleBufferTypes::Count];

UINT DX12Particles::CreateParticleBuffers(ParticleBuffers& UploadBuffers)
{
    std::array<UINT, (int)ParticleBufferTypes::Count> particleBufferSizes;
    std::array<std::wstring, (int)ParticleBufferTypes::Count> particleBufferNames;

    particleBufferSizes[(int)ParticleBufferTypes::Position] = sizeof(XMFLOAT2);
    particleBufferSizes[(int)ParticleBufferTypes::Scale] = sizeof(XMFLOAT2);
    particleBufferSizes[(int)ParticleBufferTypes::Velocity] = sizeof(XMFLOAT2);
    particleBufferSizes[(int)ParticleBufferTypes::Rotation] = sizeof(float);
    particleBufferSizes[(int)ParticleBufferTypes::Lifetime] = sizeof(float);
    particleBufferSizes[(int)ParticleBufferTypes::Color] = sizeof(XMFLOAT4);

    particleBufferNames[(int)ParticleBufferTypes::Position] = (L"Position");
    particleBufferNames[(int)ParticleBufferTypes::Scale] = (L"Scale");
    particleBufferNames[(int)ParticleBufferTypes::Velocity] = (L"Velocity");
    particleBufferNames[(int)ParticleBufferTypes::Rotation] = (L"Rotation");
    particleBufferNames[(int)ParticleBufferTypes::Lifetime] = (L"Lifetime");
    particleBufferNames[(int)ParticleBufferTypes::Color] = (L"Color");

	auto fnGetRandomNumber = [&]() -> float
	{
		auto dist = std::uniform_real_distribution<float>{};
		float ret = dist(m_randomNumberEngine);
		return ret;
	};

	auto fnGetRandomFloatInRange = [&](float from, float to) -> float
	{
		float fRnd = fnGetRandomNumber();
		return fRnd * (to - from) + from;
	};

    UINT PrecreatedParticleCount = 0;
    for(int iBuffer = 0; iBuffer < (int)ParticleBufferTypes::Count; iBuffer++)
    {
        particleData[iBuffer].resize(ParticleBufferSize * particleBufferSizes[iBuffer]);
    }

    UINT nParticlesPerRow = (UINT)ceil(sqrt((float)ParticleBufferSize));
    XMFLOAT4 colors[] = { XMFLOAT4(1, 0, 0, 1), XMFLOAT4(0, 1, 0, 1), XMFLOAT4(0, 0, 1, 1), XMFLOAT4(1, 0, 1, 1), XMFLOAT4(1, 1, 0, 1), XMFLOAT4(0, 1, 1, 1) };

    XMFLOAT2* particlePositions = reinterpret_cast<XMFLOAT2*>(particleData[(int)ParticleBufferTypes::Position].data());
    XMFLOAT2* particleScales = reinterpret_cast<XMFLOAT2*>(particleData[(int)ParticleBufferTypes::Scale].data());
    XMFLOAT2* particleVelocities = reinterpret_cast<XMFLOAT2*>(particleData[(int)ParticleBufferTypes::Velocity].data());
    float* particleRotations = particleData[(int)ParticleBufferTypes::Rotation].data();
    float* particleLifetimes = particleData[(int)ParticleBufferTypes::Lifetime].data();
    XMFLOAT4* particleColors = reinterpret_cast<XMFLOAT4*>(particleData[(int)ParticleBufferTypes::Color].data());

    for(UINT i = 0; i < ParticleBufferSize; i++)
    {
        particleLifetimes[i] = 9999999.0f;

        float posX = (float)(i % nParticlesPerRow) / nParticlesPerRow;
        posX = -1.0f + posX * 2 + 0.2f;

        float posY = (float)(i / nParticlesPerRow) / nParticlesPerRow;
        posY = 1.0f - posY * 2 - 0.2f;

        particlePositions[i].x = posX;
        particlePositions[i].y = posY;
        particleVelocities[i].x = 0.01f;
		particleScales[i] = XMFLOAT2(fnGetRandomFloatInRange(0.01f, 0.06f), fnGetRandomFloatInRange(0.01f, 0.06f));
#ifdef DISABLE_ROTATION
        particleRotations[i] = 0.0f;
#else
        particleRotations[i] = fnGetRandomFloatInRange(-XM_PI, XM_PI);
#endif

        particleColors[i] = colors[i % (sizeof(colors) / sizeof(XMFLOAT4))];

        PrecreatedParticleCount++;
    }

    // One extra loop for the upload buffers
    for (int i = 0; i < FrameCount + 1; i++)
    {
        ParticleBuffers* currentParticleBuffers;
        if(i < FrameCount)
        {
            currentParticleBuffers = &m_particleBuffers[i];
        }
        else
        {
            currentParticleBuffers = &UploadBuffers;
        }

        D3D12_RESOURCE_STATES startingState;
        D3D12_RESOURCE_FLAGS resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
        if (i == 0)
        {
            startingState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        else if (i == FrameCount)
        {
            startingState = D3D12_RESOURCE_STATE_GENERIC_READ;
            heapType = D3D12_HEAP_TYPE_UPLOAD;
            resourceFlags = D3D12_RESOURCE_FLAG_NONE;
        }
        else
        {
            startingState = D3D12_RESOURCE_STATE_COPY_DEST;
        } 

        for (int iBuffer = 0; iBuffer < (int)ParticleBufferTypes::Count; iBuffer++)
        {
            auto&& buffer = currentParticleBuffers->Buffers[iBuffer];
            auto bufferSize = particleBufferSizes[iBuffer];
            auto& bufferName = particleBufferNames[iBuffer];
            ThrowIfFailed(m_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(heapType),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(bufferSize * ParticleBufferSize, resourceFlags),
                startingState,
                nullptr,
                IID_PPV_ARGS(&buffer)
            ));
            SetNameIndexed(buffer.Get(), bufferName.c_str(), i);
        }
    }
    for (int iBuffer = 0; iBuffer < (int)ParticleBufferTypes::Count; iBuffer++)
    {
        auto&& buffer = m_particleBuffers[1].Buffers[iBuffer];
        auto bufferSize = particleBufferSizes[iBuffer];

        D3D12_SUBRESOURCE_DATA particleSubresourceData;
        particleSubresourceData.pData = reinterpret_cast<void*>(particleData[iBuffer].data());
        particleSubresourceData.SlicePitch = bufferSize * ParticleBufferSize;
        particleSubresourceData.RowPitch = particleSubresourceData.SlicePitch;
        UpdateSubresources<1>(m_commandList.Get(), buffer.Get(), UploadBuffers.Buffers[iBuffer].Get(), 0, 0, 1, &particleSubresourceData);
    }
    
    // @TODO Set the deadlist's counter to the initialized particles
    for (int i = 0; i < FrameCount; i++)
    {
        int nEnumOffsetPerFrame = (int)DescOffset::ParticlePositionSRV1 - (int)DescOffset::ParticlePositionSRV0;
        int nEnumOffset = nEnumOffsetPerFrame * i;

        // Create a shader resource view and unordered access view for every buffer
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = ParticleBufferSize;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = ParticleBufferSize;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        for (int iBuffer = 0; iBuffer < (int)ParticleBufferTypes::Count; iBuffer++)
        {
            auto&& buffer = m_particleBuffers[i].Buffers[iBuffer];
            auto bufferSize = particleBufferSizes[iBuffer];

            srvDesc.Buffer.StructureByteStride = bufferSize;
            uavDesc.Buffer.StructureByteStride = bufferSize;

            int descriptorOffset = (int)DescOffset::ParticlePositionSRV0 + nEnumOffset + iBuffer;
            CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), descriptorOffset, m_cbvSrvDescriptorSize);
            m_device->CreateShaderResourceView(buffer.Get(), &srvDesc, srvHandle);

            CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticlePositionUAV0 + nEnumOffset + iBuffer, m_cbvSrvDescriptorSize);
            m_device->CreateUnorderedAccessView(buffer.Get(), nullptr, &uavDesc, uavHandle);
        }
    }

    return PrecreatedParticleCount;
}

// Load the sample assets.
void DX12Particles::LoadAssets()
{
    // Note: ComPtr's are CPU objects but these resources need to stay in scope until
    // the command list that references them has finished executing on the GPU.
    // We will flush the GPU at the end of this method to ensure the resources are not
    // prematurely destroyed.

    UINT particleBufferCount = 6;

    // Create the root signature.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        static const int maxRangeCount = 5;
        UINT nRangeCount = 0;
        std::array<CD3DX12_DESCRIPTOR_RANGE1, maxRangeCount> ranges;
        ranges[nRangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[nRangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
        ranges[nRangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 10, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
        ranges[nRangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, particleBufferCount, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
        ranges[nRangeCount++].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, particleBufferCount, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

        nRangeCount = 0;
        std::array<CD3DX12_ROOT_PARAMETER1, maxRangeCount> rootParameters;
        for(auto& range : ranges)
        {
            rootParameters[nRangeCount++].InitAsDescriptorTable(1, &range, D3D12_SHADER_VISIBILITY_ALL);
        }

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(nRangeCount, rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;

        D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
        if (error)
        {
            OutputDebugStringA((char*)error->GetBufferPointer());
        }
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
        NAME_D3D12_OBJECT(m_rootSignature);
    }

    // Create the pipeline state, which includes loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> geometryShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> computeShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ComPtr<ID3DBlob> errorBlob = nullptr;
        // @Incomplete: Precompile the shaders! See how to set up compile flags that way.
        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleDraw.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSParticleDraw", "vs_5_0", compileFlags, 0, &vertexShader, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }

        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleDraw.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "GSParticleDraw", "gs_5_0", compileFlags, 0, &geometryShader, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }

        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleDraw.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSParticleDraw", "ps_5_0", compileFlags, 0, &pixelShader, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }


        CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        // Additive blend state
        CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
        /*blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;*/

        // Describe and create the graphics pipeline state objects (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { nullptr, 0 };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.GS = CD3DX12_SHADER_BYTECODE(geometryShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = blendDesc;
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
        NAME_D3D12_OBJECT(m_pipelineState);
    }

#ifdef TILED_STUFF_CAN_HAPPEN
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> geometryShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ComPtr<ID3DBlob> errorBlob = nullptr;
        // @Incomplete: Precompile the shaders! See how to set up compile flags that way.
        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleDebug.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSParticleDraw", "vs_5_0", compileFlags, 0, &vertexShader, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }

        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleDebug.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "GSParticleDraw", "gs_5_0", compileFlags, 0, &geometryShader, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }

        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleDebug.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSParticleDraw", "ps_5_0", compileFlags, 0, &pixelShader, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }


        CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        // Additive blend state

        // Describe and create the graphics pipeline state objects (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { nullptr, 0 };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.GS = CD3DX12_SHADER_BYTECODE(geometryShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = blendDesc;
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateDebug)));
        NAME_D3D12_OBJECT(m_pipelineStateDebug);
    }
#endif

    //Create compute PSO
    {
        ComPtr<ID3DBlob> computeShaders[(int)ComputePass::Count];
        const char* shaderFunctions[(int)ComputePass::Count] = {};
        shaderFunctions[(int)ComputePass::Generate] = "CSGenerate";
        shaderFunctions[(int)ComputePass::Move] = "CSUpdate";
        shaderFunctions[(int)ComputePass::Destroy] = "CSDestroy";
#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        for (int i = 0; i < (int)ComputePass::Count; i++)
        {
            if(!shaderFunctions[i])
            {
                continue;
            }
            ComPtr<ID3DBlob> errorBlob = nullptr;
            // @Incomplete: Precompile the shaders! See how to set up compile flags that way.
            if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleCompute.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, shaderFunctions[i], "cs_5_0", compileFlags, 0, &computeShaders[i], &errorBlob))
            {
                if (errorBlob)
                {
                    OutputDebugStringA((char*)errorBlob->GetBufferPointer());
                }
            }
            
            CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
            depthStencilDesc.DepthEnable = FALSE;
            depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

            // Describe and create the graphics pipeline state objects (PSO).
            D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
            computePsoDesc.pRootSignature = m_rootSignature.Get();
            computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(computeShaders[i].Get());

            ThrowIfFailed(m_device->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&m_computePipelineStates[i])));
            NAME_D3D12_OBJECT(m_computePipelineStates[i]);
        }
    }

    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
    NAME_D3D12_OBJECT(m_commandList);

    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_commandAllocatorCompute.Get(), nullptr, IID_PPV_ARGS(&m_commandListCompute)));
    NAME_D3D12_OBJECT(m_commandListCompute);
    m_commandListCompute->Close();

    // Create render target views (RTVs).
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < FrameCount; i++)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])));
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, m_rtvDescriptorSize);

        NAME_D3D12_OBJECT_INDEXED(m_renderTargets, i);
    }

    // Create a two particle buffers. 
    // The initial values go to second one because that's what we read in the update phase.
    // This is because we swap the buffers after the emit phase.

    // Pass in upload buffers from the outer scope so they don't get destroyed before we wait for the commands to complete (end of this function).
    ParticleBuffers UploadBuffers;
    UINT InitialParticleCount = CreateParticleBuffers(UploadBuffers);

    // Create a static constant buffer for the geometry shader
    ComPtr<ID3D12Resource> constantBufferUpload;
    {
        struct ConstantBufferData
        {
            float m_AspectRatio;
            UINT m_ParticleCount;
            UINT m_ResolutionX;
            UINT m_ResolutionY;
        };

        const UINT bufferSize = 256;

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_constantBufferGS)
        ));


        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&constantBufferUpload)
        ));

        NAME_D3D12_OBJECT(m_constantBufferGS);
        
        ConstantBufferData DataToUpload;
        DataToUpload.m_AspectRatio = (float)m_width / m_height;
        DataToUpload.m_ParticleCount = ParticleBufferSize;
        DataToUpload.m_ResolutionX = m_width;
        DataToUpload.m_ResolutionY = m_height;

        D3D12_SUBRESOURCE_DATA constantBufferSubresourceData;
        constantBufferSubresourceData.pData = reinterpret_cast<void*>(&DataToUpload);
        constantBufferSubresourceData.SlicePitch = sizeof(ConstantBufferData);
        constantBufferSubresourceData.RowPitch = sizeof(ConstantBufferData);
        UpdateSubresources<1>(m_commandList.Get(), m_constantBufferGS.Get(), constantBufferUpload.Get(), 0, 0, 1, &constantBufferSubresourceData);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_constantBufferGS.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

        // Create a constant buffer view
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBufferGS->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = bufferSize;
        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), (int)DescOffset::StaticConstantBuffer, m_cbvSrvDescriptorSize);
        m_device->CreateConstantBufferView(&cbvDesc, cbvHandle);
    }

    ComPtr<ID3D12Resource> deadListBufferUpload;
    {
        UINT64 deadListBufferSize = sizeof(UINT) * (ParticleBufferSize + 1);
        // Create the dead list append buffer
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(deadListBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_deadListBuffer)
        ));
        NAME_D3D12_OBJECT(m_deadListBuffer);

#ifdef DEBUG_PARTICLE_DATA
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(deadListBufferSize, D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_deadListReadback)
        ));
        NAME_D3D12_OBJECT(m_deadListReadback);
#endif

        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(deadListBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&deadListBufferUpload)
        ));

        auto pDataToUpload = std::make_unique<DeadListBufferData>();
        for (int i = 0; i < ParticleBufferSize; i++)
        {
            pDataToUpload->m_availableIndices[i] = i;
        }
        pDataToUpload->m_nParticleCount = InitialParticleCount;

        D3D12_SUBRESOURCE_DATA deadListBufferData;
        deadListBufferData.pData = reinterpret_cast<void*>(pDataToUpload.get());
        deadListBufferData.SlicePitch = sizeof(DeadListBufferData);
        deadListBufferData.RowPitch = sizeof(DeadListBufferData);
        UpdateSubresources<1>(m_commandList.Get(), m_deadListBuffer.Get(), deadListBufferUpload.Get(), 0, 0, 1, &deadListBufferData);
        m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_deadListBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));


        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = ParticleBufferSize + 1;
        uavDesc.Buffer.StructureByteStride = sizeof(UINT);
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        uavDesc.Buffer.CounterOffsetInBytes = 0;

        CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), (int)DescOffset::DeadListUAV, m_cbvSrvDescriptorSize);
        m_device->CreateUnorderedAccessView(m_deadListBuffer.Get(), m_deadListBuffer.Get(), &uavDesc, uavHandle);
    }

    
    // @Note: This could be done in a single buffer using multiple views with different offsets
    for(int i = 0; i < FrameCount; i++)
    {
        UINT bufferSize = 256;
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_constantBufferPerFrame[i])
        ));
        NAME_D3D12_OBJECT_INDEXED(m_constantBufferPerFrame, i);

        CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_constantBufferPerFrame[i]->Map(0, &readRange, reinterpret_cast<void**>(&m_constantBufferPerFrameData[i])));
        ZeroMemory(m_constantBufferPerFrameData[i], bufferSize);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_constantBufferPerFrame[i]->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = bufferSize;
        CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), (int)DescOffset::PerFrameConstantBuffer0 + i, m_cbvSrvDescriptorSize);
        m_device->CreateConstantBufferView(&cbvDesc, cbvHandle);
    }

#ifdef TILED_STUFF_CAN_HAPPEN
    {
        //Create Root signature for the tile gathering process
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        CD3DX12_DESCRIPTOR_RANGE1 ranges[6];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, particleBufferCount, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);    // For the readable particle data
        ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 10, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 11, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 12, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
        ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 13, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

        CD3DX12_ROOT_PARAMETER1 rootParameters[6];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[3].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[4].InitAsDescriptorTable(1, &ranges[4], D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[5].InitAsDescriptorTable(1, &ranges[5], D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;

        D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
        if (error)
        {
            OutputDebugStringA((char*)error->GetBufferPointer());
        }
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_tileRootSignature)));
        NAME_D3D12_OBJECT(m_tileRootSignature);
    }

    {
        // Create pipeline state objects for the tile gathering process
        ComPtr<ID3DBlob> tileShader;
        ComPtr<ID3DBlob> tileRasterizerShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ComPtr<ID3DBlob> errorBlob = nullptr;
        // @Incomplete: Precompile the shaders! See how to set up compile flags that way.
        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleTile.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "CSCollectParticles", "cs_5_0", compileFlags, 0, &tileShader, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }

        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleTile.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "CSRasterizeParticles", "cs_5_0", compileFlags, 0, &tileRasterizerShader, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }
        
        // Describe and create the graphics pipeline state objects (PSO).
        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_tileRootSignature.Get();
        psoDesc.CS = CD3DX12_SHADER_BYTECODE(tileShader.Get());

        ThrowIfFailed(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_tilePipelineStates[(int)TileComputePass::GatherParticles])));
        NAME_D3D12_OBJECT(m_tilePipelineStates[(int)TileComputePass::GatherParticles]);

        psoDesc.CS = CD3DX12_SHADER_BYTECODE(tileRasterizerShader.Get());

        ThrowIfFailed(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_tilePipelineStates[(int)TileComputePass::RasterizeParticles])));
        NAME_D3D12_OBJECT(m_tilePipelineStates[(int)TileComputePass::RasterizeParticles]);


        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"ParticleTile.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "CSResetTileOffsetCounter", "cs_5_0", compileFlags, 0, &tileShader, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }

        psoDesc.CS = CD3DX12_SHADER_BYTECODE(tileShader.Get());
        ThrowIfFailed(m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_tilePipelineStates[(int)TileComputePass::ResetCounter])));
        NAME_D3D12_OBJECT(m_tilePipelineStates[(int)TileComputePass::ResetCounter]);
    }

    {
        // Offset per tile Resource

        UINT tileCountX = (UINT)((float)m_width / TILE_SIZE_IN_PIXELS + 0.5f);
        UINT tileCountY = (UINT)((float)m_height/ TILE_SIZE_IN_PIXELS + 0.5f);

        // Create the resources for the tile process as well as the UAVs
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32_UINT, tileCountX, tileCountY, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_tileOffsets)
        ));
        NAME_D3D12_OBJECT(m_tileOffsets);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = DXGI_FORMAT_R32G32_UINT;
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandleOffsets(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), (int)DescOffset::OffsetPerTilesUAV, m_cbvSrvDescriptorSize);
        m_device->CreateUnorderedAccessView(m_tileOffsets.Get(), nullptr, &uavDesc, cpuHandleOffsets);
    }

    {
        // Particle index buffer for tiles

        UINT tileCountX = (UINT)((float)m_width / TILE_SIZE_IN_PIXELS + 0.5f);
        UINT tileCountY = (UINT)((float)m_height / TILE_SIZE_IN_PIXELS + 0.5f);
        UINT tileOffsetBufferSize = (tileCountX * tileCountY * MAX_PARTICLE_PER_TILE + 1) * sizeof(UINT);

        // Create the resources for the tile process as well as the UAVs
        ThrowIfFailed(m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(tileOffsetBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_ParticleIndicesForTiles)
        ));
        NAME_D3D12_OBJECT(m_ParticleIndicesForTiles);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.NumElements = tileCountX * tileCountY * MAX_PARTICLE_PER_TILE;
        uavDesc.Buffer.FirstElement = 1;
        uavDesc.Buffer.StructureByteStride = sizeof(UINT);

        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticleIndicesForTilesUAV, m_cbvSrvDescriptorSize);
        m_device->CreateUnorderedAccessView(m_ParticleIndicesForTiles.Get(), nullptr, &uavDesc, cpuHandle);

        uavDesc.Buffer.NumElements = 1;
        uavDesc.Buffer.FirstElement = 0;
        CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandleCounter(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), (int)DescOffset::OffsetCounterUAV, m_cbvSrvDescriptorSize);
        m_device->CreateUnorderedAccessView(m_ParticleIndicesForTiles.Get(), nullptr, &uavDesc, cpuHandleCounter);
    }

    {
        // Setup tiled debug rendering
        m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_TileDebugRenderTarget)
        );
        NAME_D3D12_OBJECT(m_TileDebugRenderTarget);

        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

            CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), (int)DescOffset::TileRenderDebugUAV, m_cbvSrvDescriptorSize);
            m_device->CreateUnorderedAccessView(m_TileDebugRenderTarget.Get(), nullptr, &uavDesc, cpuHandle);
        }

        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;

            CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(m_cbvSrvHeap->GetCPUDescriptorHandleForHeapStart(), (int)DescOffset::TileRenderDebugSRV, m_cbvSrvDescriptorSize);
            m_device->CreateShaderResourceView(m_TileDebugRenderTarget.Get(), &srvDesc, cpuHandle);
        }
    }
    

    ///////////////////////////////////////////////////////////////////////////////
    {
        //Create Root signature for debug texture render
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

        CD3DX12_ROOT_PARAMETER1 rootParameters[1];
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_STATIC_SAMPLER_DESC samplerDescs[1];
        samplerDescs[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, _countof(samplerDescs), samplerDescs);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;

        D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
        if (error)
        {
            OutputDebugStringA((char*)error->GetBufferPointer());
        }
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_debugRenderRootSignature)));
        NAME_D3D12_OBJECT(m_debugRenderRootSignature);
    }

    {
        // Create pipeline state objects for the tile gathering process
        ComPtr<ID3DBlob> debugRenderShaderVS;
        ComPtr<ID3DBlob> debugRenderShaderGS;
        ComPtr<ID3DBlob> debugRenderShaderPS;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ComPtr<ID3DBlob> errorBlob = nullptr;

        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"TextureRender.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSTextureRender", "vs_5_0", compileFlags, 0, &debugRenderShaderVS, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }

        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"TextureRender.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "GSTextureRender", "gs_5_0", compileFlags, 0, &debugRenderShaderGS, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }

        // @Incomplete: Precompile the shaders! See how to set up compile flags that way.
        if FAILED(D3DCompileFromFile(GetAssetFullPath(L"TextureRender.hlsl").c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSTextureRender", "ps_5_0", compileFlags, 0, &debugRenderShaderPS, &errorBlob))
        {
            if (errorBlob)
            {
                OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            }
        }

        CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        // Alpha blend blend state
        CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        // Describe and create the graphics pipeline state objects (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_debugRenderRootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(debugRenderShaderVS.Get());
        psoDesc.GS = CD3DX12_SHADER_BYTECODE(debugRenderShaderGS.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(debugRenderShaderPS.Get());
        psoDesc.InputLayout = { nullptr, 0 };
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = blendDesc;
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_debugRenderPipelineState)));
        NAME_D3D12_OBJECT(m_debugRenderPipelineState);
    }

    // Create stuff for profiling
    {
        // The heap contains a begin-end timestamp pair for each statistic we want to measure.
        // This is the memory layout: Frame1Stat1Begin, Frame1Stat1End, Frame1Stat2Begin, Frame1Stat2End, ...

        D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
        queryHeapDesc.Count = (int)FramePerformanceStatistics::FramePerfomanceStatisticCount * FrameCount * 2;
        queryHeapDesc.NodeMask = 0;
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        m_device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_TimingQueryHeap));
        NAME_D3D12_OBJECT(m_TimingQueryHeap);

        int resultSize = sizeof(UINT64) * FrameCount * 2;

        m_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(queryHeapDesc.Count * sizeof(UINT64)),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_TimingQueryResult));

        NAME_D3D12_OBJECT(m_TimingQueryResult);
    }
#endif // TILED_STUFF_CAN_HAPPEN

    // Close the command list and execute it to begin the initial GPU setup.
    ThrowIfFailed(m_commandList->Close());
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue++;

        // Create an event handle to use for frame synchronization.
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute; we are reusing the same command 
        // list in our main loop but for now, we just want to wait for setup to 
        // complete before continuing.

        // Signal and increment the fence value.
        const UINT64 fenceToWaitFor = m_fenceValue;
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceToWaitFor));
        m_fenceValue++;

        // Wait until the fence is completed.
        ThrowIfFailed(m_fence->SetEventOnCompletion(fenceToWaitFor, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

// Update frame-based values.
void DX12Particles::OnUpdate()
{
    UINT64 elapsedTimeUs[(int)FramePerformanceStatistics::FramePerfomanceStatisticCount] = {};

#if 0
    {
        // Collect statistics info
        const UINT oldestFrameIndex = (m_frameIndex + 1) % FrameCount;
        D3D12_RANGE readRange = {};
        const D3D12_RANGE emptyRange = {};

        UINT queryCountPerFrame = (int)FramePerformanceStatistics::FramePerfomanceStatisticCount * 2;

        readRange.Begin = oldestFrameIndex * queryCountPerFrame * sizeof(UINT64);
        readRange.End = readRange.Begin + queryCountPerFrame * sizeof(UINT64);

        void* pData = nullptr;
        ThrowIfFailed(m_TimingQueryResult->Map(0, &readRange, &pData));

        FramePerformanceData PerfData = (FramePerformanceData&)*(static_cast<UINT8*>(pData) + readRange.Begin);

        // Unmap with an empty range (written range).
        m_TimingQueryResult->Unmap(0, &emptyRange);

        for (int iStat = 0; iStat < (int)FramePerformanceStatistics::FramePerfomanceStatisticCount; iStat++)
        {
            elapsedTimeUs[iStat] = PerfData.stats[iStat].end - PerfData.stats[iStat].begin;
            elapsedTimeUs[iStat] = (elapsedTimeUs[iStat] * 1000000) / m_nComputeTimestampFreq;
        }
    }
#endif

    m_timer.Tick(NULL);

    if (m_frameCounter == 50)
    {
        float fTileCollectionTimeMs = (float)((double)elapsedTimeUs[(int)FramePerformanceStatistics::TileCollectionTime] / 1000.0);
        float fPrimitiveRenderTimeMs = (float)((double)elapsedTimeUs[(int)FramePerformanceStatistics::PrimitiveRenderTime] / 1000.0);

        // Update window text with FPS value.
        wchar_t fps[200];
        swprintf_s(fps, L"%ufps; TileCollectionTime: %0.3f ms; PrimitiveRender : %.03f ms",
            m_timer.GetFramesPerSecond(),
            fTileCollectionTimeMs,
            fPrimitiveRenderTimeMs);
        m_frameCounter = 0;
        SetCustomWindowText(fps);
    }

    m_frameCounter++;

    struct ConstBufferData
    {
        UINT m_EmitCount = 0;
        UINT m_nRandomSeed = 0;
        float m_fElapsedTime;
    };

    ConstBufferData& DataToUpload = *reinterpret_cast<ConstBufferData*>(m_constantBufferPerFrameData[m_frameIndex]);
    if (m_bPaused)
    {
        DataToUpload.m_EmitCount = 0;
        DataToUpload.m_fElapsedTime = 0.0f;
    }
    else
    {
        DataToUpload.m_EmitCount = m_nEmitCountNextFrame;
        DataToUpload.m_fElapsedTime = (float)m_timer.GetElapsedSeconds();
        DataToUpload.m_nRandomSeed = std::uniform_int_distribution<UINT>{}(m_randomNumberEngine);
    }
    m_nEmitCountNextFrame = 0;
}

void DX12Particles::RunComputeShader(int readableBufferIndex, int writableBufferIndex)
{
    ThrowIfFailed(m_commandAllocatorCompute->Reset());
    ThrowIfFailed(m_commandListCompute->Reset(m_commandAllocatorCompute.Get(), m_computePipelineStates[(int)ComputePass::Generate].Get()));

    m_commandListCompute->SetComputeRootSignature(m_rootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvHeap.Get() };
    m_commandListCompute->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    // The constant buffer view needs to be set even though we dont use it. The reason is that we specified it in the root signature.
    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvStaticHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::StaticConstantBuffer, m_cbvSrvDescriptorSize);
    m_commandListCompute->SetComputeRootDescriptorTable(0, cbvStaticHandle);

    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::PerFrameConstantBuffer0 + readableBufferIndex, m_cbvSrvDescriptorSize);
    m_commandListCompute->SetComputeRootDescriptorTable(1, cbvHandle);

    CD3DX12_GPU_DESCRIPTOR_HANDLE counterHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::DeadListUAV, m_cbvSrvDescriptorSize);
    m_commandListCompute->SetComputeRootDescriptorTable(2, counterHandle);

    // Make sure the the read buffer can be read and the write buffer can be written to
    for (auto& buffer : m_particleBuffers[readableBufferIndex].Buffers)
    {
        m_commandListCompute->ResourceBarrier(1,
            &CD3DX12_RESOURCE_BARRIER::Transition(buffer.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }

    for (auto& buffer : m_particleBuffers[writableBufferIndex].Buffers)
    {
        m_commandListCompute->ResourceBarrier(1,
            &CD3DX12_RESOURCE_BARRIER::Transition(buffer.Get(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    }

    int descriptorOffsetPerFrame = (int)DescOffset::ParticlePositionSRV1 - (int)DescOffset::ParticlePositionSRV0;
    int readDescriptorOffset = readableBufferIndex * descriptorOffsetPerFrame;
    int writeDescriptorOffset = writableBufferIndex * descriptorOffsetPerFrame;

    {
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticlePositionSRV0 + readDescriptorOffset, m_cbvSrvDescriptorSize);
        m_commandListCompute->SetComputeRootDescriptorTable(3, srvHandle);

        CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticlePositionUAV0 + writeDescriptorOffset, m_cbvSrvDescriptorSize);
        m_commandListCompute->SetComputeRootDescriptorTable(4, uavHandle);
    }

    m_commandListCompute->SetPipelineState(m_computePipelineStates[(int)ComputePass::Generate].Get());
    //m_commandListCompute->Dispatch((UINT)ceilf((float)ParticleBufferSize / 1000), 1, 1);

    // After the generation part we swap the buffers so that the update pass doesn't override the emitted particles
    for (auto& buffer : m_particleBuffers[readableBufferIndex].Buffers)
    {
        m_commandListCompute->ResourceBarrier(1,
            &CD3DX12_RESOURCE_BARRIER::Transition(buffer.Get(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    }

    for (auto& buffer : m_particleBuffers[writableBufferIndex].Buffers)
    {
        m_commandListCompute->ResourceBarrier(1,
            &CD3DX12_RESOURCE_BARRIER::Transition(buffer.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    }

    std::swap(readableBufferIndex, writableBufferIndex);
    readDescriptorOffset = readableBufferIndex * descriptorOffsetPerFrame;
    writeDescriptorOffset = writableBufferIndex * descriptorOffsetPerFrame;

    {
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticlePositionSRV0 + readDescriptorOffset, m_cbvSrvDescriptorSize);
        m_commandListCompute->SetComputeRootDescriptorTable(3, srvHandle);

        CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticlePositionUAV0 + writeDescriptorOffset, m_cbvSrvDescriptorSize);
        m_commandListCompute->SetComputeRootDescriptorTable(4, uavHandle);
    }

    m_commandListCompute->SetPipelineState(m_computePipelineStates[(int)ComputePass::Move].Get());
    m_commandListCompute->Dispatch((UINT)ceilf((float)ParticleBufferSize / 1000), 1, 1);

    m_commandListCompute->SetPipelineState(m_computePipelineStates[(int)ComputePass::Destroy].Get());
    m_commandListCompute->Dispatch((UINT)ceilf((float)ParticleBufferSize / 1000), 1, 1);

#ifdef DEBUG_PARTICLE_DATA
    m_commandListCompute->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_deadListBuffer.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE));

    m_commandListCompute->CopyResource(m_deadListReadback.Get(), m_deadListBuffer.Get());

    m_commandListCompute->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_deadListBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
#endif

#ifdef TILE_STUFF_CAN_HAPPEN
    if(m_RenderMode == RenderMode::TiledRasterization || m_RenderMode == RenderMode::Debug_TileOccupancy)
    {
        // Run the gather compute shader
        m_commandListCompute->SetComputeRootSignature(m_tileRootSignature.Get());
        
        CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::StaticConstantBuffer, m_cbvSrvDescriptorSize);
        m_commandListCompute->SetComputeRootDescriptorTable(0, cbvHandle);

        CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticleSRV0 + readableBufferIndex, m_cbvSrvDescriptorSize);
        m_commandListCompute->SetComputeRootDescriptorTable(1, srvHandle);

        {
            CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::OffsetCounterUAV, m_cbvSrvDescriptorSize);
            m_commandListCompute->SetComputeRootDescriptorTable(2, uavHandle);
        }
        {
            CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::OffsetPerTilesUAV, m_cbvSrvDescriptorSize);
            m_commandListCompute->SetComputeRootDescriptorTable(3, uavHandle);
        }
        {
            CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticleIndicesForTilesUAV, m_cbvSrvDescriptorSize);
            m_commandListCompute->SetComputeRootDescriptorTable(4, uavHandle);
        }
        {
            CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::DeadListUAV, m_cbvSrvDescriptorSize);
            m_commandListCompute->SetComputeRootDescriptorTable(5, uavHandle);
        }
        {
            CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::TileRenderDebugUAV, m_cbvSrvDescriptorSize);
            m_commandListCompute->SetComputeRootDescriptorTable(6, uavHandle);
        }

        UINT tileCountX = (UINT)((float)m_width / TILE_SIZE_IN_PIXELS + 0.5f);
        UINT tileCountY = (UINT)((float)m_height / TILE_SIZE_IN_PIXELS + 0.5f);

        // @TODO Timing this doesn't work because the command processor starts the dispatches and moves on
        // so it won't ever wait for them to finish.
        //UINT queryCountPerFrame = (int)FramePerformanceStatistics::FramePerfomanceStatisticCount * 2;
        //UINT timeQueryIndex = queryCountPerFrame * m_frameIndex + (int)FramePerformanceStatistics::TileCollectionTime * 2;
        //m_commandListCompute->EndQuery(m_TimingQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timeQueryIndex);

        m_commandListCompute->SetPipelineState(m_tilePipelineStates[(int)TileComputePass::GatherParticles].Get());
        
        m_commandListCompute->Dispatch(tileCountX, tileCountY, 1);

        m_commandListCompute->SetPipelineState(m_tilePipelineStates[(int)TileComputePass::RasterizeParticles].Get());
        m_commandListCompute->Dispatch(tileCountX, tileCountY, 1);

        m_commandListCompute->SetPipelineState(m_tilePipelineStates[(int)TileComputePass::ResetCounter].Get());
        m_commandListCompute->Dispatch(1, 1, 1);

        //m_commandListCompute->EndQuery(m_TimingQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timeQueryIndex + 1);
        //UINT startQueryIndex = m_frameIndex * queryCountPerFrame;
        //m_commandListCompute->ResolveQueryData(m_TimingQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startQueryIndex, queryCountPerFrame, m_TimingQueryResult.Get(), startQueryIndex * sizeof(UINT64));
    }
#endif

    m_commandListCompute->Close();

    ID3D12CommandList* ppCommandLists[] = { m_commandListCompute.Get() };
    m_commandQueueCompute->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

void DX12Particles::WaitForFence(bool waitOnCpu, bool bCompute)
{
    const UINT64 fence = m_fenceValue;
    m_fenceValue++;

    ID3D12CommandQueue* pCommandQueue;
    if (bCompute)
    {
        pCommandQueue = m_commandQueueCompute.Get();
    }
    else
    {
        pCommandQueue = m_commandQueue.Get();
    }

    // Wait until the previous frame is finished.
    ThrowIfFailed(pCommandQueue->Signal(m_fence.Get(), fence));
    if (m_fence->GetCompletedValue() < fence)
    {
        if (waitOnCpu)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
        else
        {
            ThrowIfFailed(pCommandQueue->Wait(m_fence.Get(), fence));
        }
    }
}

void DX12Particles::DrawParticlesWithPrimitives(int readableBufferIndex)
{
    m_commandList->SetPipelineState(m_pipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandleStatic(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::StaticConstantBuffer, m_cbvSrvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(0, cbvHandleStatic);

    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::PerFrameConstantBuffer0 + readableBufferIndex, m_cbvSrvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(1, cbvHandle);

    CD3DX12_GPU_DESCRIPTOR_HANDLE deadlistHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::DeadListUAV, m_cbvSrvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(2, deadlistHandle);

    int descriptorOffsetPerFrame = (int)DescOffset::ParticlePositionSRV1 - (int)DescOffset::ParticlePositionSRV0;
    int currentDescriptorOffset = readableBufferIndex * descriptorOffsetPerFrame;

    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticlePositionSRV0 + currentDescriptorOffset, m_cbvSrvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(3, srvHandle);

    CD3DX12_GPU_DESCRIPTOR_HANDLE uavHandle(m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::ParticlePositionUAV0, m_cbvSrvDescriptorSize);
    m_commandList->SetGraphicsRootDescriptorTable(4, uavHandle);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    m_commandList->DrawInstanced(ParticleBufferSize, 1, 0, 0);
}

void DX12Particles::RenderParticles(int readableBufferIndex)
{
    // Record all the commands we need to render the scene into the command list.
    ThrowIfFailed(m_commandAllocator->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

#if 0
    UINT queryCountPerFrame = (int)FramePerformanceStatistics::FramePerfomanceStatisticCount * 2;
    UINT timeQueryIndex = queryCountPerFrame * m_frameIndex + (int)FramePerformanceStatistics::PrimitiveRenderTime * 2;
    m_commandList->EndQuery(m_TimingQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timeQueryIndex);
#endif

    // Indicate that the back buffer will be used as a render target.
    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    m_commandList->RSSetViewports(1, &m_viewport);

    switch (m_RenderMode)
    {
    case RenderMode::TiledRasterization:
        {
            RenderDebugTexture();
        }
        break;
    case RenderMode::DrawWithPrimitives:
        {
            DrawParticlesWithPrimitives(readableBufferIndex);
        }
        break;
    default:
        break;
    }

#if 0
    m_commandList->EndQuery(m_TimingQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timeQueryIndex + 1);
    UINT startQueryIndex = m_frameIndex * queryCountPerFrame;
    m_commandList->ResolveQueryData(m_TimingQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startQueryIndex, queryCountPerFrame, m_TimingQueryResult.Get(), startQueryIndex * sizeof(UINT64));
#endif

    m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
    m_commandList->Close();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);
}

void DX12Particles::RenderDebugTexture()
{
    // Record all the commands we need to render the scene into the command list.
    m_commandList->SetPipelineState(m_debugRenderPipelineState.Get());
    m_commandList->SetGraphicsRootSignature(m_debugRenderRootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    m_commandList->SetGraphicsRootDescriptorTable(0, CD3DX12_GPU_DESCRIPTOR_HANDLE(
        m_cbvSrvHeap->GetGPUDescriptorHandleForHeapStart(), (int)DescOffset::TileRenderDebugSRV, m_cbvSrvDescriptorSize
    ));

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->DrawInstanced(1, 1, 0, 0);
}

// Render the scene.
void DX12Particles::OnRender()
{
    if (m_computeFirst)
    {
        // This one seems to be slightly faster
        RunComputeShader(m_frameIndex, (m_frameIndex + 1) % FrameCount);
        RenderParticles((m_frameIndex + 1) % FrameCount);
    }
    else
    {
        RenderParticles((m_frameIndex + 1) % FrameCount);
        RunComputeShader(m_frameIndex, (m_frameIndex + 1) % FrameCount);
    }

    // Present and update the frame index for the next frame.
    ThrowIfFailed(m_swapChain->Present(0, 0));
    WaitForFence(true, false);
    WaitForFence(true, true);

#ifdef DEBUG_PARTICLE_DATA
    DeadListBufferData* deadListBufferData;
    CD3DX12_RANGE ReadRange(0, sizeof(DeadListBufferData));
    ThrowIfFailed(m_deadListReadback->Map(0, &ReadRange, reinterpret_cast<void**>(&deadListBufferData)));
    m_LastFrameDeadListBufferData = std::make_unique<DeadListBufferData>(*deadListBufferData);
    m_deadListReadback->Unmap(0, nullptr);
#endif

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

}

void DX12Particles::OnDestroy()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    {
        const UINT64 fence = m_fenceValue;
        const UINT64 lastCompletedFence = m_fence->GetCompletedValue();

        // Signal and increment the fence value.
        ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValue));
        m_fenceValue++;

        // Wait until the previous frame is finished.
        if (lastCompletedFence < fence)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }
}

void DX12Particles::OnKeyDown(UINT8 key)
{
    m_camera.OnKeyDown(key);

    switch (key)
    {
    case 'C':
        m_computeFirst = !m_computeFirst;
        break;
    case 'G':
        m_waitForComputeOnGPU = !m_waitForComputeOnGPU;
        break;
    case 'E':
        m_nEmitCountNextFrame = 1;
        break;
    case 'R':
        m_nEmitCountNextFrame = ParticleBufferSize;
        break;
    case 'P':
        m_bPaused = !m_bPaused;
        break;
    case 'D':
        m_RenderMode = (RenderMode)(((int)m_RenderMode + 1) % 2);
        break;
    }
}

void DX12Particles::OnKeyUp(UINT8 key)
{
    m_camera.OnKeyUp(key);
}

