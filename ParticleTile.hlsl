#include "ParticleCommon.hlsli"
#include "TileConstants.h"

[numthreads(1, 1, 1)]
void CSResetTileOffsetCounter(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    g_offsetCounter[0] = 0;
}

RWTexture2D<float4> g_OutputTexture : register(u5);


groupshared uint gs_nParticleCountForCurrentTile;
groupshared uint gs_nParticleCountTotal;
groupshared uint gs_nOriginalValue;
groupshared uint gs_aParticleIndices[MAX_PARTICLE_PER_TILE];

// For debugging purposes
void BubbleSort()
{
    for (uint c = 0; c < (gs_nParticleCountForCurrentTile - 1); c++)
    {
        for (uint d = 0; d < gs_nParticleCountForCurrentTile - c - 1; d++)
        {
            if (gs_aParticleIndices[d] > gs_aParticleIndices[d + 1]) /* For decreasing order use < */
            {
                uint swap = gs_aParticleIndices[d];
                gs_aParticleIndices[d] = gs_aParticleIndices[d + 1];
                gs_aParticleIndices[d + 1] = swap;
            }
        }
    }
}

void BitonicSort(in uint localIdxFlattened)
{
    localIdxFlattened = localIdxFlattened * COLLECT_PARTICLE_COUNT_PER_THREAD;
    uint numParticles = gs_nParticleCountForCurrentTile;

    // Round the number of particles up to the nearest power of two
    uint numParticlesPowerOfTwo = 1;
    while (numParticlesPowerOfTwo < numParticles)
        numParticlesPowerOfTwo <<= 1;

    // The wait is required for the flow control
    GroupMemoryBarrierWithGroupSync();

   
    for (uint nMergeSize = 2; nMergeSize <= numParticlesPowerOfTwo; nMergeSize = nMergeSize * 2)
    {
        for (uint nMergeSubSize = nMergeSize >> 1; nMergeSubSize > 0; nMergeSubSize = nMergeSubSize >> 1)
        {
            for (uint element = 0; element < COLLECT_PARTICLE_COUNT_PER_THREAD; element++)
            {
                uint tmp_index = localIdxFlattened + element;
                uint index_low = tmp_index & (nMergeSubSize - 1);
                uint index_high = 2 * (tmp_index - index_low);
                uint index = index_high + index_low;

                uint nSwapElem = nMergeSubSize == nMergeSize >> 1 ?
                    index_high + (2 * nMergeSubSize - 1) - index_low :
                    index_high + nMergeSubSize + index_low;

                if (nSwapElem < numParticles && index < numParticles)
                {
                    // Here we swap the data if it's in the wrong order
                    if (gs_aParticleIndices[index] > gs_aParticleIndices[nSwapElem])
                    {
                        uint uTemp = gs_aParticleIndices[index];
                        gs_aParticleIndices[index] = gs_aParticleIndices[nSwapElem];
                        gs_aParticleIndices[nSwapElem] = uTemp;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }
}

[numthreads(MAX_PARTICLE_PER_TILE / COLLECT_PARTICLE_COUNT_PER_THREAD, 1, 1)]
void CSCollectParticles(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    // Resetting the counters
    if (GTid.x == 0)
    {
        gs_nParticleCountForCurrentTile = 0;
        gs_nParticleCountTotal = g_deadList[0];
    }

    GroupMemoryBarrierWithGroupSync();

    // Culling the particles using the Separating Axis Theorem
    uint2 tileTopLeftPointPx = uint2(TILE_SIZE_IN_PIXELS.xx) * Gid.xy;
    uint2 tileBottomRightPointPx = tileTopLeftPointPx + uint2(TILE_SIZE_IN_PIXELS.xx);
    
    float2 topLeft = ((float2)tileTopLeftPointPx / g_Resolution) * float2(2, -2) - float2(1, -1);
    float2 bottomRight = ((float2)tileBottomRightPointPx / g_Resolution) * float2(2, -2) - float2(1, -1);

	float2 tileCorners[4];
	tileCorners[0] = topLeft;
	tileCorners[1] = float2(bottomRight.x, topLeft.y);
	tileCorners[2] = bottomRight;
	tileCorners[3] = float2(topLeft.x, bottomRight.y);

    uint iCorner = 0;

    uint nParticlePerThread = ceil((float)gs_nParticleCountTotal / (MAX_PARTICLE_PER_TILE / COLLECT_PARTICLE_COUNT_PER_THREAD));

#ifdef INTERLEAVED_PARTICLE_COLLECTION
    for (uint iLoop = 0; iLoop < nParticlePerThread; iLoop++)
    {
        uint iParticle = GTid.x + iLoop * (MAX_PARTICLE_PER_TILE / COLLECT_PARTICLE_COUNT_PER_THREAD);
#else
    // @Performance: Apparently it's a good idea to have the threads work on interleaved data so that cache coherence improves as the warp is lockstepping
    uint nParticleStartIndex = nParticlePerThread * GTid.x;
    uint nParticleEndIndex = nParticleStartIndex + nParticlePerThread;
    nParticleEndIndex = min(nParticleEndIndex, gs_nParticleCountTotal);
    
    for (uint iParticle = nParticleStartIndex; iParticle < nParticleEndIndex; iParticle++)
    {
#endif
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

		for (iCorner = 0; iCorner < 4; iCorner++)
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

#ifndef DISABLE_ROTATION
		{
			// Now check the particle's axises

			// They actually dont have to be normalized
			// Alternatively we could generate these directly from the rotSin and rotCos
			float2 normal1 = particleCorners[0] - particleCorners[1];
			float2 normal2 = particleCorners[1] - particleCorners[2];

			float2 tileCornersProjected[4];
			for (iCorner = 0; iCorner < 4; iCorner++)
			{
				tileCornersProjected[iCorner] = float2(dot(tileCorners[iCorner], normal1), dot(tileCorners[iCorner], normal2));
			}

			float2 particleCornersProjected[4];
			for (iCorner = 0; iCorner < 4; iCorner++)
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
#else
        bParticleAxisX = true;
        bParticleAxisY = true;
#endif


        if(bTileAxisX && bTileAxisY && bParticleAxisX && bParticleAxisY)
        {
            uint nPrevCount;
            InterlockedAdd(gs_nParticleCountForCurrentTile, 1, nPrevCount);
            if (nPrevCount > MAX_PARTICLE_PER_TILE)
            {
                gs_nParticleCountForCurrentTile = MAX_PARTICLE_PER_TILE;
                break;
            }
            gs_aParticleIndices[nPrevCount] = iParticle;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (GTid.x == 0)
    {
        if (gs_nParticleCountForCurrentTile)
        {
            uint nParticleCountForCurrentTile = gs_nParticleCountForCurrentTile;
            uint nOriginalValue;
            InterlockedAdd(g_offsetCounter[0], nParticleCountForCurrentTile, nOriginalValue);
            g_offsetPerTiles[Gid.xy] = uint2(nOriginalValue, nParticleCountForCurrentTile);
            gs_nOriginalValue = nOriginalValue;
        }
        else
        {
            g_offsetPerTiles[Gid.xy] = uint2(0, 0);
        }
    }
    
    if (gs_nParticleCountForCurrentTile)
    {
#ifdef DEBUG_SORTING
        if (GTid.x == 0)
        {
            BubbleSort();
        }
        GroupMemoryBarrierWithGroupSync();
#else
        BitonicSort(GTid.x);
#endif

#ifdef INTERLEAVED_PARTICLE_COLLECTION
        uint nParticleCountForCurrentTile = gs_nParticleCountForCurrentTile;
        nParticlePerThread = ceil((float)gs_nParticleCountForCurrentTile / (MAX_PARTICLE_PER_TILE / COLLECT_PARTICLE_COUNT_PER_THREAD));
        for (uint iParticle = 0; iParticle < nParticlePerThread; iParticle++)
        {
            uint iParticleIndex = GTid.x + iParticle * (MAX_PARTICLE_PER_TILE / COLLECT_PARTICLE_COUNT_PER_THREAD);
            if (iParticleIndex < nParticleCountForCurrentTile)
            {
                g_particleIndicesForTiles[gs_nOriginalValue + iParticleIndex] = gs_aParticleIndices[iParticleIndex];
            }
        }
#else
        nParticlePerThread = ceil((float)gs_nParticleCountForCurrentTile / (MAX_PARTICLE_PER_TILE / COLLECT_PARTICLE_COUNT_PER_THREAD));
        nParticleStartIndex = nParticlePerThread * GTid.x;
        nParticleEndIndex = nParticleStartIndex + nParticlePerThread;
        nParticleEndIndex = min(nParticleEndIndex, gs_nParticleCountForCurrentTile);

        for (uint iParticle = nParticleStartIndex; iParticle < nParticleEndIndex; iParticle++)
        {
            g_particleIndicesForTiles[gs_nOriginalValue + iParticle] = gs_aParticleIndices[iParticle];
        }
#endif
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
    for (int iParticle = offsetAndCount.y - 1; iParticle >= 0; iParticle--)
    {
        uint particleIndex = g_particleIndicesForTiles[offsetAndCount.x + iParticle];
       
        PosVelo particle = g_bufPosVelo[particleIndex];
        float2 particlePos = particle.pos.xy;

        float rotate = particle.rotate.x;
        float rotSin, rotCos;
        sincos(rotate, rotSin, rotCos);

        // Note the scale parameter means the half size
        float2 particleTopLeft = particlePos - particle.scale.xy;
        float2 particleBottomRight = particlePos + particle.scale.xy;

        // Particle corners in cw order
        float2 particleCorners[4];
        particleCorners[0] = particleTopLeft;
        particleCorners[1] = float2(particleBottomRight.x, particleTopLeft.y);
        particleCorners[2] = particleBottomRight;
        particleCorners[3] = float2(particleTopLeft.x, particleBottomRight.y);

        for (int iCorner = 0; iCorner < 4; iCorner++)
        {
            float2 originalPosition = particleCorners[iCorner] - particlePos;
            particleCorners[iCorner].x = originalPosition.x * rotCos - originalPosition.y * rotSin;
            particleCorners[iCorner].y = originalPosition.x * rotSin + originalPosition.y * rotCos;
            particleCorners[iCorner] += particlePos;
        }

        float2 v0 = particleCorners[2] - particleCorners[3];
        float2 v1 = particleCorners[0] - particleCorners[3];

        float2 v2 = threadPos - particleCorners[3];

        // Compute dot products
        float dot00 = dot(v0, v0);
        float dot01 = dot(v0, v1);
        float dot02 = dot(v0, v2);
        float dot11 = dot(v1, v1);
        float dot12 = dot(v1, v2);

        // Compute barycentric coordinates
        float invDenom = 1 / (dot00 * dot11 - dot01 * dot01);
        float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        // Check if point is in triangle
        if ((u >= 0) && (v >= 0) && u <= 1 && v <= 1)
        {
            color = float4(u, v, 0.5, 1);
            break;
        }
    }
    g_OutputTexture[DTid.xy] = color;
}