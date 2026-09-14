cbuffer GFD_PSCONST_MATERIAL : register(b1)
{
	float4 matBaseColor : packoffset(c0);
	float4 matEmissiveColor : packoffset(c1);
	float matBloomIntensity : packoffset(c2);
	float atestRef : packoffset(c2.y);
	float distortionPower : packoffset(c2.z);
	float dissolveThreshold : packoffset(c2.w);
	float rimTransPower : packoffset(c3);
	float lerpBlendRate : packoffset(c3.y);
	float fittingTile : packoffset(c3.z);
	float multiFittingTile : packoffset(c3.w);
	float matBrightness : packoffset(c4);
}

SamplerState linearWrapSampler_s : register(s12);
Texture2D<float4> baseTexture : register(t0);

void main(
	float4 v0 : SV_POSITION0,
	float4 v1 : COLOR0,
	float2 v2 : TEXCOORD0,
	float4 v3 : TEXCOORD3,
	float4 v4 : TEXCOORD4,
	out float4 o0 : SV_Target0,
	out float4 o1 : SV_Target1,
	out float2 o5 : SV_Target5)
{
	float4 r0;
	float4 basecolor = baseTexture.Sample(linearWrapSampler_s, v2.xy).xyzw;
	r0.xyz = log2(abs(basecolor.xyz));
	r0.w = matBaseColor.w * basecolor.w;
	r0.xyz = float3(2.20000005,2.20000005,2.20000005) * r0.xyz;
	r0.xyz = exp2(r0.xyz);
	r0.xyz = r0.xyz * matBaseColor.xyz + matEmissiveColor.xyz;
	o0.xyzw = v1.xyzw * r0.xyzw;
	r0.x = saturate(matBloomIntensity);
	o1.z = 0.0588235334 * r0.x;
	o1.xyw = float3(0,0,0);
	o5 = v3.xy / v3.w - v4.xy / v4.w;
	if(basecolor.w == 0.0f)
	{
		discard;
	}
	return;
}