// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 31 17:22:27 2025


cbuffer PerSceneConsts : register(b1)
{
  row_major float4x4 projectionMatrix : packoffset(c0);
  row_major float4x4 viewMatrix : packoffset(c4);
  row_major float4x4 viewProjectionMatrix : packoffset(c8);
  row_major float4x4 inverseProjectionMatrix : packoffset(c12);
  row_major float4x4 inverseViewMatrix : packoffset(c16);
  row_major float4x4 inverseViewProjectionMatrix : packoffset(c20);
  float4 eyeOffset : packoffset(c24);
  float4 adsZScale : packoffset(c25);
  float4 hdrControl0 : packoffset(c26);
  float4 hdrControl1 : packoffset(c27);
  float4 fogColor : packoffset(c28);
  float4 fogConsts : packoffset(c29);
  float4 fogConsts2 : packoffset(c30);
  float4 fogConsts3 : packoffset(c31);
  float4 fogConsts4 : packoffset(c32);
  float4 fogConsts5 : packoffset(c33);
  float4 fogConsts6 : packoffset(c34);
  float4 fogConsts7 : packoffset(c35);
  float4 fogConsts8 : packoffset(c36);
  float4 fogConsts9 : packoffset(c37);
  float3 sunFogDir : packoffset(c38);
  float4 sunFogColor : packoffset(c39);
  float2 sunFog : packoffset(c40);
  float4 zNear : packoffset(c41);
  float3 clothPrimaryTint : packoffset(c42);
  float3 clothSecondaryTint : packoffset(c43);
  float4 renderTargetSize : packoffset(c44);
  float4 upscaledTargetSize : packoffset(c45);
  float4 materialColor : packoffset(c46);
  float4 cameraUp : packoffset(c47);
  float4 cameraLook : packoffset(c48);
  float4 cameraSide : packoffset(c49);
  float4 cameraVelocity : packoffset(c50);
  float4 skyMxR : packoffset(c51);
  float4 skyMxG : packoffset(c52);
  float4 skyMxB : packoffset(c53);
  float4 sunMxR : packoffset(c54);
  float4 sunMxG : packoffset(c55);
  float4 sunMxB : packoffset(c56);
  float4 skyRotationTransition : packoffset(c57);
  float4 debugColorOverride : packoffset(c58);
  float4 debugAlphaOverride : packoffset(c59);
  float4 debugNormalOverride : packoffset(c60);
  float4 debugSpecularOverride : packoffset(c61);
  float4 debugGlossOverride : packoffset(c62);
  float4 debugOcclusionOverride : packoffset(c63);
  float4 debugStreamerControl : packoffset(c64);
  float4 emblemLUTSelector : packoffset(c65);
  float4 colorMatrixR : packoffset(c66);
  float4 colorMatrixG : packoffset(c67);
  float4 colorMatrixB : packoffset(c68);
  float4 gameTime : packoffset(c69);
  float4 gameTick : packoffset(c70);
  float4 subpixelOffset : packoffset(c71);
  float4 viewportDimensions : packoffset(c72);
  float4 viewSpaceScaleBias : packoffset(c73);
  float4 ui3dUVSetup0 : packoffset(c74);
  float4 ui3dUVSetup1 : packoffset(c75);
  float4 ui3dUVSetup2 : packoffset(c76);
  float4 ui3dUVSetup3 : packoffset(c77);
  float4 ui3dUVSetup4 : packoffset(c78);
  float4 ui3dUVSetup5 : packoffset(c79);
  float4 clipSpaceLookupScale : packoffset(c80);
  float4 clipSpaceLookupOffset : packoffset(c81);
  uint4 computeSpriteControl : packoffset(c82);
  float4 invBcTexSizes : packoffset(c83);
  float4 invMaskTexSizes : packoffset(c84);
  float4 relHDRExposure : packoffset(c85);
  uint4 triDensityFlags : packoffset(c86);
  float4 triDensityParams : packoffset(c87);
  float4 voldecalRevealTextureInfo : packoffset(c88);
  float4 extraClipPlane0 : packoffset(c89);
  float4 extraClipPlane1 : packoffset(c90);
  float4 shaderDebug : packoffset(c91);
  uint isDepthHack : packoffset(c92);
}

SamplerState pointClamp_s : register(s1);
SamplerState linearClamp_s : register(s2);
Texture2D<float4> colorMapSampler : register(t0);
Texture2D<float4> sceneVelocitySampler : register(t6);
Texture2D<float4> sceneVelocityTile1Sampler : register(t7);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

float4 SampleFromVelocity(const uint sampleTotal, float4 color, float4 velUV, float4 r1_, float4 r4_, float velOff) {
  float4 r1, r2, r3, r4, r5, r6, r7, r8, r9, r10;
  r4.y = r4_.y;
  r1.yz = r1_.yz;
  r5 = color;
  r6 = velUV;

  r7.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
  r8.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
  r7.xy = r7.xy * float2(127.5,127.5) + float2(-64,-64);
  r1.w = dot(r7.xy, r7.xy);
  r4.w = sqrt(r1.w);
  r7.xy = r8.xy * float2(127.5,127.5) + float2(-64,-64);
  r1.w = dot(r7.xy, r7.xy);
  r4.x = sqrt(r1.w);
  r1.w = r7.z * r7.z;
  r7.x = r8.z * r8.z;
  r1.w = r1.w * 8192 + -r1.z;

  r7.yw = saturate(r1.ww * float2(1,-1) + float2(0.5,0.5));
  r8.xy = saturate(r1.yy * r4.yw + velOff);
  r1.w = dot(r7.yw, r8.xy);
  r7.x = r7.x * 8192 + -r1.z;
  r7.xy = saturate(r7.xx * float2(1,-1) + float2(0.5,0.5));
  r8.xy = saturate(r1.yy * r4.yx + velOff);
  r7.x = dot(r7.xy, r8.xy);
  r7.y = cmp(r8.z < r7.z);
  r7.z = cmp(r4.w < r4.x);
  r7.w = r7.z ? r7.y : 0;
  r8.x = r7.w ? r7.x : r1.w;
  r7.y = (int)r7.z | (int)r7.y;
  r7.y = (int)r7.y | (int)r7.w;
  r1.w = r7.y ? r7.x : r1.w;

  //sample both forwards and backwards
  r7.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
  r7.w = 1.f/sampleTotal;
  r5.xyzw = r8.xxxx * r7.xyzw + r5.xyzw;
  
  r6.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
  r6.w = 1.f/sampleTotal;
  r5.xyzw = r1.wwww * r6.xyzw + r5.xyzw;

  return r5;
}


//https://docs.google.com/spreadsheets/d/190gRS4lsqYmRr7cMskOMDoPgaKQnWScLHTpwqwjayJk/edit?usp=sharing
#if CUSTOM_MB_QUALITY == 0
  static const uint samplePairs = 6;
  static const float velMultipliers[samplePairs] = {0.0869565234, 0.260869563, 0.434782624, 0.608695626, 0.782608688, 0.956521749}; 
  static const float velOffset[samplePairs] = {0, -0.5, -1.5, -2.5, -3.5, -4.5};
#elif CUSTOM_MB_QUALITY == 1
  static const uint samplePairs = 16;
  static const float velMultipliers[samplePairs] = { 0.08695650000, 0.14492750000, 0.20289850000, 0.26086950000, 0.31884050000, 0.37681150000, 0.43478250000, 0.49275350000, 0.55072450000, 0.60869550000, 0.66666650000, 0.72463750000, 0.78260850000, 0.84057950000, 0.89855050000, 0.95652150000 };
  static const float velOffset[samplePairs] = { 0.000000000000,  -0.071429000000, -0.380953000000, -0.690477000000, -1.000001000000, -1.309525000000, -1.619049000000, -1.928573000000, -2.238097000000, -2.547621000000, -2.857145000000, -3.166669000000, -3.476193000000, -3.785717000000, -4.095241000000, -4.404765000000 };
