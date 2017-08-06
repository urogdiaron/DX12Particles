#include "ParticleCommon.h"

struct VSParticleIn
{
    float4 color	: COLOR;
    uint id			: SV_VERTEXID;
};

struct VSParticleDrawOut
{
    float4 pos			: POSITION;
    float4 color		: COLOR;
};

struct GSParticleDrawOut
{
    float2 tex			: TEXCOORD0;
    float4 color		: COLOR;
    float4 pos			: SV_POSITION;
};

struct PSParticleDrawIn
{
    float2 tex			: TEXCOORD0;
    float4 color		: COLOR;
};

cbuffer cbImmutable
{
    static float3 g_positions[4] =
    {
        float3(0,  0, 0),
        float3(1,  0, 0),
        float3(0, -1, 0),
        float3(1, -1, 0),
    };

    static float2 g_texcoords[4] =
    {
        float2(0, 0),
        float2(1, 0),
        float2(0, 1),
        float2(1, 1),
    };
};

//
// Vertex shader for drawing the point-sprite particles.
//
VSParticleDrawOut VSParticleDraw(uint id : SV_VERTEXID)
{
    VSParticleDrawOut output;

    uint nCountFromTheEnd = min(1000, g_nParticleBufferSize);
    uint nOffset = g_nParticleBufferSize - nCountFromTheEnd;
    uint nParticleIndex = id + nOffset;
    if (nParticleIndex >= g_nParticleBufferSize)
    {
        output.pos = float4(-100, -100, -100, 0);
        output.color.a = float4(0, 0, 0, 0);
        return output;
    }
    //g_deadList
    PosVelo particle = g_bufPosVelo[nParticleIndex];

    uint nParticlesPerRow = ceil(sqrt((float)nCountFromTheEnd));

    float posX = (float)(id % nParticlesPerRow) / nParticlesPerRow;
    posX = -1.0f + posX * 2;

    float posY = (float)(id / nParticlesPerRow) / nParticlesPerRow;
    posY = 1.0f - posY * 2;

    output.pos = float4(posX, posY, 0, 1);
    output.color = float4(particle.timeLeft != 0 ? 1 : 0.5, 0, 0, 1);

    uint nParticleCount = g_deadList[0];
    uint nNextParticle = g_deadList[g_nParticleBufferSize - nParticleCount];
    uint nDeadListValue = g_deadList[nParticleIndex + 1];

    if (id == nParticleCount)
    {
        output.color = float4(0, 0, 1, 1);
        return output;
    }

    if (nParticleIndex == nNextParticle)
    {
        output.pos.y += 0.01f;
    }



    return output;
}

//
// GS for rendering point sprite particles.  Takes a point and turns 
// it into 2 triangles.
//
[maxvertexcount(4)]
void GSParticleDraw(point VSParticleDrawOut input[1], inout TriangleStream<GSParticleDrawOut> SpriteStream)
{
    GSParticleDrawOut output;

    uint nCountFromTheEnd = min(1000, g_nParticleBufferSize);
    uint nParticlesPerRow = ceil(sqrt((float)nCountFromTheEnd));
    float fParticleSize = 1.0f / (nCountFromTheEnd / nParticlesPerRow);

    // Emit two new triangles.
    for (int i = 0; i < 4; i++)
    {
        float3 position = g_positions[i] * fParticleSize;
        position += input[0].pos.xyz;
        //position.x /= g_fAspectRatio;

        output.pos = float4(position, 1);
        output.color = input[0].color;
        output.tex = g_texcoords[i];
        SpriteStream.Append(output);
    }
    SpriteStream.RestartStrip();
}

//
// PS for drawing particles. Use the texture coordinates to generate a 
// radial gradient representing the particle.
//
float4 PSParticleDraw(PSParticleDrawIn input) : SV_Target
{
    return float4(input.color.rgb, 1);
}
