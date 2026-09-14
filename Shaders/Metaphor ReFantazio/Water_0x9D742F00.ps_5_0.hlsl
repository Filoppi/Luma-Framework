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

cbuffer GFD_PSCONST_LIGHT_PS : register(b2)
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

SamplerState linearWrapSampler_s : register(s1);
SamplerState pointWrapSampler_s : register(s2);
SamplerState linearClampSampler_s : register(s3);
Texture2D<float4> baseTexture : register(t0);
Texture2D<float4> compositeTexture : register(t1);
Texture2D<float4> foamTexture : register(t2);
Texture2D<float4> depthTexture : register(t4);
Texture2D<float4> reflectionTexture : register(t5);

void main(
	float4 v0 : SV_POSITION0,
	float3 v1 : NORMAL0,
	float4 v2 : TEXCOORD0,
	float4 v3 : TEXCOORD1,
	float4 v4 : TEXCOORD2,
	float4 v5 : TEXCOORD3,
	float4 v6 : TEXCOORD4,
	float4 v7 : TEXCOORD5,
	out float4 o0 : SV_Target0,
	out float4 o1 : SV_Target1,
	out float4 o5 : SV_Target5)
{
	float4 r0,r1,r2,r3,r4,r5,r6,r7;
	uint4 bitmask, uiDest;
	float4 fDest;

	r0.x = dot(lightDirection.xyz, lightDirection.xyz);
	r0.x = rsqrt(r0.x);
	r0.xyz = lightDirection.xyz * r0.xxx;
	r0.xyz = r0.xyz * float3(-100000,-100000,-100000) + -v2.xyz;
	r0.w = dot(r0.xyz, r0.xyz);
	r0.w = rsqrt(r0.w);
	r0.xyz = r0.xyz * r0.www;
	r0.w = dot(v5.xyz, v5.xyz);
	r1.x = rsqrt(r0.w);
	r1.yzw = v5.xyz * r1.xxx;
	r2.xyz = v6.xyz / v6.www;
	r3.xy = r2.xy * float2(0.5,0.5) + float2(0.5,0.5);
	r2.x = dot(v1.xyz, v1.xyz);
	r2.x = rsqrt(r2.x);
	r2.xyw = v1.xyz * r2.xxx;
	r3.w = v2.y * 0.25 + 0.25;
	r3.w = max(0, r3.w);
	r3.w = 2.5 * r3.w;
	r4.x = dot(r0.xz, r0.xz);
	r4.x = rsqrt(r4.x);
	r4.xy = r4.xx * r0.xz;
	r4.x = dot(r4.xy, -r1.yw);
	r4.x = max(0, r4.x);
	r4.x = log2(r4.x);
	r4.x = 1000 * r4.x;
	r4.x = exp2(r4.x);
	r3.w = r4.x * r3.w;
	r4.x = dot(r0.xyz, r2.xyw);
	r4.y = 1 + -r4.x;
	r4.y = min(0.5, r4.y);
	r4.y = max(0, r4.y);
	r4.y = r4.y * r4.y;
	r4.y = r4.y * r4.y;
	r4.y = r4.y * r4.y;
	r0.w = sqrt(r0.w);
	r4.z = 1 + v2.y;
	r4.z = max(0, r4.z);
	r4.z = 0.75 * r4.z;
	r4.w = dot(r1.yzw, r2.xyw);
	r5.x = max(0, r4.w);
	r4.z = r5.x * r4.z;
	r1.x = -v5.y * r1.x + 1;
	r1.x = max(0, r1.x);
	r1.x = r4.z * r1.x;
	r5.xy = ScatteringCameraScale + r0.ww;
	r5.xy = ScatteringCameraScale / r5.xy;
	r0.w = r5.x * r1.x;
	r0.w = r3.w * r4.y + r0.w;
	r6.x = dot(r2.xw, mtxView._m00_m20);
	r6.y = dot(r2.xw, mtxView._m01_m21);
	r4.yz = float2(-0.0500000007,0.0500000007) * r6.xy;
	r4.yz = r4.yz * r5.yy;
	r1.x = 1 + -r4.w;
	r1.x = r1.x * r1.x;
	r1.x = r1.x * r1.x;
	r1.x = r1.x * 0.909090936 + 0.0909090936;
	r1.x = min(1, r1.x);
	r3.w = r4.w + r4.w;
	r5.xyz = r3.www * r2.xyw + -r1.yzw;
	r0.x = dot(r0.xyz, r5.xyz);
	r0.x = max(0, r0.x);
	r0.x = log2(r0.x);
	r0.x = 70 * r0.x;
	r0.x = exp2(r0.x);
	r0.y = r1.x * r1.x;
	r0.z = max(0, r4.x);
	r0.z = r0.z * 0.5 + 0.5;
	r5.xyz = waterDeepColor.xyz * r0.zzz;
	r6.xyz = max(float3(9.99999975e-06,9.99999975e-06,9.99999975e-06), abs(r2.xyw));
	r0.z = dot(r6.xyz, r6.xyz);
	r0.z = rsqrt(r0.z);
	r6.xyw = r6.xyz * r0.zzz;
	r3.w = r6.x + r6.y;
	r0.z = r6.z * r0.z + r3.w;
	r6.xyz = r6.xyw / r0.zzz;
	r4.xw = texAnimationSpeed * TexShift.xy;
	r7.xyzw = v3.yzxz * triPlanarScale + r4.xwxw;
	r0.z = baseTexture.Sample(linearWrapSampler_s, r7.xy).x;
	r3.w = baseTexture.Sample(linearWrapSampler_s, r7.zw).x;
	r4.xw = v3.xy * triPlanarScale + r4.xw;
	r4.x = baseTexture.Sample(linearWrapSampler_s, r4.xw).x;
	r3.w = r3.w * r6.y;
	r0.z = r0.z * r6.x + r3.w;
	r0.z = r4.x * r6.z + r0.z;
	r5.xyz = r5.xyz * r0.zzz;
	r0.z = dot(-r1.yzw, r2.xyw);
	r0.z = r0.z + r0.z;
	r1.yzw = r2.xyw * -r0.zzz + -r1.yzw;
	r6.xyz = v2.xyz + r1.yzw;
	r6.w = 1;
	r2.x = dot(r6.xyzw, reflectViewProj._m00_m10_m20_m30);
	r2.y = dot(r6.xyzw, reflectViewProj._m01_m11_m21_m31);
	r0.z = dot(r6.xyzw, reflectViewProj._m03_m13_m23_m33);
	r1.yz = r2.xy / r0.zz;
	r6.xy = r1.yz * float2(0.5,0.5) + float2(0.5,0.5);
	r6.z = 1 + -r6.y;
	r1.yz = r4.yz * float2(100,100) + r6.xz;
	r1.yzw = reflectionTexture.Sample(linearClampSampler_s, r1.yz).xyz;
	r3.z = 1 + -r3.y;
	r6.z = depthTexture.Sample(pointWrapSampler_s, r3.xz).x;
	r2.xy = r3.xz * float2(2,2) + float2(-1,-1);
	r2.xy = invProjParams.xy * r2.xy;
	r6.xy = r2.xy * -r6.zz;
	r6.w = 1;
	r7.x = dot(r6.xyzw, mtxInvView._m00_m10_m20_m30);
	r7.y = dot(r6.xyzw, mtxInvView._m01_m11_m21_m31);
	r7.z = dot(r6.xyzw, mtxInvView._m02_m12_m22_m32);
	r2.xyw = -v2.xyz + r7.xyz;
	r0.z = dot(r2.xyw, r2.xyw);
	r0.z = sqrt(r0.z);
	r2.x = saturate(r0.z / OceanDepthScale);
	r2.y = DisturbanceDepthScale * r2.x;
	r3.yw = r4.yz * r2.yy;
	r2.z = invProjParams.z + r2.z;
	r2.z = invProjParams.w / r2.z;
	r2.yw = r4.yz * r2.yy + r3.xz;
	r2.y = depthTexture.Sample(pointWrapSampler_s, r2.yw).x;
	r2.y = r2.z >= r2.y;
	r2.y = r2.y ? 1.000000 : 0;
	r2.yw = r3.yw * r2.yy + r3.xz;
	r3.xyz = compositeTexture.Sample(pointWrapSampler_s, r2.yw).xyz;
	r1.yzw = r5.xyz * r1.yzw + -r3.xyz;
	r1.yzw = r2.xxx * r1.yzw + r3.xyz;
	r1.x = r1.x * r0.y;
	r1.x = waterReflectionColor.w * r1.x;
	r2.xyw = waterReflectionColor.xyz + -r1.yzw;
	r1.xyz = r1.xxx * r2.xyw + r1.yzw;
	r0.x = r0.x * r0.y;
	r1.xyz = r0.xxx * float3(0.5,0.5,0.5) + r1.xyz;
	r0.xyw = waterScatterColor.xyz * r0.www + r1.xyz;
	r1.x = r6.z < r2.z;
	if (r1.x != 0) {
		r1.x = r0.z / FoamDistance;
		r1.x = min(1, r1.x);
		r1.x = 1 + -r1.x;
		r2.y = cos(v4.x);
		r2.xw = float2(0.100000001,0.100000001) * TexShift.xy;
		r1.yz = v4.xy + -r2.xy;
		r1.y = foamTexture.Sample(linearWrapSampler_s, r1.yz).x;
		r2.z = sin(v4.y);
		r1.zw = v4.xy * float2(0.5,0.5) + r2.zw;
		r1.z = foamTexture.Sample(linearWrapSampler_s, r1.zw).z;
		r1.y = r1.y + r1.z;
		r1.y = 0.949999988 * r1.y;
		r1.y = r1.y * r1.y;
		r1.y = min(1, r1.y);
		r1.y = 1 + -r1.y;
		r0.z = 0.5 >= r0.z;
		r0.z = r0.z ? 1.000000 : 0;
		r0.z = r1.y + r0.z;
		r0.z = min(0.800000012, r0.z);
		r0.z = r1.x * r0.z;
		r0.z = waterFoamColor.w * r0.z;
		r1.xyz = waterFoamColor.xyz + -r0.xyw;
		r0.xyw = r0.zzz * r1.xyz + r0.xyw;
	}
	r1.x = mtxInvView._m30;
	r1.y = mtxInvView._m31;
	r1.z = mtxInvView._m32;
	r2.xyz = v2.xyz + -r1.xyz;
	r0.z = dot(r2.xyz, r2.xyz);
	r1.y = rsqrt(r0.z);
	r0.z = r1.y * r0.z;
	r0.z = r0.z * fogDistanceParameter.z + fogDistanceParameter.y;
	r0.z = saturate(-1 + r0.z);
	r0.z = 1 + -r0.z;
	r0.z = fogDistanceColor.w * r0.z;
	r2.xyz = fogDistanceColor.xyz + -r0.xyw;
	r0.xyz = r0.zzz * r2.xyz + r0.xyw;
	r0.w = saturate(v2.y * fogHeightParameterY + fogHeightParameterX);
	r0.w = fogHeightColor.w * r0.w;
	r2.xyz = fogHeightColor.xyz + -r0.xyz;
	r0.xyz = r0.www * r2.xyz + r0.xyz;
	r1.w = fogExponentialHeightYRate * mtxInvView._m31;
	r2.xyz = v2.xyz + -r1.xwz;
	r0.w = 0.00999999978 + -r2.y;
	r1.x = 0.00999999978 >= abs(r2.y);
	r1.x = r1.x ? 1.000000 : 0;
	r2.w = r0.w * r1.x + r2.y;
	r0.w = dot(r2.xzw, r2.xzw);
	r1.x = rsqrt(r0.w);
	r1.y = r2.w * r1.x;
	r0.w = r0.w * r1.x + -fogParameters.w;
	r0.w = max(0, r0.w);
	r1.x = fogParameters.y * -r1.w;
	r1.x = exp2(r1.x);
	r0.w = -r0.w * r1.y;
	r0.w = fogParameters.x * r0.w;
	r0.w = exp2(r0.w);
	r0.w = 1 + -r0.w;
	r0.w = r1.x * r0.w;
	r0.w = saturate(r0.w / r1.y);
	r0.w = min(fogColorParameter.w, r0.w);
	r1.xyz = fogColorParameter.xyz + -r0.xyz;
	o0.xyz = r0.www * r1.xyz + r0.xyz;
	o0.w = 1;
	o1.xyzw = float4(0,0,0.0274509806,0);
	
	r0.xy = v6.xy / v6.ww;
	r1.xy = v7.xy / v7.ww;
	o5.xy = r0.xy - r1.xy;
	o5.zw = 0.0f;
	return;
}