#elif CUSTOM_MB_QUALITY == 2
  static const uint samplePairs = 24;
  static const float velMultipliers[samplePairs] = { 0.08695650000, 0.12476367391, 0.16257084783, 0.20037802174, 0.23818519565, 0.27599236957, 0.31379954348, 0.35160671739, 0.38941389130, 0.42722106522, 0.46502823913, 0.50283541304, 0.54064258696, 0.57844976087, 0.61625693478, 0.65406410870, 0.69187128261, 0.72967845652, 0.76748563043, 0.80529280435, 0.84309997826, 0.88090715217, 0.91871432609, 0.95652150000 };
  static const float velOffset[samplePairs] = { 0.000000000000,  0.000000000000,  -0.165631956522, -0.367495434783, -0.569358913043, -0.771222391304, -0.973085869565, -1.174949347826, -1.376812826087, -1.578676304348, -1.780539782609, -1.982403260870, -2.184266739130, -2.386130217391, -2.587993695652, -2.789857173913, -2.991720652174, -3.193584130435, -3.395447608696, -3.597311086957, -3.799174565217, -4.001038043478, -4.202901521739, -4.404765000000 };
#elif CUSTOM_MB_QUALITY == 3
  static const uint samplePairs = 32;
  static const float velMultipliers[samplePairs] = { 0.08695650000, 0.11500698387, 0.14305746774, 0.17110795161, 0.19915843548, 0.22720891935, 0.25525940323, 0.28330988710, 0.31136037097, 0.33941085484, 0.36746133871, 0.39551182258, 0.42356230645, 0.45161279032, 0.47966327419, 0.50771375806, 0.53576424194, 0.56381472581, 0.59186520968, 0.61991569355, 0.64796617742, 0.67601666129, 0.70406714516, 0.73211762903, 0.76016811290, 0.78821859677, 0.81626908065, 0.84431956452, 0.87237004839, 0.90042053226, 0.92847101613, 0.95652150000};
  static const float velOffset[samplePairs] = { 0.000000000000,0.000000000000,-0.061444354839,-0.211214032258,-0.360983709677,-0.510753387097,-0.660523064516,-0.810292741935,-0.960062419355,-1.109832096774,-1.259601774194,-1.409371451613,-1.559141129032,-1.708910806452,-1.858680483871,-2.008450161290,-2.158219838710,-2.307989516129,-2.457759193548,-2.607528870968,-2.757298548387,-2.907068225806,-3.056837903226,-3.206607580645,-3.356377258065,-3.506146935484,-3.655916612903,-3.805686290323,-3.955455967742,-4.105225645161,-4.254995322581,-4.404765000000 };
#elif CUSTOM_MB_QUALITY == 4
  static const uint samplePairs = 48;
  static const float velMultipliers[samplePairs] = { 0.08695650000, 0.10545788298, 0.12395926596, 0.14246064894, 0.16096203191, 0.17946341489, 0.19796479787, 0.21646618085, 0.23496756383, 0.25346894681, 0.27197032979, 0.29047171277, 0.30897309574, 0.32747447872, 0.34597586170, 0.36447724468, 0.38297862766, 0.40148001064, 0.41998139362, 0.43848277660, 0.45698415957, 0.47548554255, 0.49398692553, 0.51248830851, 0.53098969149, 0.54949107447, 0.56799245745, 0.58649384043, 0.60499522340, 0.62349660638, 0.64199798936, 0.66049937234, 0.67900075532, 0.69750213830, 0.71600352128, 0.73450490426, 0.75300628723, 0.77150767021, 0.79000905319, 0.80851043617, 0.82701181915, 0.84551320213, 0.86401458511, 0.88251596809, 0.90101735106, 0.91951873404, 0.93802011702, 0.95652150000 };
  static const float velOffset[samplePairs] = { 0.000000000000,  0.000000000000,  0.000000000000,  -0.058257765957, -0.157042021277, -0.255826276596, -0.354610531915, -0.453394787234, -0.552179042553, -0.650963297872, -0.749747553191, -0.848531808511, -0.947316063830, -1.046100319149, -1.144884574468, -1.243668829787, -1.342453085106, -1.441237340426, -1.540021595745, -1.638805851064, -1.737590106383, -1.836374361702, -1.935158617021, -2.033942872340, -2.132727127660, -2.231511382979, -2.330295638298, -2.429079893617, -2.527864148936, -2.626648404255, -2.725432659574, -2.824216914894, -2.923001170213, -3.021785425532, -3.120569680851, -3.219353936170, -3.318138191489, -3.416922446809, -3.515706702128, -3.614490957447, -3.713275212766, -3.812059468085, -3.910843723404, -4.009627978723, -4.108412234043, -4.207196489362, -4.305980744681, -4.404765000000 };
#elif CUSTOM_MB_QUALITY == 5
  static const uint samplePairs = 64;
  static const float velMultipliers[samplePairs] = { 0.08695650000, 0.10075911905, 0.11456173810, 0.12836435714, 0.14216697619, 0.15596959524, 0.16977221429, 0.18357483333, 0.19737745238, 0.21118007143, 0.22498269048, 0.23878530952, 0.25258792857, 0.26639054762, 0.28019316667, 0.29399578571, 0.30779840476, 0.32160102381, 0.33540364286, 0.34920626190, 0.36300888095, 0.37681150000, 0.39061411905, 0.40441673810, 0.41821935714, 0.43202197619, 0.44582459524, 0.45962721429, 0.47342983333, 0.48723245238, 0.50103507143, 0.51483769048, 0.52864030952, 0.54244292857, 0.55624554762, 0.57004816667, 0.58385078571, 0.59765340476, 0.61145602381, 0.62525864286, 0.63906126190, 0.65286388095, 0.66666650000, 0.68046911905, 0.69427173810, 0.70807435714, 0.72187697619, 0.73567959524, 0.74948221429, 0.76328483333, 0.77708745238, 0.79089007143, 0.80469269048, 0.81849530952, 0.83229792857, 0.84610054762, 0.85990316667, 0.87370578571, 0.88750840476, 0.90131102381, 0.91511364286, 0.92891626190, 0.94271888095, 0.95652150000 };
  static const float velOffset[samplePairs] = { 0.000000000000,  0.000000000000,  0.000000000000,  0.000000000000,  -0.056689761905, -0.130385952381, -0.204082142857, -0.277778333333, -0.351474523810, -0.425170714286, -0.498866904762, -0.572563095238, -0.646259285714, -0.719955476190, -0.793651666667, -0.867347857143, -0.941044047619, -1.014740238095, -1.088436428571, -1.162132619048, -1.235828809524, -1.309525000000, -1.383221190476, -1.456917380952, -1.530613571429, -1.604309761905, -1.678005952381, -1.751702142857, -1.825398333333, -1.899094523810, -1.972790714286, -2.046486904762, -2.120183095238, -2.193879285714, -2.267575476190, -2.341271666667, -2.414967857143, -2.488664047619, -2.562360238095, -2.636056428571, -2.709752619048, -2.783448809524, -2.857145000000, -2.930841190476, -3.004537380952, -3.078233571429, -3.151929761905, -3.225625952381, -3.299322142857, -3.373018333333, -3.446714523810, -3.520410714286, -3.594106904762, -3.667803095238, -3.741499285714, -3.815195476190, -3.888891666667, -3.962587857143, -4.036284047619, -4.109980238095, -4.183676428571, -4.257372619048, -4.331068809524, -4.404765000000 };
