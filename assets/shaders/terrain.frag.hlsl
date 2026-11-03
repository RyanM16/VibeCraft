struct Input { float4 position : SV_Position; [[vk::location(0)]] float3 color : COLOR0; [[vk::location(1)]] float distance : TEXCOORD0; };
float4 main(Input input) : SV_Target0 { float fog=1-exp(-input.distance/14000.0); return float4(lerp(input.color,float3(.52,.70,.87),fog),1); }
