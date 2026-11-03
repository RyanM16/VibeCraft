struct Push { column_major float4x4 viewProjection; float4 eye; };
[[vk::push_constant]] ConstantBuffer<Push> pc;
struct Input { [[vk::location(0)]] float3 position : POSITION; [[vk::location(1)]] float3 color : COLOR0; };
struct Output { float4 position : SV_Position; [[vk::location(0)]] float3 color : COLOR0; [[vk::location(1)]] float distance : TEXCOORD0; };
Output main(Input input) { Output o; o.position=mul(pc.viewProjection,float4(input.position,1)); o.color=input.color; o.distance=length(input.position-pc.eye.xyz); return o; }
