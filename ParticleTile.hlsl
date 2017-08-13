#include "ParticleCommon.hlsli"


[numthreads(1, 1, 1)]
void CSCollectParticles(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    uint nOriginalValue;
    InterlockedAdd(g_offsetCounter[0], 50, nOriginalValue);

    g_offsetPerTiles[Gid.x] = nOriginalValue;

    for (int i = 0; i < 50; i++)
    {
        uint nIndex = nOriginalValue + i;
        g_particleIndicesForTiles[nIndex] = nIndex * 2;
    }
}