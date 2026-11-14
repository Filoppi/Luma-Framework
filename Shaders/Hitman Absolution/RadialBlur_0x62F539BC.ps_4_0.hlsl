// The vanilla radial blur is retained for SDR. In HDR its streak overlay is
// suppressed so it cannot turn streetlamp highlights into long radial lines.

Texture2D<float4> t0 : register(t0);
SamplerState s0 : register(s0);

cbuffer cb4 : register(b4)
{
  float4 cb4[99];
}

void main(
  float4 position : SV_POSITION,
  float4 color : COLOR0,
  float2 uv : TEXCOORD0,
  out float4 output : SV_TARGET0)
{
#if TONEMAP_TYPE >= 1
  output = 0.0;
#else
  float4 center = t0.Sample(s0, uv);
  float radialStrength = saturate(cb4[51].z);
  float2 direction = uv - cb4[98].xy;
  float radius = length(direction);
  float radialFade = max((radius - cb4[97].x) / radius, 0.0) * 0.15;
  float sampleStrength = radialStrength * radialFade;
  float2 sampleDelta = sampleStrength * direction;

  float3 sum = center.xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (1.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (2.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (3.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (4.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (5.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (6.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (7.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (8.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (9.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (10.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (11.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (12.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (13.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta * (14.0 / 15.0)).xyz;
  sum += t0.Sample(s0, uv - sampleDelta).xyz;

  output.xyz = sum * (1.0 / 16.0);
  float aspectRadius = length(float2(direction.x * cb4[98].z, direction.y));
  output.w = saturate((aspectRadius - cb4[97].x) * cb4[97].y * cb4[51].z);
#endif
}
