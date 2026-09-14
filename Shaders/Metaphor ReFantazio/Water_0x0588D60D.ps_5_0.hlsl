cbuffer GFD_PSCONST_SYSTEM : register(b0)
{
	float2 resolution : packoffset(c0);
	float2 resolutionRev : packoffset(c0.z);
	float4x4 mtxView : packoffset(c1);
	float4x4 mtxInvView : packoffset(c5);
	float4x4 mtxProj : packoffset(c9);
	float4x4 mtxInvProj : packoffset(c13);
	float4 invProjParams : packoffset(c17);
}

cbuffer GFD_PSCONST_MATERIAL : register(b1)
{
	float2 TexShift : packoffset(c0);
	float TCScale : packoffset(c0.z);
	float OceanDepthScale : packoffset(c0.w);
	float DisturbanceCameraScale : packoffset(c1);
	float DisturbanceDepthScale : packoffset(c1.y);
	float ScatteringCameraScale : packoffset(c1.z);
	float DisturbanceTolerance : packoffset(c1.w);
	float FoamDistance : packoffset(c2);
	float CausticsTolerance : packoffset(c2.y);
	float triPlanarScale : packoffset(c2.z);
	float texAnimationSpeed : packoffset(c2.w);
	float4 waterDeepColor : packoffset(c3);
	float4 waterScatterColor : packoffset(c4);
	float4 waterReflectionColor : packoffset(c5);
	float4 waterFoamColor : packoffset(c6);
}

cbuffer GFD_PSCONST_OCEAN : register(b11)
{
	float4x4 reflectViewProj : packoffset(c0);
}

cbuffer GFD_PSCONST_SKY_LIGHT_PS : register(b13)
{
	float4 lightColor : packoffset(c0);
	float3 lightDirection : packoffset(c1);
	float lightSpecularIntensity : packoffset(c1.w);
	float3 lightInvDirection : packoffset(c2);
	float light_reserved_1 : packoffset(c2.w);
	float3 lightAmbient : packoffset(c3);
	float lightShadowAlpha : packoffset(c3.w);
}

cbuffer GFD_PSCONST_FOG : register(b3)
{
	float4 fogParameters : packoffset(c0);
	float4 fogColorParameter : packoffset(c1);
	float4 dirInscatColor : packoffset(c2);
	float dirInscatStartDistance : packoffset(c3);
	float fogHeightParameterX : packoffset(c3.y);
	float fogHeightParameterY : packoffset(c3.z);
	float fogExponentialHeightYRate : packoffset(c3.w);
	float4 fogHeightColor : packoffset(c4);
	float4 fogColorParameter_sky : packoffset(c5);
	float4 fogHeightColor_sky : packoffset(c6);
	float4 fogDistanceParameter : packoffset(c7);
	float4 fogDistanceColor : packoffset(c8);
	float fogHeightParameterX_sky : packoffset(c9);
	float fogHeightParameterY_sky : packoffset(c9.y);
	float fog_reserved_1 : packoffset(c9.z);
	float fog_reserved_2 : packoffset(c9.w);
	float4 fogColorParameter_toon : packoffset(c10);
	float4 fogDistanceColor_toon : packoffset(c11);
	float4 fogHeightColor_toon : packoffset(c12);
	float4 fogParameters_sky : packoffset(c13);
	float4 fogDistanceParameter_sky : packoffset(c14);
	float4 fogDistanceColor_sky : packoffset(c15);
}

cbuffer GFD_PSCONST_SHADOW : register(b6)
{
	float shadowDimmer : packoffset(c0);
	float shadowBiasPBR : packoffset(c0.y);
	float shadowBiasOther : packoffset(c0.z);
	float shadowTexShift : packoffset(c0.w);
	float4 shadowSplit : packoffset(c1);
	float3 shadowColor : packoffset(c2);
	float shadowSlopeScaledBias : packoffset(c2.w);
}

SamplerState linearWrapSampler_s : register(s1);
SamplerState pointWrapSampler_s : register(s2);
SamplerState linearClampSampler_s : register(s3);
SamplerComparisonState shadowSampler_s : register(s10);
Texture2D<float4> compositeTexture : register(t1);
Texture2D<float4> foamTexture : register(t2);
Texture2D<float4> depthTexture : register(t4);
Texture2D<float4> reflectionTexture : register(t5);
Texture2D<float4> shadowTexture0 : register(t10);
Texture2D<float4> shadowTexture1 : register(t11);
Texture2D<float4> shadowTexture2 : register(t12);