#elif CUSTOM_MB_QUALITY == 6
  static const uint samplePairs = 128;
  static const float velMultipliers[samplePairs] = { 0.08695650000, 0.09380346850, 0.10065043701, 0.10749740551, 0.11434437402, 0.12119134252, 0.12803831102, 0.13488527953, 0.14173224803, 0.14857921654, 0.15542618504, 0.16227315354, 0.16912012205, 0.17596709055, 0.18281405906, 0.18966102756, 0.19650799606, 0.20335496457, 0.21020193307, 0.21704890157, 0.22389587008, 0.23074283858, 0.23758980709, 0.24443677559, 0.25128374409, 0.25813071260, 0.26497768110, 0.27182464961, 0.27867161811, 0.28551858661, 0.29236555512, 0.29921252362, 0.30605949213, 0.31290646063, 0.31975342913, 0.32660039764, 0.33344736614, 0.34029433465, 0.34714130315, 0.35398827165, 0.36083524016, 0.36768220866, 0.37452917717, 0.38137614567, 0.38822311417, 0.39507008268, 0.40191705118, 0.40876401969, 0.41561098819, 0.42245795669, 0.42930492520, 0.43615189370, 0.44299886220, 0.44984583071, 0.45669279921, 0.46353976772, 0.47038673622, 0.47723370472, 0.48408067323, 0.49092764173, 0.49777461024, 0.50462157874, 0.51146854724, 0.51831551575, 0.52516248425, 0.53200945276, 0.53885642126, 0.54570338976, 0.55255035827, 0.55939732677, 0.56624429528, 0.57309126378, 0.57993823228, 0.58678520079, 0.59363216929, 0.60047913780, 0.60732610630, 0.61417307480, 0.62102004331, 0.62786701181, 0.63471398031, 0.64156094882, 0.64840791732, 0.65525488583, 0.66210185433, 0.66894882283, 0.67579579134, 0.68264275984, 0.68948972835, 0.69633669685, 0.70318366535, 0.71003063386, 0.71687760236, 0.72372457087, 0.73057153937, 0.73741850787, 0.74426547638, 0.75111244488, 0.75795941339, 0.76480638189, 0.77165335039, 0.77850031890, 0.78534728740, 0.79219425591, 0.79904122441, 0.80588819291, 0.81273516142, 0.81958212992, 0.82642909843, 0.83327606693, 0.84012303543, 0.84697000394, 0.85381697244, 0.86066394094, 0.86751090945, 0.87435787795, 0.88120484646, 0.88805181496, 0.89489878346, 0.90174575197, 0.90859272047, 0.91543968898, 0.92228665748, 0.92913362598, 0.93598059449, 0.94282756299, 0.94967453150, 0.95652150000 };
  static const float velOffset[samplePairs] = { 0.000000000000,  0.000000000000,  0.000000000000,  0.000000000000,  0.000000000000,  0.000000000000, 0.000000000000,  -0.017810669291, -0.054368622047, -0.090926574803, -0.127484527559, -0.164042480315, -0.200600433071, -0.237158385827, -0.273716338583, -0.310274291339, -0.346832244094, -0.383390196850, -0.419948149606, -0.456506102362, -0.493064055118, -0.529622007874, -0.566179960630, -0.602737913386, -0.639295866142, -0.675853818898, -0.712411771654, -0.748969724409, -0.785527677165, -0.822085629921, -0.858643582677, -0.895201535433, -0.931759488189, -0.968317440945, -1.004875393701, -1.041433346457, -1.077991299213, -1.114549251969, -1.151107204724, -1.187665157480, -1.224223110236, -1.260781062992, -1.297339015748, -1.333896968504, -1.370454921260, -1.407012874016, -1.443570826772, -1.480128779528, -1.516686732283, -1.553244685039, -1.589802637795, -1.626360590551, -1.662918543307, -1.699476496063, -1.736034448819, -1.772592401575, -1.809150354331, -1.845708307087, -1.882266259843, -1.918824212598, -1.955382165354, -1.991940118110, -2.028498070866, -2.065056023622, -2.101613976378, -2.138171929134, -2.174729881890, -2.211287834646, -2.247845787402, -2.284403740157, -2.320961692913, -2.357519645669, -2.394077598425, -2.430635551181, -2.467193503937, -2.503751456693, -2.540309409449, -2.576867362205, -2.613425314961, -2.649983267717, -2.686541220472, -2.723099173228, -2.759657125984, -2.796215078740, -2.832773031496, -2.869330984252, -2.905888937008, -2.942446889764, -2.979004842520, -3.015562795276, -3.052120748032, -3.088678700787, -3.125236653543, -3.161794606299, -3.198352559055, -3.234910511811, -3.271468464567, -3.308026417323, -3.344584370079, -3.381142322835, -3.417700275591, -3.454258228346, -3.490816181102, -3.527374133858, -3.563932086614, -3.600490039370, -3.637047992126, -3.673605944882, -3.710163897638, -3.746721850394, -3.783279803150, -3.819837755906, -3.856395708661, -3.892953661417, -3.929511614173, -3.966069566929, -4.002627519685, -4.039185472441, -4.075743425197, -4.112301377953, -4.148859330709, -4.185417283465, -4.221975236220, -4.258533188976, -4.295091141732, -4.331649094488, -4.368207047244, -4.404765000000 };
#endif
  static const uint sampleTotal = samplePairs * 2;



