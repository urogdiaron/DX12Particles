#include "ParticleCommon.hlsli"

float GetRandomNumber(inout uint seed)
{
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);

    // Generate a random float in [0, 1)...
    return float(seed) * (1.0 / 4294967296.0);
}

PosVelo GenerateNewParticle(uint rndSeed)
{
    PosVelo particle;

    particle.pos = float3(0, 0, 0);
    particle.timeLeft = -1.0f;
    particle.velocity = float4(GetRandomNumber(rndSeed) * 2.0f - 1.0f, GetRandomNumber(rndSeed) * 2.0f - 1.0f, 0, 0);
    particle.color = float4(GetRandomNumber(rndSeed), GetRandomNumber(rndSeed), GetRandomNumber(rndSeed), 1);
    particle.color = saturate(particle.color * 3);
	particle.scale = float4(0.01, 0.01, 0, 0);
	particle.rotate = float4(0, 0, 0, 0);
    return particle;
}

[numthreads(1000, 1, 1)]
void CSGenerate(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    if (DTid.x < g_nEmitCount)
    {
        uint nPrevParticleCount = g_deadList.IncrementCounter();
        if (nPrevParticleCount < g_nParticleBufferSize)
        {
            //There's still space left for a new particle. Mark one for creation
            uint nLastParticle = g_deadList[g_nParticleBufferSize - nPrevParticleCount];
            g_deadList[g_nParticleBufferSize - nPrevParticleCount] = 9;
            g_bufPosVeloOut[nLastParticle] = GenerateNewParticle(g_nRandomSeed + DTid.x);
        }
        else
        {
            uint nTmp;
            InterlockedMin(g_deadList[0], g_nParticleBufferSize, nTmp);
        }
    }
}

[numthreads(1000, 1, 1)]
void CSUpdate(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    if (DTid.x >= g_nParticleBufferSize)
    {
        return;
    }    

    PosVelo particle = g_bufPosVelo[DTid.x];

    if (particle.timeLeft == 0.0)
    {
        g_bufPosVeloOut[DTid.x] = particle;
        return;
    }

    // Negative time means it lives forever (sounds like a bad idea tbh)
    if (particle.timeLeft > 0.0)
    {
        particle.timeLeft -= g_fElapsedTime;
        particle.timeLeft = max(0.0, particle.timeLeft);
    }

    particle.pos.xyz += particle.velocity.xyz * g_fElapsedTime;
	particle.rotate.x += g_fElapsedTime * 0.5f;

    if (particle.pos.x < -1)
    {
        particle.pos.x = -1;
        particle.velocity.x *= -1;
    }
    else if (particle.pos.x > 1)
    {
        particle.pos.x = 1;
        particle.velocity.x *= -1;
    }

    if (particle.pos.y < -1)
    {
        particle.pos.y = -1;
        particle.velocity.y *= -1;
    }
    else if (particle.pos.y > 1)
    {
        particle.pos.y = 1;
        particle.velocity.y *= -1;
    }

    if (particle.timeLeft == 0)
    {
        int nNewParticleCount = g_deadList.DecrementCounter();
        g_deadList[g_nParticleBufferSize - nNewParticleCount] = DTid.x;
    }

    g_bufPosVeloOut[DTid.x] = particle;
}

[numthreads(1000, 1, 1)]
void CSDestroy(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
}