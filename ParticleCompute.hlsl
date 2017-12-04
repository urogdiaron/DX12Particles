#include "ParticleCommon.hlsli"
#include "TileConstants.h"

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

void GenerateNewParticle(uint rndSeed, out Particle particle)
{
    particle.pos = float2(0, 0);
    particle.timeLeft = -1.0f;
    particle.velocity = float2(GetRandomNumber(rndSeed) * 2.0f - 1.0f, GetRandomNumber(rndSeed) * 2.0f - 1.0f);
    particle.color = float4(GetRandomNumber(rndSeed), GetRandomNumber(rndSeed), GetRandomNumber(rndSeed), 0.02f);
    //particle.color = saturate(particle.color * 3);
    particle.scale = float2(0.01, 0.01);
#ifdef DISABLE_ROTATION
    particle.rotate = 0;
#else
    particle.rotate = GetRandomNumber(rndSeed);
#endif
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

            Particle newParticle;
            GenerateNewParticle(g_nRandomSeed + DTid.x, newParticle);
            g_particlePositionsOut[nLastParticle] = newParticle.pos;
            g_particleScalesOut[nLastParticle] = newParticle.scale;
            g_particleVelocitiesOut[nLastParticle] = newParticle.velocity;
            g_particleRotationsOut[nLastParticle] = newParticle.rotate;
            g_particleLifetimesOut[nLastParticle] = newParticle.timeLeft;
            g_particleColorsOut[nLastParticle] = newParticle.color;
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

    Particle particle;
    
    particle.pos = g_particlePositions[DTid.x];
    particle.scale = g_particleScales[DTid.x];
    particle.velocity = g_particleVelocities[DTid.x];
    particle.rotate = g_particleRotations[DTid.x];
    particle.timeLeft = g_particleLifetimes[DTid.x];
    particle.color = g_particleColors[DTid.x];

    if (particle.timeLeft == 0.0)
    {
        g_particlePositionsOut[DTid.x] = particle.pos;
        g_particleScalesOut[DTid.x] = particle.scale;
        g_particleVelocitiesOut[DTid.x] = particle.velocity;
        g_particleRotationsOut[DTid.x] = particle.rotate;
        g_particleLifetimesOut[DTid.x] = particle.timeLeft;
        g_particleColorsOut[DTid.x] = particle.color;

        return;
    }

    // Negative time means it lives forever (sounds like a bad idea tbh)
    if (particle.timeLeft > 0.0)
    {
        particle.timeLeft -= g_fElapsedTime;
        particle.timeLeft = max(0.0, particle.timeLeft);
    }

    particle.pos += particle.velocity * g_fElapsedTime;
#ifndef DISABLE_ROTATION
    particle.rotate += g_fElapsedTime * 0.5f;
#endif

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

    g_particlePositionsOut[DTid.x] = particle.pos;
    g_particleScalesOut[DTid.x] = particle.scale;
    g_particleVelocitiesOut[DTid.x] = particle.velocity;
    g_particleRotationsOut[DTid.x] = particle.rotate;
    g_particleLifetimesOut[DTid.x] = particle.timeLeft;
    g_particleColorsOut[DTid.x] = particle.color;
}

[numthreads(1000, 1, 1)]
void CSDestroy(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
}