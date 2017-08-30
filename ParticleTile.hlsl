#include "ParticleCommon.hlsli"
#include "TileConstants.h"

[numthreads(1, 1, 1)]
void CSResetTileOffsetCounter(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    g_offsetCounter[0] = 0;
}

RWTexture2D<float4> g_OutputTexture : register(u5);


groupshared uint aParticleIndices[1024];
groupshared uint nParticleCountForCurrentTile;
groupshared uint nParticleCountTotal;

#define fParticleSize 0.01

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
        if (p.x >= (topLeft.x - fParticleSize) && 
            p.x < (bottomRight.x + fParticleSize) && 
            p.y >= (bottomRight.y - fParticleSize) && 
            p.y < (topLeft.y + fParticleSize))
        {
            uint nPrevCount;
            InterlockedAdd(nParticleCountForCurrentTile, 1, nPrevCount);
            aParticleIndices[nPrevCount] = iParticle;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // @Performance This could be done using multiple threads
    if (GTid.x == 0)
    {
        // Rendering debug texture
        /*float4 debugColor = nParticleCountForCurrentTile ? float4(0.2, 0.6, 0.2, 1.0) : float4(0.6, 0.2, 0.2, 1.0);
        for (uint x = tileTopLeftPointPx.x; x < tileBottomRightPointPx.x - 1; x++)
        {
            for (uint y = tileTopLeftPointPx.y; y < tileBottomRightPointPx.y - 1; y++)
            {
                g_OutputTexture[uint2(x, y)] = debugColor;
            }
        }*/

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
        else
        {
            g_offsetPerTiles[Gid.xy] = uint2(0, 0);
        }
    }
}

[numthreads(TILE_SIZE_IN_PIXELS, TILE_SIZE_IN_PIXELS, 1)]
void CSRasterizeParticles(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    uint nWidth, nHeight;
    g_OutputTexture.GetDimensions(nWidth, nHeight);
    if (DTid.x >= nWidth || DTid.y >= nHeight)
    {
        return;
    }

    float2 threadPos = ((float2)DTid.xy / g_Resolution) * float2(2, -2) - float2(1, -1);

    uint2 offsetAndCount = g_offsetPerTiles[Gid.xy];
    float4 color = float4(0, 0, 0, 1);
    for (uint iParticle = 0; iParticle < offsetAndCount.y; iParticle++)
    {
        uint particleIndex = g_particleIndicesForTiles[offsetAndCount.x + iParticle];
        PosVelo particle = g_bufPosVelo[particleIndex];
        
        float2 toParticle = particle.pos.xy - threadPos;
        toParticle.x *= g_fAspectRatio;
        float distanceSq = dot(toParticle, toParticle);
        if (distanceSq < 0.01 * 0.01)
        {
            color = particle.color;
        }
    }
    g_OutputTexture[DTid.xy] = color;
}