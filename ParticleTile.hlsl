#include "ParticleCommon.hlsli"
#include "TileConstants.h"

[numthreads(1, 1, 1)]
void CSResetTileOffsetCounter(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    g_offsetCounter[0] = 0;
}

RWTexture2D<float4> g_DebugTexture : register(u5);


groupshared uint aParticleIndices[1024];
groupshared uint nParticleCountForCurrentTile;
groupshared uint nParticleCountTotal;

[numthreads(MAX_PARTICLE_PER_TILE, 1, 1)]
void CSCollectParticles(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    // Resetting the counters
    if (GTid.x == 0)
    {
        nParticleCountForCurrentTile = 0;
        nParticleCountTotal = g_deadList[0];
    }

    GroupMemoryBarrierWithGroupSync();

    // Culling the particles

    uint nParticlePerThread = ceil((float)nParticleCountTotal / MAX_PARTICLE_PER_TILE);
    uint nParticleStartIndex = nParticlePerThread * GTid.x;
    uint nParticleEndIndex = nParticleStartIndex + nParticlePerThread;
    nParticleEndIndex = min(nParticleEndIndex, nParticleCountTotal);

    uint2 tileTopLeftPointPx = uint2(TILE_SIZE_IN_PIXELS.xx) * Gid.xy;
    uint2 tileBottomRightPointPx = tileTopLeftPointPx + uint2(TILE_SIZE_IN_PIXELS.xx);
    
    float2 topLeft = ((float2)tileTopLeftPointPx / g_Resolution) * float2(2, -2) - float2(1, -1);
    float2 bottomRight = ((float2)tileBottomRightPointPx / g_Resolution) * float2(2, -2) - float2(1, -1);

    for (uint iParticle = nParticleStartIndex; iParticle < nParticleEndIndex; iParticle++)
    {
        PosVelo particle = g_bufPosVelo[iParticle];
        float2 p = particle.pos.xy;
        if (p.x >= topLeft.x && p.x < bottomRight.x && p.y >= bottomRight.y && p.y < topLeft.y)
        {
            uint nPrevCount;
            InterlockedAdd(nParticleCountForCurrentTile, 1, nPrevCount);
            aParticleIndices[nPrevCount] = iParticle;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (GTid.x == 0)
    {
        // Rendering debug texture
        // @Performance This could be done using multiple threads or preferably a different compute shader
        float4 debugColor = nParticleCountForCurrentTile ? float4(0.2, 0.6, 0.2, 1.0) : float4(0.6, 0.2, 0.2, 1.0);
        //float4 debugColor = float4(topLeft.xy, bottomRight.xy);
        //float4 debugColor = ((float)nParticleCountTotal / 100.0).xxxx;
        for (uint x = tileTopLeftPointPx.x; x < tileBottomRightPointPx.x - 1; x++)
        {
            for (uint y = tileTopLeftPointPx.y; y < tileBottomRightPointPx.y - 1; y++)
            {
                g_DebugTexture[uint2(x, y)] = debugColor;
            }
        }

        // Output culled particles to global array
        if (nParticleCountForCurrentTile)
        {
            uint nOriginalValue;
            InterlockedAdd(g_offsetCounter[0], nParticleCountForCurrentTile, nOriginalValue);
            g_offsetPerTiles[Gid.xy] = uint2(nOriginalValue, nParticleCountForCurrentTile);

            // @Performance This loop can be separated for multiple threads
            for (uint iParticle = 0; iParticle < nParticleCountForCurrentTile; iParticle++)
            {
                g_particleIndicesForTiles[nOriginalValue + iParticle] = aParticleIndices[iParticle];
            }
        }
    }
}