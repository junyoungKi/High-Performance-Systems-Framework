// shaders.hlsl

struct VS_INPUT
{
    float2 pos   : POSITION;
    float x      : INST_X; // Slot 1에서 옴
    float y      : INST_Y; // Slot 2에서 옴
    float3 color : INST_COLOR;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

// Vertex Shader: 정점의 위치를 결정합니다.
PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    float2 Offset = float2(input.x, input.y);
    output.pos = float4(input.pos + Offset, 0.0f, 1.0f);
    output.color = float4(input.color, 1.0f);
    return output;
}

// Pixel Shader: 각 픽셀의 최종 색상을 결정합니다.
float4 PSMain(PS_INPUT input) : SV_TARGET
{
    return input.color;
}