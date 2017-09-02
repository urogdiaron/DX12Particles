#include "ParticleCommon.hlsli"

struct VSParticleIn
{
    float4 color	: COLOR;
    uint id			: SV_VERTEXID;
};

struct VSParticleDrawOut
{
    float4 pos			: POSITION;
    float4 color		: COLOR;
	float4 scale		: SCALE;
	float4 rotate		: ROTATION;
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
        float3(-1, 1, 0),
        float3(1, 1, 0),
        float3(-1, -1, 0),
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

    PosVelo particleData = g_bufPosVelo[id];
    output.pos = float4(particleData.pos.xyz, particleData.timeLeft);
    output.color = particleData.color;
	output.scale = particleData.scale;
	output.rotate = particleData.rotate;

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

    if (input[0].pos.w == 0.0)
        return;

	float rotSin, rotCos;
	sincos(input[0].rotate.x, rotSin, rotCos);

    // Emit two new triangles.
    for (int i = 0; i < 4; i++)
    {
        float3 position = g_positions[i];
		position.xy *= input[0].scale.xy;
		
		float2 originalPosition = position.xy;
		position.x = originalPosition.x * rotCos - originalPosition.y * rotSin;
		position.y = originalPosition.x * rotSin + originalPosition.y * rotCos;

        position += input[0].pos.xyz;

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

float GetRandomNumber(inout uint rng_state)
{
    rng_state ^= (rng_state << 13);
    rng_state ^= (rng_state >> 17);
    rng_state ^= (rng_state << 5);
    // Generate a random float in [0, 1)...
    return float(rng_state) * (1.0 / 4294967296.0);
}