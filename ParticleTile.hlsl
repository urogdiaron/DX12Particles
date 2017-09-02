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

    // Culling the particles using the Separating Axis Theorem

    uint nParticlePerThread = ceil((float)nParticleCountTotal / MAX_PARTICLE_PER_TILE);
    uint nParticleStartIndex = nParticlePerThread * GTid.x;
    uint nParticleEndIndex = nParticleStartIndex + nParticlePerThread;
    nParticleEndIndex = min(nParticleEndIndex, nParticleCountTotal);

    uint2 tileTopLeftPointPx = uint2(TILE_SIZE_IN_PIXELS.xx) * Gid.xy;
    uint2 tileBottomRightPointPx = tileTopLeftPointPx + uint2(TILE_SIZE_IN_PIXELS.xx);
    
    float2 topLeft = ((float2)tileTopLeftPointPx / g_Resolution) * float2(2, -2) - float2(1, -1);
    float2 bottomRight = ((float2)tileBottomRightPointPx / g_Resolution) * float2(2, -2) - float2(1, -1);

	float2 tileCorners[4];
	tileCorners[0] = topLeft;
	tileCorners[1] = float2(bottomRight.x, topLeft.y);
	tileCorners[2] = bottomRight;
	tileCorners[3] = float2(topLeft.x, bottomRight.y);

    for (uint iParticle = nParticleStartIndex; iParticle < nParticleEndIndex; iParticle++)
    {
        PosVelo particle = g_bufPosVelo[iParticle];
        float2 p = particle.pos.xy;

		float rotate = particle.rotate.x;
		float rotSin, rotCos;
		sincos(rotate, rotSin, rotCos);

		// Note the scale parameter means the half size
		float2 particleTopLeft = p - particle.scale.xy;
		float2 particleBottomRight = p + particle.scale.xy;

		// Particle corners in cw order
		float2 particleCorners[4];
		particleCorners[0] = particleTopLeft;
		particleCorners[1] = float2(particleBottomRight.x, particleTopLeft.y);
		particleCorners[2] = particleBottomRight;
		particleCorners[3] = float2(particleTopLeft.x, particleBottomRight.y);

		for (int iCorner = 0; iCorner < 4; iCorner++)
		{
			float2 originalPosition = particleCorners[iCorner] - p;
			particleCorners[iCorner].x = originalPosition.x * rotCos - originalPosition.y * rotSin;
			particleCorners[iCorner].y = originalPosition.x * rotSin + originalPosition.y * rotCos;
            particleCorners[iCorner] += p;
		}

		bool bTileAxisX = false;
		bool bTileAxisY = false;

		bool bParticleAxisX = false;
		bool bParticleAxisY = false;

		{
			// Check with the tile's axis (which is just the xy coordinates)
			// The y min value is in the bottomRight because it goes from bottom up
			float2 tilePosMin = float2(topLeft.x, bottomRight.y);
			float2 tilePosMax = float2(bottomRight.x, topLeft.y);

			float2 particlePosMin = min(min(particleCorners[0], particleCorners[1]), min(particleCorners[2], particleCorners[3]));
			float2 particlePosMax = max(max(particleCorners[0], particleCorners[1]), max(particleCorners[2], particleCorners[3]));

			bTileAxisX = particlePosMin.x <= tilePosMax.x && tilePosMin.x <= particlePosMax.x;
			bTileAxisY = particlePosMin.y <= tilePosMax.y && tilePosMin.y <= particlePosMax.y;
		}

		{
			// Now check the particle's axises

			// They actually dont have to be normalized
			// Alternatively we could generate these directly from the rotSin and rotCos
			float2 normal1 = particleCorners[0] - particleCorners[1];
			float2 normal2 = particleCorners[1] - particleCorners[2];

			float2 tileCornersProjected[4];
			for (int iCorner = 0; iCorner < 4; iCorner++)
			{
				tileCornersProjected[iCorner] = float2(dot(tileCorners[iCorner], normal1), dot(tileCorners[iCorner], normal2));
			}

			float2 particleCornersProjected[4];
			for (int iCorner = 0; iCorner < 4; iCorner++)
			{
				particleCornersProjected[iCorner] = float2(dot(particleCorners[iCorner], normal1), dot(particleCorners[iCorner], normal2));
			}

			float2 tilePosMin = min(min(tileCornersProjected[0], tileCornersProjected[1]), min(tileCornersProjected[2], tileCornersProjected[3]));
			float2 tilePosMax = max(max(tileCornersProjected[0], tileCornersProjected[1]), max(tileCornersProjected[2], tileCornersProjected[3]));

			float2 particlePosMin = min(min(particleCornersProjected[0], particleCornersProjected[1]), min(particleCornersProjected[2], particleCornersProjected[3]));
			float2 particlePosMax = max(max(particleCornersProjected[0], particleCornersProjected[1]), max(particleCornersProjected[2], particleCornersProjected[3]));

			bParticleAxisX = particlePosMin.x <= tilePosMax.x && tilePosMin.x <= particlePosMax.x;
			bParticleAxisY = particlePosMin.y <= tilePosMax.y && tilePosMin.y <= particlePosMax.y;
		}


        if(bTileAxisX && bTileAxisY && bParticleAxisX && bParticleAxisY)
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
        float4 debugColor = nParticleCountForCurrentTile ? float4(0.2, 0.6, 0.2, 1.0) : float4(0.6, 0.2, 0.2, 1.0);
        for (uint x = tileTopLeftPointPx.x; x < tileBottomRightPointPx.x - 1; x++)
        {
            for (uint y = tileTopLeftPointPx.y; y < tileBottomRightPointPx.y - 1; y++)
            {
                g_OutputTexture[uint2(x, y)] = debugColor;
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
        else
        {
            g_offsetPerTiles[Gid.xy] = uint2(0, 0);
        }
    }
}

[numthreads(TILE_SIZE_IN_PIXELS, TILE_SIZE_IN_PIXELS, 1)]
void CSRasterizeParticles(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	return;

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
        float distanceSq = dot(toParticle, toParticle);
        if (distanceSq < 0.01 * 0.01)
        {
            color = particle.color;
        }
    }
    g_OutputTexture[DTid.xy] = color;
}