float4 LoopSampleMotionBlur(float4 r3, float4 r2, float4 r1, float4 r4, float scale) {
  float4 color = 0;
  for (uint i = 0; i < samplePairs; i++) {
    //velUV
    float a = velMultipliers[i] * scale;
    float4 velUV = r3.zwzw * float4(a, a, -a, -a) + r2.zwzw;

    // sample
    color = SampleFromVelocity(sampleTotal, color, velUV, r1, r4, velOffset[i]);
  }

  return color;
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9;
  uint4 bitmask, uiDest;
  float4 fDest;

  //color
  r0.xyzw = colorMapSampler.Sample(pointClamp_s, v1.xy).xyzw;

  //threshold discard
  r1.xy = renderTargetSize.xy * v1.xy;
  r1.xy = (uint2)r1.xy;
  r1.xy = (int2)r1.xy & int2(1,1);
  r1.xy = (uint2)r1.xy;
  r1.x = r1.x * 0.5 + -0.25;
  r1.y = r1.y * 2 + -1;
  r1.x = r1.x * r1.y;
  r1.yz = renderTargetSize.zw * r1.xx;
  r1.yz = r1.yz * float2(20,20) + v1.xy;
  r1.yzw = sceneVelocityTile1Sampler.Sample(pointClamp_s, r1.yz).xyz;

  r2.x = cmp(r1.w < 0.00392156886);
  if (r2.x != 0) {
    o0.xyzw = r0.xyzw;
    return;
  }

  r3.xyzw = r1.yzyz * float4(127.5,127.5,127.5,127.5) + float4(-64,-64,-64,-64);
  r3.xyzw = round(r3.xyzw);
  r3.xyzw = renderTargetSize.zwzw * r3.xyzw;

  r2.x = sceneVelocityTile1Sampler.Sample(pointClamp_s, v1.xy).x;
  r1.y = cmp(0 != r2.x);

  //if motion
  if (r1.y != 0) { 
    const float scale = GS.MotionBlur;

    //prep motion
    r1.y = 255 * r1.w;
    r1.y = 5.75 / r1.y;
    r2.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, v1.xy, 1).xyz;

    r1.zw = r2.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.z = dot(r1.zw, r1.zw);
    r4.y = sqrt(r1.z);
    r1.z = r2.z * r2.z;
    r1.z = 8192 * r1.z;
    
    r2.xyzw = r3.zwzw * r1.xxxx;
    r2.xyzw = r2.xyzw * 0.173913047 + v1.xyxy; //motion vector init for subsequent samples

    r2 = LoopSampleMotionBlur(r3, r2, r1, r4, scale);
    r1.y = 1 + -r2.w;
    r1.yzw = r1.yyy * r0.xyz;
    r0.xyz = r2.xyz * (1.f/sampleTotal) + r1.yzw;
    // r0.xyz = r2.xyz;
    
  /* 
    //sample 1 2
    // r5.xyzw = r3.zwzw * float4(0.0869565234,0.0869565234,-0.0869565234,-0.0869565234) + r2.zwzw;
    {
      const float a = 0.0869565234 * scale; // 1/11.5
      r5.xyzw = r3.zwzw * float4(a, a, -a, -a) + r2.zwzw;
    }

    r6.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r5.xy, 1).xyz;
    r7.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r5.zw, 1).xyz;
    r6.xy = r6.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r6.xy, r6.xy);
    r4.x = sqrt(r1.w);
    r6.xy = r7.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r6.xy, r6.xy);
    r4.z = sqrt(r1.w);
    r1.w = r6.z * r6.z;
    r6.x = r7.z * r7.z;
    r1.w = r1.w * 8192 + -r1.z;

    r6.yw = saturate(r1.ww * float2(1,-1) + float2(0.5,0.5));
    r8.xyzw = saturate(r4.yxyz * r1.yyyy);
    r1.w = dot(r6.yw, r8.xy);
    r6.x = r6.x * 8192 + -r1.z;
    r6.xy = saturate(r6.xx * float2(1,-1) + float2(0.5,0.5));
    r6.x = dot(r6.xy, r8.zw);
    r6.y = cmp(r7.z < r6.z);
    r6.z = cmp(r4.x < r4.z);
    r6.w = r6.z ? r6.y : 0;
    r7.x = r6.w ? r6.x : r1.w;
    r6.y = (int)r6.z | (int)r6.y;
    r6.y = (int)r6.y | (int)r6.w;
    r1.w = r6.y ? r6.x : r1.w;

    r6.xyz = colorMapSampler.SampleLevel(pointClamp_s, r5.xy, 1).xyz;
    r6.w = 0.0833333358;

    r5.xyz = colorMapSampler.SampleLevel(pointClamp_s, r5.zw, 1).xyz;
    r5.w = 0.0833333358;
    r5.xyzw = r5.xyzw * r1.wwww;

    r5.xyzw = r7.xxxx * r6.xyzw + r5.xyzw; //diff from others because this is first
    
    //sample 3 4
    // r6.xyzw = r3.zwzw * float4(0.260869563,0.260869563,-0.260869563,-0.260869563) + r2.zwzw;
    {
      const float a = 0.260869563 * scale; // 1/3.8333
      r6.xyzw = r3.zwzw * float4(a, a, -a, -a) + r2.zwzw;
    }

    r7.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
    r8.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
    r7.xy = r7.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r7.xy, r7.xy);
    r4.w = sqrt(r1.w);
    r7.xy = r8.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r7.xy, r7.xy);
    r4.x = sqrt(r1.w);
    r1.w = r7.z * r7.z;
    r7.x = r8.z * r8.z;
    r1.w = r1.w * 8192 + -r1.z;

    r7.yw = saturate(r1.ww * float2(1,-1) + float2(0.5,0.5));
    r8.xy = saturate(r1.yy * r4.yw + float2(-0.5,-0.5));
    r1.w = dot(r7.yw, r8.xy);
    r7.x = r7.x * 8192 + -r1.z;
    r7.xy = saturate(r7.xx * float2(1,-1) + float2(0.5,0.5));
    r8.xy = saturate(r1.yy * r4.yx + float2(-0.5,-0.5));
    r7.x = dot(r7.xy, r8.xy);
    r7.y = cmp(r8.z < r7.z);
    r7.z = cmp(r4.w < r4.x);
    r7.w = r7.z ? r7.y : 0;
    r8.x = r7.w ? r7.x : r1.w;
    r7.y = (int)r7.z | (int)r7.y;
    r7.y = (int)r7.y | (int)r7.w;
    r1.w = r7.y ? r7.x : r1.w;

    r7.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
    r7.w = 0.0833333358;
    r5.xyzw = r8.xxxx * r7.xyzw + r5.xyzw;
    
    r6.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
    r6.w = 0.0833333358;
    r5.xyzw = r1.wwww * r6.xyzw + r5.xyzw;

    //sample 5 6
    // r6.xyzw = r3.zwzw * float4(0.434782624,0.434782624,-0.434782624,-0.434782624) + r2.zwzw;
    {
      const float a = 0.434782624 * scale; // 1/2.3
      r6.xyzw = r3.zwzw * float4(a, a, -a, -a) + r2.zwzw;
    }

    r7.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
    r8.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
    r7.xy = r7.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r7.xy, r7.xy);
    r4.z = sqrt(r1.w);
    r7.xy = r8.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r7.xy, r7.xy);
    r4.w = sqrt(r1.w);
    r1.w = r7.z * r7.z;
    r7.x = r8.z * r8.z;
    r1.w = r1.w * 8192 + -r1.z;

    r7.yw = saturate(r1.ww * float2(1,-1) + float2(0.5,0.5));
    r9.xyzw = saturate(r1.yyyy * r4.yzyw + float4(-1.5,-1.5,-1.5,-1.5));
    r1.w = dot(r7.yw, r9.xy);
    r7.x = r7.x * 8192 + -r1.z;
    r7.xy = saturate(r7.xx * float2(1,-1) + float2(0.5,0.5));
    r7.x = dot(r7.xy, r9.zw);
    r7.y = cmp(r8.z < r7.z);
    r7.z = cmp(r4.z < r4.w);
    r7.w = r7.z ? r7.y : 0;
    r8.x = r7.w ? r7.x : r1.w;
    r7.y = (int)r7.z | (int)r7.y;
    r7.y = (int)r7.y | (int)r7.w;
    r1.w = r7.y ? r7.x : r1.w;

    r7.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
    r7.w = 0.0833333358;
    r5.xyzw = r8.xxxx * r7.xyzw + r5.xyzw;

    r6.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
    r6.w = 0.0833333358;
    r5.xyzw = r1.wwww * r6.xyzw + r5.xyzw;

    //sample 7 8
    // r6.xyzw = r3.zwzw * float4(0.608695626,0.608695626,-0.608695626,-0.608695626) + r2.zwzw;
    {
      const float a = 0.608695626 * scale; // 1/1.642857
      r6.xyzw = r3.zwzw * float4(a, a, -a, -a) + r2.zwzw;
    }


    r7.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
    r8.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
    r7.xy = r7.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r7.xy, r7.xy);
    r4.x = sqrt(r1.w);
    r7.xy = r8.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r7.xy, r7.xy);
    r4.z = sqrt(r1.w);
    r1.w = r7.z * r7.z;
    r7.x = r8.z * r8.z;
    r1.w = r1.w * 8192 + -r1.z;

    r7.yw = saturate(r1.ww * float2(1,-1) + float2(0.5,0.5));
    r9.xyzw = saturate(r1.yyyy * r4.yxyz + float4(-2.49999976,-2.49999976,-2.49999976,-2.49999976));
    r1.w = dot(r7.yw, r9.xy);
    r7.x = r7.x * 8192 + -r1.z;
    r7.xy = saturate(r7.xx * float2(1,-1) + float2(0.5,0.5));
    r7.x = dot(r7.xy, r9.zw);
    r7.y = cmp(r8.z < r7.z);
    r7.z = cmp(r4.x < r4.z);
    r7.w = r7.z ? r7.y : 0;
    r8.x = r7.w ? r7.x : r1.w;
    r7.y = (int)r7.z | (int)r7.y;
    r7.y = (int)r7.y | (int)r7.w;
    r1.w = r7.y ? r7.x : r1.w;

    r7.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
    r7.w = 0.0833333358;
    r5.xyzw = r8.xxxx * r7.xyzw + r5.xyzw;

    r6.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
    r6.w = 0.0833333358;
    r5.xyzw = r1.wwww * r6.xyzw + r5.xyzw;

    //sample 9 10
    // r6.xyzw = r3.zwzw * float4(0.782608688,0.782608688,-0.782608688,-0.782608688) + r2.zwzw;
    {
      const float a = 0.782608688 * scale; // 1/1.277777
      r6.xyzw = r3.zwzw * float4(a, a, -a, -a) + r2.zwzw;
    }

    r7.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
    r8.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
    r7.xy = r7.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r7.xy, r7.xy);
    r4.w = sqrt(r1.w);
    r7.xy = r8.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r7.xy, r7.xy);
    r4.x = sqrt(r1.w);
    r1.w = r7.z * r7.z;
    r7.x = r8.z * r8.z;
    r1.w = r1.w * 8192 + -r1.z;

    r7.yw = saturate(r1.ww * float2(1,-1) + float2(0.5,0.5));
    r8.xy = saturate(r1.yy * r4.yw + float2(-3.5,-3.5));
    r1.w = dot(r7.yw, r8.xy);
    r7.x = r7.x * 8192 + -r1.z;
    
    r7.xy = saturate(r7.xx * float2(1,-1) + float2(0.5,0.5));
    r8.xy = saturate(r1.yy * r4.yx + float2(-3.5,-3.5));
    r7.x = dot(r7.xy, r8.xy);
    r7.y = cmp(r8.z < r7.z);
    r4.x = cmp(r4.w < r4.x);
    r7.z = r4.x ? r7.y : 0;
    r7.w = r7.z ? r7.x : r1.w;
    r4.x = (int)r4.x | (int)r7.y;
    r4.x = (int)r4.x | (int)r7.z;
    r1.w = r4.x ? r7.x : r1.w;

    r8.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.xy, 1).xyz;
    r8.w = 0.0833333358;
    r5.xyzw = r7.wwww * r8.xyzw + r5.xyzw;

    r6.xyz = colorMapSampler.SampleLevel(pointClamp_s, r6.zw, 1).xyz;
    r6.w = 0.0833333358;
    r5.xyzw = r1.wwww * r6.xyzw + r5.xyzw;

    //sample 11 12
    // r2.xyzw = r3.zwzw * float4(0.956521749,0.956521749,-0.956521749,-0.956521749) + r2.xyzw;
    {
      const float a = 0.956521749 * scale; // 1/1.044444
      r2.xyzw = r3.zwzw * float4(a, a, -a, -a) + r2.xyzw;
    }

    r6.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r2.xy, 1).xyz;
    r7.xyz = sceneVelocitySampler.SampleLevel(pointClamp_s, r2.zw, 1).xyz;
    r6.xy = r6.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r6.xy, r6.xy);
    r4.z = sqrt(r1.w);
    r6.xy = r7.xy * float2(127.5,127.5) + float2(-64,-64);
    r1.w = dot(r6.xy, r6.xy);
    r4.w = sqrt(r1.w);
    r1.w = r6.z * r6.z;
    r4.x = r7.z * r7.z;
    r1.w = r1.w * 8192 + -r1.z;
    r6.xy = saturate(r1.ww * float2(1,-1) + float2(0.5,0.5));
    r8.xyzw = saturate(r1.yyyy * r4.yzyw + float4(-4.5,-4.5,-4.5,-4.5));
    r1.y = dot(r6.xy, r8.xy);
    r1.z = r4.x * 8192 + -r1.z;
    r1.zw = saturate(r1.zz * float2(1,-1) + float2(0.5,0.5));
    r1.z = dot(r1.zw, r8.zw);
    r1.w = cmp(r7.z < r6.z);
    r4.x = cmp(r4.z < r4.w);
    r4.y = r1.w ? r4.x : 0;
    r4.z = r4.y ? r1.z : r1.y;
    r1.w = (int)r1.w | (int)r4.x;
    r1.w = (int)r1.w | (int)r4.y;
    r1.y = r1.w ? r1.z : r1.y;

    r6.xyz = colorMapSampler.SampleLevel(pointClamp_s, r2.xy, 1).xyz;
    r6.w = 0.0833333358;
    r4.xyzw = r4.zzzz * r6.xyzw + r5.xyzw;

    r2.xyz = colorMapSampler.SampleLevel(pointClamp_s, r2.zw, 1).xyz;
    r2.w = 0.0833333358;
    r2.xyzw = r1.yyyy * r2.xyzw + r4.xyzw;

    //final
    r1.y = 1 + -r2.w;
    r1.yzw = r1.yyy * r0.xyz;
    r0.xyz = r2.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r1.yzw; // 1/12
  */

  } else {
    //whatever this is, it's hard to find its case.

    r1.xyzw = r3.zwzw * r1.xxxx;
    r1.xyzw = r1.xyzw * float4(0.173913047,0.173913047,0.173913047,0.173913047) + v1.xyxy;
    r2.xyzw = r3.zwzw * float4(-0.956521749,-0.956521749,-0.782608688,-0.782608688) + r1.zwzw;
    r4.xyz = colorMapSampler.SampleLevel(linearClamp_s, r2.xy, 1).xyz;
    r2.xyz = colorMapSampler.SampleLevel(linearClamp_s, r2.zw, 1).xyz;
    r2.xyz = float3(0.0833333358,0.0833333358,0.0833333358) * r2.xyz;
    r2.xyz = r4.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r4.xyzw = r3.zwzw * float4(-0.608695626,-0.608695626,-0.434782624,-0.434782624) + r1.zwzw;
    r5.xyz = colorMapSampler.SampleLevel(linearClamp_s, r4.xy, 1).xyz;
    r2.xyz = r5.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r4.xyz = colorMapSampler.SampleLevel(linearClamp_s, r4.zw, 1).xyz;
    r2.xyz = r4.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r4.xyzw = r3.zwzw * float4(-0.260869563,-0.260869563,-0.086956501,-0.086956501) + r1.zwzw;
    r5.xyz = colorMapSampler.SampleLevel(linearClamp_s, r4.xy, 1).xyz;
    r2.xyz = r5.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r4.xyz = colorMapSampler.SampleLevel(linearClamp_s, r4.zw, 1).xyz;
    r2.xyz = r4.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r4.xyzw = r3.zwzw * float4(0.086956501,0.086956501,0.260869622,0.260869622) + r1.zwzw;
    r5.xyz = colorMapSampler.SampleLevel(linearClamp_s, r4.xy, 1).xyz;
    r2.xyz = r5.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r4.xyz = colorMapSampler.SampleLevel(linearClamp_s, r4.zw, 1).xyz;
    r2.xyz = r4.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r4.xyzw = r3.zwzw * float4(0.434782624,0.434782624,0.608695626,0.608695626) + r1.zwzw;
    r5.xyz = colorMapSampler.SampleLevel(linearClamp_s, r4.xy, 1).xyz;
    r2.xyz = r5.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r4.xyz = colorMapSampler.SampleLevel(linearClamp_s, r4.zw, 1).xyz;
    r2.xyz = r4.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r1.xyzw = r3.xyzw * float4(0.782608747,0.782608747,0.956521749,0.956521749) + r1.xyzw;
    r3.xyz = colorMapSampler.SampleLevel(linearClamp_s, r1.xy, 1).xyz;
    r2.xyz = r3.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
    r1.xyz = colorMapSampler.SampleLevel(linearClamp_s, r1.zw, 1).xyz;
    r0.xyz = r1.xyz * float3(0.0833333358,0.0833333358,0.0833333358) + r2.xyz;
  }

  o0.xyzw = r0.xyzw;
  return;
}