void main(
	float4 v0 : SV_POSITION0,
	float3 v1 : NORMAL0,
	float4 v2 : TEXCOORD0,
	float4 v3 : TEXCOORD1,
	float4 v4 : TEXCOORD2,
	float4 v5 : TEXCOORD3,
	float4 v6 : TEXCOORD4,
	float4 v7 : TEXCOORD5,
	float4 v8 : TEXCOORD6,
	float4 v9 : TEXCOORD7,
	float4 v10 : TEXCOORD8,
	out float4 o0 : SV_Target0,
	out float4 o1 : SV_Target1,
	out float4 o5 : SV_Target5)
{
	float4 r0,r1,r2,r3,r4,r5,r6,r7;
	uint4 bitmask, uiDest;
	float4 fDest;

	r0.x = v0.w < shadowSplit.x;
	if (r0.x != 0) {
		r0.xyz = v7.zxy / v7.www;
		r1.xyz = -shadowBiasPBR + r0.xyz;
		r0.x = shadowTexture0.SampleCmpLevelZero(shadowSampler_s, r1.yz, r1.x).x;
		r2.yz = float2(0,0);
		r2.x = -shadowTexShift;
		r3.xyzw = r2.yxxy + r0.yzyz;
		r0.w = shadowTexture0.SampleCmpLevelZero(shadowSampler_s, r3.xy, r1.x).x;
		r0.x = r0.x + r0.w;
		r4.xyzw = shadowTexShift * float4(1,-1,-1,1) + r0.yzyz;
		r0.w = shadowTexture0.SampleCmpLevelZero(shadowSampler_s, r4.xy, r1.x).x;
		r0.x = r0.x + r0.w;
		r0.w = shadowTexture0.SampleCmpLevelZero(shadowSampler_s, r3.zw, r1.x).x;
		r0.x = r0.x + r0.w;
		r0.w = shadowTexture0.SampleCmpLevelZero(shadowSampler_s, r0.yz, r1.x).x;
		r0.x = r0.x + r0.w;
		r2.w = shadowTexShift;
		r2.xyzw = r2.wzzw + r0.yzyz;
		r0.w = shadowTexture0.SampleCmpLevelZero(shadowSampler_s, r2.xy, r1.x).x;
		r0.x = r0.x + r0.w;
		r0.w = shadowTexture0.SampleCmpLevelZero(shadowSampler_s, r4.zw, r1.x).x;
		r0.x = r0.x + r0.w;
		r0.w = shadowTexture0.SampleCmpLevelZero(shadowSampler_s, r2.zw, r1.x).x;
		r0.x = r0.x + r0.w;
		r0.yz = shadowTexShift + r0.yz;
		r0.y = shadowTexture0.SampleCmpLevelZero(shadowSampler_s, r0.yz, r1.x).x;
		r0.x = r0.x + r0.y;
		r0.x = 0.111111112 * r0.x;
	} else {
		r0.y = v0.w < shadowSplit.y;
		if (r0.y != 0) {
			r0.yzw = v8.zxy / v8.www;
			r1.xyz = -shadowBiasPBR + r0.yzw;
			r0.y = shadowTexture1.SampleCmpLevelZero(shadowSampler_s, r1.yz, r1.x).x;
			r2.yz = float2(0,0);
			r2.x = -shadowTexShift;
			r3.xyzw = r2.yxxy + r0.zwzw;
			r1.y = shadowTexture1.SampleCmpLevelZero(shadowSampler_s, r3.xy, r1.x).x;
			r0.y = r1.y + r0.y;
			r4.xyzw = shadowTexShift * float4(1,-1,-1,1) + r0.zwzw;
			r1.y = shadowTexture1.SampleCmpLevelZero(shadowSampler_s, r4.xy, r1.x).x;
			r0.y = r1.y + r0.y;
			r1.y = shadowTexture1.SampleCmpLevelZero(shadowSampler_s, r3.zw, r1.x).x;
			r0.y = r1.y + r0.y;
			r1.y = shadowTexture1.SampleCmpLevelZero(shadowSampler_s, r0.zw, r1.x).x;
			r0.y = r1.y + r0.y;
			r2.w = shadowTexShift;
			r2.xyzw = r2.wzzw + r0.zwzw;
			r1.y = shadowTexture1.SampleCmpLevelZero(shadowSampler_s, r2.xy, r1.x).x;
			r0.y = r1.y + r0.y;
			r1.y = shadowTexture1.SampleCmpLevelZero(shadowSampler_s, r4.zw, r1.x).x;
			r0.y = r1.y + r0.y;
			r1.y = shadowTexture1.SampleCmpLevelZero(shadowSampler_s, r2.zw, r1.x).x;
			r0.y = r1.y + r0.y;
			r0.zw = shadowTexShift + r0.zw;
			r0.z = shadowTexture1.SampleCmpLevelZero(shadowSampler_s, r0.zw, r1.x).x;
			r0.y = r0.y + r0.z;
			r0.x = 0.111111112 * r0.y;
		} else {
			r0.yzw = v9.zxy / v9.www;
			r1.xyz = -shadowBiasPBR + r0.yzw;
			r1.x = saturate(r1.x);
			r0.y = shadowTexture2.SampleCmpLevelZero(shadowSampler_s, r1.yz, r1.x).x;
			r2.yz = float2(0,0);
			r2.x = -shadowTexShift;
			r3.xyzw = r2.yxxy + r0.zwzw;
			r1.y = shadowTexture2.SampleCmpLevelZero(shadowSampler_s, r3.xy, r1.x).x;
			r0.y = r1.y + r0.y;
			r4.xyzw = shadowTexShift * float4(1,-1,-1,1) + r0.zwzw;
			r1.y = shadowTexture2.SampleCmpLevelZero(shadowSampler_s, r4.xy, r1.x).x;
			r0.y = r1.y + r0.y;
			r1.y = shadowTexture2.SampleCmpLevelZero(shadowSampler_s, r3.zw, r1.x).x;
			r0.y = r1.y + r0.y;
			r1.y = shadowTexture2.SampleCmpLevelZero(shadowSampler_s, r0.zw, r1.x).x;
			r0.y = r1.y + r0.y;
			r2.w = shadowTexShift;
			r2.xyzw = r2.wzzw + r0.zwzw;
			r1.y = shadowTexture2.SampleCmpLevelZero(shadowSampler_s, r2.xy, r1.x).x;
			r0.y = r1.y + r0.y;
			r1.y = shadowTexture2.SampleCmpLevelZero(shadowSampler_s, r4.zw, r1.x).x;
			r0.y = r1.y + r0.y;
			r1.y = shadowTexture2.SampleCmpLevelZero(shadowSampler_s, r2.zw, r1.x).x;
			r0.y = r1.y + r0.y;
			r0.zw = shadowTexShift + r0.zw;
			r0.z = shadowTexture2.SampleCmpLevelZero(shadowSampler_s, r0.zw, r1.x).x;
			r0.y = r0.y + r0.z;
			r0.x = 0.111111112 * r0.y;
		}
	}
	r0.x = 1 + -r0.x;
	r0.x = -r0.x * shadowDimmer + 1;
	r0.y = dot(lightDirection.xyz, lightDirection.xyz);
	r0.y = rsqrt(r0.y);
	r0.yzw = lightDirection.xyz * r0.yyy;
	r0.yzw = r0.yzw * float3(-100000,-100000,-100000) + -v2.xyz;
	r1.x = dot(r0.yzw, r0.yzw);
	r1.x = rsqrt(r1.x);
	r0.yzw = r1.xxx * r0.yzw;
	r1.x = dot(v5.xyz, v5.xyz);
	r1.y = rsqrt(r1.x);
	r2.xyz = v5.xyz * r1.yyy;
	r3.xyz = v6.xyz / v6.www;
	r4.xy = r3.xy * float2(0.5,0.5) + float2(0.5,0.5);
	r1.z = dot(v1.xyz, v1.xyz);
	r1.z = rsqrt(r1.z);
	r3.xyw = v1.xyz * r1.zzz;
	r1.z = v2.y * 0.25 + 0.25;
	r1.z = max(0, r1.z);
	r1.w = dot(r0.yw, r0.yw);
	r1.w = rsqrt(r1.w);
	r5.xy = r1.ww * r0.yw;
	r1.w = dot(r5.xy, -r2.xz);
	r1.w = max(0, r1.w);
	r1.w = log2(r1.w);
	r1.zw = float2(2.5,1000) * r1.zw;
	r1.w = exp2(r1.w);
	r1.w = r1.w * r0.x;
	r1.z = r1.z * r1.w;
	r1.w = dot(r0.yzw, r3.xyw);
	r2.w = 1 + -r1.w;
	r2.w = min(0.5, r2.w);
	r2.w = max(0, r2.w);
	r2.w = r2.w * r2.w;
	r2.w = r2.w * r2.w;
	r2.w = r2.w * r2.w;
	r1.x = sqrt(r1.x);
	r4.w = 0.75 * r0.x;
	r5.x = 1 + v2.y;
	r5.x = max(0, r5.x);
	r4.w = r5.x * r4.w;
	r5.x = dot(r2.xyz, r3.xyw);
	r5.y = max(0, r5.x);
	r4.w = r5.y * r4.w;
	r1.y = -v5.y * r1.y + 1;
	r1.y = max(0, r1.y);
	r1.y = r4.w * r1.y;
	r5.yz = ScatteringCameraScale + r1.xx;
	r5.yz = ScatteringCameraScale / r5.yz;
	r1.x = r5.y * r1.y;
	r1.x = r1.z * r2.w + r1.x;
	r1.y = r0.x * 0.899999976 + 0.100000001;
	r1.x = r1.x * r1.y;
	r6.x = dot(r3.xw, mtxView._m00_m20);
	r6.y = dot(r3.xw, mtxView._m01_m21);
	r1.yz = float2(-0.0500000007,0.0500000007) * r6.xy;
	r1.yz = r1.yz * r5.zz;
	r2.w = 1 + -r5.x;
	r2.w = r2.w * r2.w;
	r2.w = r2.w * r2.w;
	r2.w = r2.w * 0.909090936 + 0.0909090936;
	r2.w = min(1, r2.w);
	r4.w = r5.x + r5.x;
	r5.xyz = r4.www * r3.xyw + -r2.xyz;
	r0.y = dot(r0.yzw, r5.xyz);
	r0.y = max(0, r0.y);
	r0.y = log2(r0.y);
	r0.y = 70 * r0.y;
	r0.y = exp2(r0.y);
	r0.z = r2.w * r2.w;
	r0.w = max(0, r1.w);
	r0.w = r0.w * 0.5 + 0.5;
	r5.xyz = waterDeepColor.xyz * r0.www;
	r0.w = dot(-r2.xyz, r3.xyw);
	r0.w = r0.w + r0.w;
	r2.xyz = r3.xyw * -r0.www + -r2.xyz;
	r6.xyz = v2.xyz + r2.xyz;
	r6.w = 1;
	r2.x = dot(r6.xyzw, reflectViewProj._m00_m10_m20_m30);
	r2.y = dot(r6.xyzw, reflectViewProj._m01_m11_m21_m31);
	r0.w = dot(r6.xyzw, reflectViewProj._m03_m13_m23_m33);
	r2.xy = r2.xy / r0.ww;
	r2.xy = r2.xy * float2(0.5,0.5) + float2(0.5,0.5);
	r2.z = 1 + -r2.y;
	r2.xy = r1.yz * float2(100,100) + r2.xz;
	r2.xyz = reflectionTexture.Sample(linearClampSampler_s, r2.xy).xyz;
	r4.z = 1 + -r4.y;
	r6.z = depthTexture.Sample(pointWrapSampler_s, r4.xz).x;
	r3.xy = r4.xz * float2(2,2) + float2(-1,-1);
	r3.xy = invProjParams.xy * r3.xy;
	r6.xy = r3.xy * -r6.zz;
	r6.w = 1;
	r7.x = dot(r6.xyzw, mtxInvView._m00_m10_m20_m30);
	r7.y = dot(r6.xyzw, mtxInvView._m01_m11_m21_m31);
	r7.z = dot(r6.xyzw, mtxInvView._m02_m12_m22_m32);
	r3.xyw = -v2.xyz + r7.xyz;
	r0.w = dot(r3.xyw, r3.xyw);
	r0.w = sqrt(r0.w);
	r1.w = saturate(r0.w / OceanDepthScale);
	r3.x = DisturbanceDepthScale * r1.w;
	r3.yw = r3.xx * r1.yz;
	r3.z = invProjParams.z + r3.z;
	r3.z = invProjParams.w / r3.z;
	r1.yz = r1.yz * r3.xx + r4.xz;
	r1.y = depthTexture.Sample(pointWrapSampler_s, r1.yz).x;
	r1.y = r3.z >= r1.y;
	r1.y = r1.y ? 1.000000 : 0;
	r1.yz = r3.yw * r1.yy + r4.xz;
	r3.xyw = compositeTexture.Sample(pointWrapSampler_s, r1.yz).xyz;
	r2.xyz = r5.xyz * r2.xyz + -r3.xyw;
	r1.yzw = r1.www * r2.xyz + r3.xyw;
	r2.x = r2.w * r0.z;
	r2.x = waterReflectionColor.w * r2.x;
	r2.yzw = waterReflectionColor.xyz + -r1.yzw;
	r1.yzw = r2.xxx * r2.yzw + r1.yzw;
	r0.y = r0.y * r0.z;
	r1.yzw = r0.yyy * float3(0.5,0.5,0.5) + r1.yzw;
	r1.xyz = waterScatterColor.xyz * r1.xxx + r1.yzw;
	r0.y = r6.z < r3.z;
	if (r0.y != 0) {
		r0.y = r0.w / FoamDistance;
		r0.y = min(1, r0.y);
		r0.y = 1 + -r0.y;
		r2.xy = float2(0.100000001,0.100000001) * TexShift.xy;
		r0.z = cos(v4.x);
		r3.x = -r2.x;
		r3.y = -r0.z;
		r2.xz = v4.xy + r3.xy;
		r0.z = foamTexture.Sample(linearWrapSampler_s, r2.xz).x;
		r1.w = sin(v4.y);
		r3.x = v4.x * 0.5 + r1.w;
		r3.y = v4.y * 0.5 + r2.y;
		r1.w = foamTexture.Sample(linearWrapSampler_s, r3.xy).z;
		r0.z = r1.w + r0.z;
		r0.z = 0.949999988 * r0.z;
		r0.z = r0.z * r0.z;
		r0.z = min(1, r0.z);
		r0.z = 1 + -r0.z;
		r0.w = 0.5 >= r0.w;
		r0.w = r0.w ? 1.000000 : 0;
		r0.z = r0.z + r0.w;
		r0.z = min(0.800000012, r0.z);
		r0.y = r0.y * r0.z;
		r0.y = waterFoamColor.w * r0.y;
		r2.xyz = waterFoamColor.xyz + -r1.xyz;
		r1.xyz = r0.yyy * r2.xyz + r1.xyz;
	}
	r0.yzw = r1.xyz * r0.xxx;
	r2.x = mtxInvView._m30;
	r2.y = mtxInvView._m31;
	r2.z = mtxInvView._m32;
	r3.xyz = v2.xyz + -r2.xyz;
	r1.w = dot(r3.xyz, r3.xyz);
	r2.y = rsqrt(r1.w);
	r1.w = r2.y * r1.w;
	r1.w = r1.w * fogDistanceParameter_sky.z + fogDistanceParameter_sky.y;
	r1.w = saturate(-1 + r1.w);
	r1.w = 1 + -r1.w;
	r1.w = fogDistanceColor_sky.w * r1.w;
	r1.xyz = -r1.xyz * r0.xxx + fogDistanceColor_sky.xyz;
	r0.xyz = r1.www * r1.xyz + r0.yzw;
	r0.w = saturate(v2.y * fogHeightParameterY_sky + fogHeightParameterX_sky);
	r0.w = fogHeightColor_sky.w * r0.w;
	r1.xyz = fogHeightColor_sky.xyz + -r0.xyz;
	r0.xyz = r0.www * r1.xyz + r0.xyz;
	r2.w = fogExponentialHeightYRate * mtxInvView._m31;
	r1.xyz = v2.xyz + -r2.xwz;
	r0.w = 0.00999999978 + -r1.y;
	r2.x = 0.00999999978 >= abs(r1.y);
	r2.x = r2.x ? 1.000000 : 0;
	r1.w = r0.w * r2.x + r1.y;
	r0.w = dot(r1.xzw, r1.xzw);
	r1.x = rsqrt(r0.w);
	r1.y = r1.w * r1.x;
	r0.w = r0.w * r1.x + -fogParameters_sky.w;
	r0.w = max(0, r0.w);
	r1.x = fogParameters_sky.y * -r2.w;
	r1.x = exp2(r1.x);
	r0.w = -r0.w * r1.y;
	r0.w = fogParameters_sky.x * r0.w;
	r0.w = exp2(r0.w);
	r0.w = 1 + -r0.w;
	r0.w = r1.x * r0.w;
	r0.w = saturate(r0.w / r1.y);
	r0.w = min(fogColorParameter_sky.w, r0.w);
	r1.xyz = fogColorParameter_sky.xyz + -r0.xyz;
	o0.xyz = r0.www * r1.xyz + r0.xyz;
	o0.w = 1;
	o1.xyzw = float4(0,0,0.0274509806,0);
		
	r0.xy = v6.xy / v6.ww;
	r1.xy = v10.xy / v10.ww;
	o5.xy = r0.xy - r1.xy;
	o5.zw = 0.0f;
	return;
}