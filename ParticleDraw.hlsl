//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

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

struct PosVelo
{
	float4 pos;
    float4 alive;
};

StructuredBuffer<PosVelo> g_bufPosVelo		 : register(t0);	// SRV
RWStructuredBuffer<PosVelo> g_bufPosVeloOut  : register(u0);	// UAV
RWStructuredBuffer<uint> g_counter           : register(u1);	// UAV

cbuffer perFrame : register(b0)
{
	float g_fAspectRatio;
    int g_nParticleCount;
};

cbuffer perFrame : register(b1)
{
    int g_nEmitCount;
};

cbuffer cb1
{
	static float g_fParticleRad = 0.01f;
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
VSParticleDrawOut VSParticleDraw(VSParticleIn input)
{
	VSParticleDrawOut output;

	output.pos = float4(g_bufPosVelo[input.id].pos.xyz, g_bufPosVelo[input.id].alive.x);
	output.color = input.color;
	
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
	
    if (input[0].pos.w < 0.5)
        return;

	// Emit two new triangles.
	for (int i = 0; i < 4; i++)
	{
		float3 position = g_positions[i] * g_fParticleRad;
		position += input[0].pos;
		position.x /= g_fAspectRatio;

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
	float intensity = 0.5f - length(float2(0.5f, 0.5f) - input.tex);
	intensity = clamp(intensity, 0.0f, 0.5f) * 2.0f;
	return float4(1, 1, 1, intensity);
}


[numthreads(1000, 1, 1)]
void CSParticleCompute(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
    g_bufPosVeloOut[DTid.x] = g_bufPosVelo[DTid.x];
    g_bufPosVeloOut[DTid.x].pos.x += 0.0001f * g_bufPosVeloOut[DTid.x].alive.x;

    if(DTid.x < g_nEmitCount)
    {
        uint nLastParticle = g_counter.IncrementCounter();
        g_bufPosVeloOut[nLastParticle].pos = float4(0, 0, 0, 0);
        g_bufPosVeloOut[nLastParticle].alive.x = 1.0;
    }
}
