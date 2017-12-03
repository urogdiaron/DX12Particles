StructuredBuffer<float2> g_particlePositions:  register(t0);
StructuredBuffer<float2> g_particleScales:     register(t1);	
StructuredBuffer<float2> g_particleVelocities: register(t2);	
StructuredBuffer<float>  g_particleRotations:  register(t3);	
StructuredBuffer<float>  g_particleLifetimes:  register(t4);	
StructuredBuffer<float4> g_particleColors:     register(t5);

RWStructuredBuffer<float2> g_particlePositionsOut:  register(u0);
RWStructuredBuffer<float2> g_particleScalesOut:     register(u1);
RWStructuredBuffer<float2> g_particleVelocitiesOut: register(u2);
RWStructuredBuffer<float>  g_particleRotationsOut:  register(u3);
RWStructuredBuffer<float>  g_particleLifetimesOut:  register(u4);
RWStructuredBuffer<float4> g_particleColorsOut:     register(u5);

globallycoherent RWStructuredBuffer<uint> g_deadList      : register(u10);	// UAV - g_deadList[g_nParticleBufferSize] = the current particle count
globallycoherent RWStructuredBuffer<uint> g_offsetCounter : register(u11);
RWTexture2D<uint2> g_offsetPerTiles                       : register(u12);
RWStructuredBuffer<uint> g_particleIndicesForTiles        : register(u13);

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