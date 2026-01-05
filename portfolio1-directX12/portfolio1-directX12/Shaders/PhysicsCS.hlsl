// PhysicsCS.hlsl

cbuffer SimulationConstants : register(b0) // (O) - 5.0 문법
{
    float dt;
    uint instanceCount;
    float2 padding;
};

RWStructuredBuffer<float> InstancePosY : register(u0);
StructuredBuffer<float> InstanceSpeeds : register(t0);

[numthreads(256, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= instanceCount)
        return;

    uint i = DTid.x;

    // 1. 위치 업데이트
    InstancePosY[i] -= InstanceSpeeds[i] * dt;

    // 2. 경계 처리
    if (InstancePosY[i] < -1.1f)
    {
        InstancePosY[i] = 1.1f;
    }
}