/*

0x00000000: ps_5_0
      0x00000008: dcl_globalFlags refactoringAllowed
      0x0000000C: dcl_constantbuffer CB1[45], immediateIndexed
      0x0000001C: dcl_sampler s1, mode_default
      0x00000028: dcl_sampler s2, mode_default
      0x00000034: dcl_resource_texture2d (float,float,float,float) t0
      0x00000044: dcl_resource_texture2d (float,float,float,float) t6
      0x00000054: dcl_resource_texture2d (float,float,float,float) t7
      0x00000064: dcl_input_ps linear v1.xy
      0x00000070: dcl_output o0.xyzw
      0x0000007C: dcl_temps 10
   0  0x00000084: sample_indexable(texture2d)(float,float,float,float) r0.xyzw, v1.xyxx, t0.xyzw, s1
   1  0x000000B0: mul r1.xy, v1.xyxx, cb1[44].xyxx
   2  0x000000D0: ftou r1.xy, r1.xyxx
   3  0x000000E4: and r1.xy, r1.xyxx, l(1, 1, 0, 0)
   4  0x0000010C: utof r1.xy, r1.xyxx
   5  0x00000120: mad r1.x, r1.x, l(0.500000), l(-0.250000)
   6  0x00000144: mad r1.y, r1.y, l(2.000000), l(-1.000000)
   7  0x00000168: mul r1.x, r1.y, r1.x
   8  0x00000184: mul r1.yz, r1.xxxx, cb1[44].zzwz
   9  0x000001A4: mad r1.yz, r1.yyzy, l(0.000000, 20.000000, 20.000000, 0.000000), v1.xxyx
  10  0x000001D4: sample_indexable(texture2d)(float,float,float,float) r1.yzw, r1.yzyy, t7.wxyz, s1
  11  0x00000200: lt r2.x, r1.w, l(0.003922)
  12  0x0000021C: if_nz r2.x
  13  0x00000228:   mov o0.xyzw, r0.xyzw
  14  0x0000023C:   ret 
  15  0x00000240: endif 
  16  0x00000244: sample_indexable(texture2d)(float,float,float,float) r2.x, v1.xyxx, t7.xyzw, s1
  17  0x00000270: mad r3.xyzw, r1.yzyz, l(127.500000, 127.500000, 127.500000, 127.500000), l(-64.000000, -64.000000, -64.000000, -64.000000)
  18  0x000002AC: round_ne r3.xyzw, r3.xyzw
  19  0x000002C0: mul r3.xyzw, r3.xyzw, cb1[44].zwzw
  20  0x000002E0: ne r1.y, l(0.000000, 0.000000, 0.000000, 0.000000), r2.x
  21  0x00000308: if_nz r1.y
  22  0x00000314:   mul r1.y, r1.w, l(255.000000)
  23  0x00000330:   div r1.y, l(5.750000), r1.y
  24  0x0000034C:   sample_l_indexable(texture2d)(float,float,float,float) r2.xyz, v1.xyxx, t6.xyzw, s1, l(1.000000)
  25  0x00000380:   mad r1.zw, r2.xxxy, l(0.000000, 0.000000, 127.500000, 127.500000), l(0.000000, 0.000000, -64.000000, -64.000000)
  26  0x000003BC:   dp2 r1.z, r1.zwzz, r1.zwzz
  27  0x000003D8:   sqrt r4.y, r1.z
  28  0x000003EC:   mul r1.z, r2.z, r2.z
  29  0x00000408:   mul r1.z, r1.z, l(8192.000000)
  30  0x00000424:   mul r2.xyzw, r1.xxxx, r3.zwzw
  31  0x00000440:   mad r2.xyzw, r2.xyzw, l(0.173913, 0.173913, 0.173913, 0.173913), v1.xyxy
  32  0x00000470:   mad r5.xyzw, r3.zwzw, l(0.086957, 0.086957, -0.086957, -0.086957), r2.zwzw
  33  0x000004A0:   sample_l_indexable(texture2d)(float,float,float,float) r6.xyz, r5.xyxx, t6.xyzw, s1, l(1.000000)
  34  0x000004D4:   sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r5.zwzz, t6.xyzw, s1, l(1.000000)
  35  0x00000508:   mad r6.xy, r6.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
  36  0x00000544:   dp2 r1.w, r6.xyxx, r6.xyxx
  37  0x00000560:   sqrt r4.x, r1.w
  38  0x00000574:   mad r6.xy, r7.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
  39  0x000005B0:   dp2 r1.w, r6.xyxx, r6.xyxx
  40  0x000005CC:   sqrt r4.z, r1.w
  41  0x000005E0:   mul r1.w, r6.z, r6.z
  42  0x000005FC:   mul r6.x, r7.z, r7.z
  43  0x00000618:   mad r1.w, r1.w, l(8192.000000), -r1.z
  44  0x00000640:   mad_sat r6.yw, r1.wwww, l(0.000000, 1.000000, 0.000000, -1.000000), l(0.000000, 0.500000, 0.000000, 0.500000)
  45  0x0000067C:   mul_sat r8.xyzw, r1.yyyy, r4.yxyz
  46  0x00000698:   dp2 r1.w, r6.ywyy, r8.xyxx
  47  0x000006B4:   mad r6.x, r6.x, l(8192.000000), -r1.z
  48  0x000006DC:   mad_sat r6.xy, r6.xxxx, l(1.000000, -1.000000, 0.000000, 0.000000), l(0.500000, 0.500000, 0.000000, 0.000000)
  49  0x00000718:   dp2 r6.x, r6.xyxx, r8.zwzz
  50  0x00000734:   lt r6.y, r7.z, r6.z
  51  0x00000750:   lt r6.z, r4.x, r4.z
  52  0x0000076C:   and r6.w, r6.z, r6.y
  53  0x00000788:   movc r7.x, r6.w, r6.x, r1.w
  54  0x000007AC:   or r6.y, r6.z, r6.y
  55  0x000007C8:   or r6.y, r6.y, r6.w
  56  0x000007E4:   movc r1.w, r6.y, r6.x, r1.w
  57  0x00000808:   sample_l_indexable(texture2d)(float,float,float,float) r6.xyz, r5.xyxx, t0.xyzw, s1, l(1.000000)
  58  0x0000083C:   mov r6.w, l(0.083333)
  59  0x00000850:   sample_l_indexable(texture2d)(float,float,float,float) r5.xyz, r5.zwzz, t0.xyzw, s1, l(1.000000)
  60  0x00000884:   mov r5.w, l(0.083333)
  61  0x00000898:   mul r5.xyzw, r1.wwww, r5.xyzw
  62  0x000008B4:   mad r5.xyzw, r7.xxxx, r6.xyzw, r5.xyzw
  63  0x000008D8:   mad r6.xyzw, r3.zwzw, l(0.260870, 0.260870, -0.260870, -0.260870), r2.zwzw
  64  0x00000908:   sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r6.xyxx, t6.xyzw, s1, l(1.000000)
  65  0x0000093C:   sample_l_indexable(texture2d)(float,float,float,float) r8.xyz, r6.zwzz, t6.xyzw, s1, l(1.000000)
  66  0x00000970:   mad r7.xy, r7.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
  67  0x000009AC:   dp2 r1.w, r7.xyxx, r7.xyxx
  68  0x000009C8:   sqrt r4.w, r1.w
  69  0x000009DC:   mad r7.xy, r8.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
  70  0x00000A18:   dp2 r1.w, r7.xyxx, r7.xyxx
  71  0x00000A34:   sqrt r4.x, r1.w
  72  0x00000A48:   mul r1.w, r7.z, r7.z
  73  0x00000A64:   mul r7.x, r8.z, r8.z
  74  0x00000A80:   mad r1.w, r1.w, l(8192.000000), -r1.z
  75  0x00000AA8:   mad_sat r7.yw, r1.wwww, l(0.000000, 1.000000, 0.000000, -1.000000), l(0.000000, 0.500000, 0.000000, 0.500000)
  76  0x00000AE4:   mad_sat r8.xy, r1.yyyy, r4.ywyy, l(-0.500000, -0.500000, 0.000000, 0.000000)
  77  0x00000B14:   dp2 r1.w, r7.ywyy, r8.xyxx
  78  0x00000B30:   mad r7.x, r7.x, l(8192.000000), -r1.z
  79  0x00000B58:   mad_sat r7.xy, r7.xxxx, l(1.000000, -1.000000, 0.000000, 0.000000), l(0.500000, 0.500000, 0.000000, 0.000000)
  80  0x00000B94:   mad_sat r8.xy, r1.yyyy, r4.yxyy, l(-0.500000, -0.500000, 0.000000, 0.000000)
  81  0x00000BC4:   dp2 r7.x, r7.xyxx, r8.xyxx
  82  0x00000BE0:   lt r7.y, r8.z, r7.z
  83  0x00000BFC:   lt r7.z, r4.w, r4.x
  84  0x00000C18:   and r7.w, r7.z, r7.y
  85  0x00000C34:   movc r8.x, r7.w, r7.x, r1.w
  86  0x00000C58:   or r7.y, r7.z, r7.y
  87  0x00000C74:   or r7.y, r7.y, r7.w
  88  0x00000C90:   movc r1.w, r7.y, r7.x, r1.w
  89  0x00000CB4:   sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r6.xyxx, t0.xyzw, s1, l(1.000000)
  90  0x00000CE8:   mov r7.w, l(0.083333)
  91  0x00000CFC:   mad r5.xyzw, r8.xxxx, r7.xyzw, r5.xyzw
  92  0x00000D20:   sample_l_indexable(texture2d)(float,float,float,float) r6.xyz, r6.zwzz, t0.xyzw, s1, l(1.000000)
  93  0x00000D54:   mov r6.w, l(0.083333)
  94  0x00000D68:   mad r5.xyzw, r1.wwww, r6.xyzw, r5.xyzw
  95  0x00000D8C:   mad r6.xyzw, r3.zwzw, l(0.434783, 0.434783, -0.434783, -0.434783), r2.zwzw
  96  0x00000DBC:   sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r6.xyxx, t6.xyzw, s1, l(1.000000)
  97  0x00000DF0:   sample_l_indexable(texture2d)(float,float,float,float) r8.xyz, r6.zwzz, t6.xyzw, s1, l(1.000000)
  98  0x00000E24:   mad r7.xy, r7.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
  99  0x00000E60:   dp2 r1.w, r7.xyxx, r7.xyxx
 100  0x00000E7C:   sqrt r4.z, r1.w
 101  0x00000E90:   mad r7.xy, r8.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
 102  0x00000ECC:   dp2 r1.w, r7.xyxx, r7.xyxx
 103  0x00000EE8:   sqrt r4.w, r1.w
 104  0x00000EFC:   mul r1.w, r7.z, r7.z
 105  0x00000F18:   mul r7.x, r8.z, r8.z
 106  0x00000F34:   mad r1.w, r1.w, l(8192.000000), -r1.z
 107  0x00000F5C:   mad_sat r7.yw, r1.wwww, l(0.000000, 1.000000, 0.000000, -1.000000), l(0.000000, 0.500000, 0.000000, 0.500000)
 108  0x00000F98:   mad_sat r9.xyzw, r1.yyyy, r4.yzyw, l(-1.500000, -1.500000, -1.500000, -1.500000)
 109  0x00000FC8:   dp2 r1.w, r7.ywyy, r9.xyxx
 110  0x00000FE4:   mad r7.x, r7.x, l(8192.000000), -r1.z
 111  0x0000100C:   mad_sat r7.xy, r7.xxxx, l(1.000000, -1.000000, 0.000000, 0.000000), l(0.500000, 0.500000, 0.000000, 0.000000)
 112  0x00001048:   dp2 r7.x, r7.xyxx, r9.zwzz
 113  0x00001064:   lt r7.y, r8.z, r7.z
 114  0x00001080:   lt r7.z, r4.z, r4.w
 115  0x0000109C:   and r7.w, r7.z, r7.y
 116  0x000010B8:   movc r8.x, r7.w, r7.x, r1.w
 117  0x000010DC:   or r7.y, r7.z, r7.y
 118  0x000010F8:   or r7.y, r7.y, r7.w
 119  0x00001114:   movc r1.w, r7.y, r7.x, r1.w
 120  0x00001138:   sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r6.xyxx, t0.xyzw, s1, l(1.000000)
 121  0x0000116C:   mov r7.w, l(0.083333)
 122  0x00001180:   mad r5.xyzw, r8.xxxx, r7.xyzw, r5.xyzw
 123  0x000011A4:   sample_l_indexable(texture2d)(float,float,float,float) r6.xyz, r6.zwzz, t0.xyzw, s1, l(1.000000)
 124  0x000011D8:   mov r6.w, l(0.083333)
 125  0x000011EC:   mad r5.xyzw, r1.wwww, r6.xyzw, r5.xyzw
 126  0x00001210:   mad r6.xyzw, r3.zwzw, l(0.608696, 0.608696, -0.608696, -0.608696), r2.zwzw
 127  0x00001240:   sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r6.xyxx, t6.xyzw, s1, l(1.000000)
 128  0x00001274:   sample_l_indexable(texture2d)(float,float,float,float) r8.xyz, r6.zwzz, t6.xyzw, s1, l(1.000000)
 129  0x000012A8:   mad r7.xy, r7.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
 130  0x000012E4:   dp2 r1.w, r7.xyxx, r7.xyxx
 131  0x00001300:   sqrt r4.x, r1.w
 132  0x00001314:   mad r7.xy, r8.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
 133  0x00001350:   dp2 r1.w, r7.xyxx, r7.xyxx
 134  0x0000136C:   sqrt r4.z, r1.w
 135  0x00001380:   mul r1.w, r7.z, r7.z
 136  0x0000139C:   mul r7.x, r8.z, r8.z
 137  0x000013B8:   mad r1.w, r1.w, l(8192.000000), -r1.z
 138  0x000013E0:   mad_sat r7.yw, r1.wwww, l(0.000000, 1.000000, 0.000000, -1.000000), l(0.000000, 0.500000, 0.000000, 0.500000)
 139  0x0000141C:   mad_sat r9.xyzw, r1.yyyy, r4.yxyz, l(-2.500000, -2.500000, -2.500000, -2.500000)
 140  0x0000144C:   dp2 r1.w, r7.ywyy, r9.xyxx
 141  0x00001468:   mad r7.x, r7.x, l(8192.000000), -r1.z
 142  0x00001490:   mad_sat r7.xy, r7.xxxx, l(1.000000, -1.000000, 0.000000, 0.000000), l(0.500000, 0.500000, 0.000000, 0.000000)
 143  0x000014CC:   dp2 r7.x, r7.xyxx, r9.zwzz
 144  0x000014E8:   lt r7.y, r8.z, r7.z
 145  0x00001504:   lt r7.z, r4.x, r4.z
 146  0x00001520:   and r7.w, r7.z, r7.y
 147  0x0000153C:   movc r8.x, r7.w, r7.x, r1.w
 148  0x00001560:   or r7.y, r7.z, r7.y
 149  0x0000157C:   or r7.y, r7.y, r7.w
 150  0x00001598:   movc r1.w, r7.y, r7.x, r1.w
 151  0x000015BC:   sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r6.xyxx, t0.xyzw, s1, l(1.000000)
 152  0x000015F0:   mov r7.w, l(0.083333)
 153  0x00001604:   mad r5.xyzw, r8.xxxx, r7.xyzw, r5.xyzw
 154  0x00001628:   sample_l_indexable(texture2d)(float,float,float,float) r6.xyz, r6.zwzz, t0.xyzw, s1, l(1.000000)
 155  0x0000165C:   mov r6.w, l(0.083333)
 156  0x00001670:   mad r5.xyzw, r1.wwww, r6.xyzw, r5.xyzw
 157  0x00001694:   mad r6.xyzw, r3.zwzw, l(0.782609, 0.782609, -0.782609, -0.782609), r2.zwzw
 158  0x000016C4:   sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r6.xyxx, t6.xyzw, s1, l(1.000000)
 159  0x000016F8:   sample_l_indexable(texture2d)(float,float,float,float) r8.xyz, r6.zwzz, t6.xyzw, s1, l(1.000000)
 160  0x0000172C:   mad r7.xy, r7.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
 161  0x00001768:   dp2 r1.w, r7.xyxx, r7.xyxx
 162  0x00001784:   sqrt r4.w, r1.w
 163  0x00001798:   mad r7.xy, r8.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
 164  0x000017D4:   dp2 r1.w, r7.xyxx, r7.xyxx
 165  0x000017F0:   sqrt r4.x, r1.w
 166  0x00001804:   mul r1.w, r7.z, r7.z
 167  0x00001820:   mul r7.x, r8.z, r8.z
 168  0x0000183C:   mad r1.w, r1.w, l(8192.000000), -r1.z
 169  0x00001864:   mad_sat r7.yw, r1.wwww, l(0.000000, 1.000000, 0.000000, -1.000000), l(0.000000, 0.500000, 0.000000, 0.500000)
 170  0x000018A0:   mad_sat r8.xy, r1.yyyy, r4.ywyy, l(-3.500000, -3.500000, 0.000000, 0.000000)
 171  0x000018D0:   dp2 r1.w, r7.ywyy, r8.xyxx
 172  0x000018EC:   mad r7.x, r7.x, l(8192.000000), -r1.z
 173  0x00001914:   mad_sat r7.xy, r7.xxxx, l(1.000000, -1.000000, 0.000000, 0.000000), l(0.500000, 0.500000, 0.000000, 0.000000)
 174  0x00001950:   mad_sat r8.xy, r1.yyyy, r4.yxyy, l(-3.500000, -3.500000, 0.000000, 0.000000)
 175  0x00001980:   dp2 r7.x, r7.xyxx, r8.xyxx
 176  0x0000199C:   lt r7.y, r8.z, r7.z
 177  0x000019B8:   lt r4.x, r4.w, r4.x
 178  0x000019D4:   and r7.z, r4.x, r7.y
 179  0x000019F0:   movc r7.w, r7.z, r7.x, r1.w
 180  0x00001A14:   or r4.x, r4.x, r7.y
 181  0x00001A30:   or r4.x, r4.x, r7.z
 182  0x00001A4C:   movc r1.w, r4.x, r7.x, r1.w
 183  0x00001A70:   sample_l_indexable(texture2d)(float,float,float,float) r8.xyz, r6.xyxx, t0.xyzw, s1, l(1.000000)
 184  0x00001AA4:   mov r8.w, l(0.083333)
 185  0x00001AB8:   mad r5.xyzw, r7.wwww, r8.xyzw, r5.xyzw
 186  0x00001ADC:   sample_l_indexable(texture2d)(float,float,float,float) r6.xyz, r6.zwzz, t0.xyzw, s1, l(1.000000)
 187  0x00001B10:   mov r6.w, l(0.083333)
 188  0x00001B24:   mad r5.xyzw, r1.wwww, r6.xyzw, r5.xyzw
 189  0x00001B48:   mad r2.xyzw, r3.zwzw, l(0.956522, 0.956522, -0.956522, -0.956522), r2.xyzw
 190  0x00001B78:   sample_l_indexable(texture2d)(float,float,float,float) r6.xyz, r2.xyxx, t6.xyzw, s1, l(1.000000)
 191  0x00001BAC:   sample_l_indexable(texture2d)(float,float,float,float) r7.xyz, r2.zwzz, t6.xyzw, s1, l(1.000000)
 192  0x00001BE0:   mad r6.xy, r6.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
 193  0x00001C1C:   dp2 r1.w, r6.xyxx, r6.xyxx
 194  0x00001C38:   sqrt r4.z, r1.w
 195  0x00001C4C:   mad r6.xy, r7.xyxx, l(127.500000, 127.500000, 0.000000, 0.000000), l(-64.000000, -64.000000, 0.000000, 0.000000)
 196  0x00001C88:   dp2 r1.w, r6.xyxx, r6.xyxx
 197  0x00001CA4:   sqrt r4.w, r1.w
 198  0x00001CB8:   mul r1.w, r6.z, r6.z
 199  0x00001CD4:   mul r4.x, r7.z, r7.z
 200  0x00001CF0:   mad r1.w, r1.w, l(8192.000000), -r1.z
 201  0x00001D18:   mad_sat r6.xy, r1.wwww, l(1.000000, -1.000000, 0.000000, 0.000000), l(0.500000, 0.500000, 0.000000, 0.000000)
 202  0x00001D54:   mad_sat r8.xyzw, r1.yyyy, r4.yzyw, l(-4.500000, -4.500000, -4.500000, -4.500000)
 203  0x00001D84:   dp2 r1.y, r6.xyxx, r8.xyxx
 204  0x00001DA0:   mad r1.z, r4.x, l(8192.000000), -r1.z
 205  0x00001DC8:   mad_sat r1.zw, r1.zzzz, l(0.000000, 0.000000, 1.000000, -1.000000), l(0.000000, 0.000000, 0.500000, 0.500000)
 206  0x00001E04:   dp2 r1.z, r1.zwzz, r8.zwzz
 207  0x00001E20:   lt r1.w, r7.z, r6.z
 208  0x00001E3C:   lt r4.x, r4.z, r4.w
 209  0x00001E58:   and r4.y, r1.w, r4.x
 210  0x00001E74:   movc r4.z, r4.y, r1.z, r1.y
 211  0x00001E98:   or r1.w, r1.w, r4.x
 212  0x00001EB4:   or r1.w, r1.w, r4.y
 213  0x00001ED0:   movc r1.y, r1.w, r1.z, r1.y
 214  0x00001EF4:   sample_l_indexable(texture2d)(float,float,float,float) r6.xyz, r2.xyxx, t0.xyzw, s1, l(1.000000)
 215  0x00001F28:   mov r6.w, l(0.083333)
 216  0x00001F3C:   mad r4.xyzw, r4.zzzz, r6.xyzw, r5.xyzw
 217  0x00001F60:   sample_l_indexable(texture2d)(float,float,float,float) r2.xyz, r2.zwzz, t0.xyzw, s1, l(1.000000)
 218  0x00001F94:   mov r2.w, l(0.083333)
 219  0x00001FA8:   mad r2.xyzw, r1.yyyy, r2.xyzw, r4.xyzw
 220  0x00001FCC:   add r1.y, -r2.w, l(1.000000)
 221  0x00001FEC:   mul r1.yzw, r0.xxyz, r1.yyyy
 222  0x00002008:   mad r0.xyz, r2.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r1.yzwy
 223  0x00002038: else 
 224  0x0000203C:   mul r1.xyzw, r1.xxxx, r3.zwzw
 225  0x00002058:   mad r1.xyzw, r1.xyzw, l(0.173913, 0.173913, 0.173913, 0.173913), v1.xyxy
 226  0x00002088:   mad r2.xyzw, r3.zwzw, l(-0.956522, -0.956522, -0.782609, -0.782609), r1.zwzw
 227  0x000020B8:   sample_l_indexable(texture2d)(float,float,float,float) r4.xyz, r2.xyxx, t0.xyzw, s2, l(1.000000)
 228  0x000020EC:   sample_l_indexable(texture2d)(float,float,float,float) r2.xyz, r2.zwzz, t0.xyzw, s2, l(1.000000)
 229  0x00002120:   mul r2.xyz, r2.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000)
 230  0x00002148:   mad r2.xyz, r4.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 231  0x00002178:   mad r4.xyzw, r3.zwzw, l(-0.608696, -0.608696, -0.434783, -0.434783), r1.zwzw
 232  0x000021A8:   sample_l_indexable(texture2d)(float,float,float,float) r5.xyz, r4.xyxx, t0.xyzw, s2, l(1.000000)
 233  0x000021DC:   mad r2.xyz, r5.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 234  0x0000220C:   sample_l_indexable(texture2d)(float,float,float,float) r4.xyz, r4.zwzz, t0.xyzw, s2, l(1.000000)
 235  0x00002240:   mad r2.xyz, r4.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 236  0x00002270:   mad r4.xyzw, r3.zwzw, l(-0.260870, -0.260870, -0.086957, -0.086957), r1.zwzw
 237  0x000022A0:   sample_l_indexable(texture2d)(float,float,float,float) r5.xyz, r4.xyxx, t0.xyzw, s2, l(1.000000)
 238  0x000022D4:   mad r2.xyz, r5.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 239  0x00002304:   sample_l_indexable(texture2d)(float,float,float,float) r4.xyz, r4.zwzz, t0.xyzw, s2, l(1.000000)
 240  0x00002338:   mad r2.xyz, r4.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 241  0x00002368:   mad r4.xyzw, r3.zwzw, l(0.086957, 0.086957, 0.260870, 0.260870), r1.zwzw
 242  0x00002398:   sample_l_indexable(texture2d)(float,float,float,float) r5.xyz, r4.xyxx, t0.xyzw, s2, l(1.000000)
 243  0x000023CC:   mad r2.xyz, r5.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 244  0x000023FC:   sample_l_indexable(texture2d)(float,float,float,float) r4.xyz, r4.zwzz, t0.xyzw, s2, l(1.000000)
 245  0x00002430:   mad r2.xyz, r4.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 246  0x00002460:   mad r4.xyzw, r3.zwzw, l(0.434783, 0.434783, 0.608696, 0.608696), r1.zwzw
 247  0x00002490:   sample_l_indexable(texture2d)(float,float,float,float) r5.xyz, r4.xyxx, t0.xyzw, s2, l(1.000000)
 248  0x000024C4:   mad r2.xyz, r5.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 249  0x000024F4:   sample_l_indexable(texture2d)(float,float,float,float) r4.xyz, r4.zwzz, t0.xyzw, s2, l(1.000000)
 250  0x00002528:   mad r2.xyz, r4.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 251  0x00002558:   mad r1.xyzw, r3.xyzw, l(0.782609, 0.782609, 0.956522, 0.956522), r1.xyzw
 252  0x00002588:   sample_l_indexable(texture2d)(float,float,float,float) r3.xyz, r1.xyxx, t0.xyzw, s2, l(1.000000)
 253  0x000025BC:   mad r2.xyz, r3.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 254  0x000025EC:   sample_l_indexable(texture2d)(float,float,float,float) r1.xyz, r1.zwzz, t0.xyzw, s2, l(1.000000)
 255  0x00002620:   mad r0.xyz, r1.xyzx, l(0.083333, 0.083333, 0.083333, 0.000000), r2.xyzx
 256  0x00002650: endif 
 257  0x00002654: mov o0.xyzw, r0.xyzw
 258  0x00002668: ret 

*/