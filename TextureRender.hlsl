Texture2D g_Texture : register(t0);

SamplerState TextureSampler : register(s0);

cbuffer cbImmutable
{
    static float2 g_positions[4] =
    {
        float2(-1,  1),
        float2( 1,  1),
        float2(-1, -1),
        float2( 1, -1),
    };

    static float2 g_texcoords[4] =
    {
        float2(0, 0),
        float2(1, 0),
        float2(0, 1),
        float2(1, 1),
    };
};

struct VSOut
{
    float4 pos : POSITION;
};

struct GSOut
{
    float4 pos			: SV_POSITION;
    float2 tex			: TEXCOORD0;
};

VSOut VSTextureRender()
{
    VSOut ret;
    ret.pos = float4(0, 0, 0, 1);
    return ret;
}

[maxvertexcount(4)]
void GSTextureRender(point VSOut dummyInput[1], inout TriangleStream<GSOut> SpriteStream)
{
    GSOut output;

    // Emit two new triangles.
    for (int i = 0; i < 4; i++)
    {
        output.pos = float4(g_positions[i], 0, 1);
        output.tex = g_texcoords[i];
        SpriteStream.Append(output);
    }
    SpriteStream.RestartStrip();
}

float4 PSTextureRender(GSOut input) : SV_Target
{
    return g_Texture.Sample(TextureSampler, input.tex.xy);
}