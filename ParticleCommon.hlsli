struct PosVelo
{
    float3 pos;
    float  timeLeft;
    float4 velocity;
    float4 color;
};

StructuredBuffer<PosVelo> g_bufPosVelo		 : register(t0);	// SRV
RWStructuredBuffer<PosVelo> g_bufPosVeloOut  : register(u0);	// UAV
globallycoherent RWStructuredBuffer<uint> g_deadList         : register(u1);	// UAV - g_deadList[g_nParticleBufferSize] = the current particle count

globallycoherent RWStructuredBuffer<uint> g_offsetCounter: register(u2);
RWTexture2D<uint2> g_offsetPerTiles: register(u3);
RWStructuredBuffer<uint> g_particleIndicesForTiles : register(u4);

cbuffer globals : register(b0)
{
	float g_fAspectRatio;
    uint g_nParticleBufferSize;
    uint2 g_Resolution;
};

cbuffer perFrame : register(b1)
{
    uint g_nEmitCount;
    uint g_nRandomSeed;
    float g_fElapsedTime;
};