struct PosVelo
{
    float3 pos;
    float  timeLeft;
    float4 velocity;
    float4 color;
};

StructuredBuffer<PosVelo> g_bufPosVelo		 : register(t0);	// SRV
RWStructuredBuffer<PosVelo> g_bufPosVeloOut  : register(u0);	// UAV
RWStructuredBuffer<uint> g_deadList         : register(u1);	// UAV - g_deadList[g_nParticleBufferSize] = the current particle count

cbuffer perFrame : register(b0)
{
	float g_fAspectRatio;
    uint g_nParticleBufferSize;
};

cbuffer perFrame : register(b1)
{
    uint g_nEmitCount;
    uint g_nRandomSeed;
    float g_fElapsedTime;
};