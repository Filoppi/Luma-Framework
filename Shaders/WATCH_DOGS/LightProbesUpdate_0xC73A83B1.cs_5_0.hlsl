cbuffer LightProbesUpdate : register(b0)
{
  float4 _BlockSize : packoffset(c0);//                 Index:    0.xyzw           Components:     4
  float4x4 _LocalToWorldMatrix : packoffset(c1);//      Index:    1 2 3 4          Components:    16
  float4 _SkyBounceB : packoffset(c5);//                Index:    5.xyzw           Components:     4
  float4 _SkyBounceG : packoffset(c6);//                Index:    6.xyzw           Components:     4
  float4 _SkyBounceR : packoffset(c7);//                Index:    7.xyzw           Components:     4
  float4 _SkyDirectB : packoffset(c8);//                Index:    8.xyzw           Components:     4
  float4 _SkyDirectG : packoffset(c9);//                Index:    9.xyzw           Components:     4
  float4 _SkyDirectR : packoffset(c10);//                Index:   10.xyzw           Components:     4
  float4 _SunBounceB : packoffset(c11);//                Index:   11.xyzw           Components:     4
  float4 _SunBounceG : packoffset(c12);//                Index:   12.xyzw           Components:     4
  float4 _SunBounceR : packoffset(c13);//                Index:   13.xyzw           Components:     4
  float4 _TextureSize : packoffset(c14);//               Index:   14.xyzw           Components:     4
  float3 _SunDirection : packoffset(c15);//              Index:   15.xyz            Components:     3
  float _NonLinearHeightRange : packoffset(c15.w);//       Index:   15.w              Components:     1
  float3 _SunShadowRightVec : packoffset(c16);//         Index:   16.xyz            Components:     3
  float _NumZNonLinearSlices : packoffset(c16.w);//        Index:   16.w              Components:     1
  float3 _SunShadowUpVec : packoffset(c17);//            Index:   17.xyz            Components:     3
  float _ScaleFactor : packoffset(c17.w);//                Index:   17.w              Components:     1
  float2 _LocalLightsMultipliers : packoffset(c18);//    Index:   18.xy             Components:     2
  float _VolumeHeight : packoffset(c18.z);//               Index:   18.z              Components:     1 [unused]
  float _ZDistributionPower : packoffset(c18.w);//         Index:   18.w              Components:     1
  float _ZLinearSpacing : packoffset(c19);//             Index:   19.x              Components:     1
}

cbuffer PreciseElectricPower : register(b1)
{
  float4 _ElectricPowerCentreX : packoffset(c0);//      Index:    0.xyzw           Components:     4
  float4 _ElectricPowerCentreY : packoffset(c1);//      Index:    1.xyzw           Components:     4
  float4 _ElectricPowerFailureRadius : packoffset(c2);//Index:    2.xyzw           Components:     4
  float4 _ElectricPowerReturnRadius : packoffset(c3);// Index:    3.xyzw           Components:     4
  float4 _ElectricPowerSwitchDistance : packoffset(c4);//Index:    4.xyzw           Components:     4
  float _ElectricPowerBlockSize : packoffset(c5);//     Index:    5.x              Components:     1
  float _ElectricPowerNumActiveRegions : packoffset(c5.y);//Index:    5.y              Components:     1
}

// Shader Option Defines
//#define SUNSHADOW
#define ELECTRIC_POWER
//#define INTERIOR

// Number of radiance transfer basis functions
#define RADIANCE_TRANSFER_BASIS_COUNT 4

// Size, on each dimension, of the block of probes processed by each thread group in the compute shader
#define CLightProbeRenderer_ms_computeShaderBlockSize   4 // IMPORTANT: Must match CLightProbeRenderer::ms_computeShaderBlockSize

#ifdef ELECTRIC_POWER
// TODO_LM_IMPROVE: AVOID DOING THIS CALCULATION FOR EVERY PROBE, JUST DO SNAPPED INTERPOLATION
float2 GetBlackoutClampedPosition( const float2 entityPosXY )
{
    return ( floor( entityPosXY / _ElectricPowerBlockSize ) + 0.5f ) * _ElectricPowerBlockSize;
}
#endif// def ELECTRIC_POWER

struct SIntPair
{
    int    lowBits;
    int    highBits;
};

struct SUIntPair
{
    uint    lowBits;
    uint    highBits;
};

// Probe data used for relighting (update of the 3D textures) in the compute shader.
// Must match version in SceneLightProbesPrivateData.cpp
struct SRadianceTransferProbeCompute
{
    // Radiance transfer matrix
    SIntPair radianceTransfer[ RADIANCE_TRANSFER_BASIS_COUNT ];

    // Irradiance coming from static lights
    SUIntPair m_staticIrradianceRGB0_R3;
    SUIntPair m_staticIrradianceRGB1_G3;
    SUIntPair m_staticIrradianceRGB2_B3;

    // Sky visibility 
    SUIntPair skyVisibility;
};

StructuredBuffer<SRadianceTransferProbeCompute>  TransferProbes : register(t0);// Input buffer of probes to process
RWTexture3D<float4> OutputTextureR : register(u0);// Ouptut texture containing for each probe: RGB for basis vector[0], R for basis vector[3]
RWTexture3D<float4> OutputTextureG : register(u1);// Ouptut texture containing for each probe: RGB for basis vector[1], G for basis vector[3]
RWTexture3D<float4> OutputTextureB : register(u2);// Ouptut texture containing for each probe: RGB for basis vector[2], B for basis vector[3]

SamplerState Viewport__ParaboloidReflectionTexture__SampObj___s : register(s12);
Texture2D<float4> Viewport__ParaboloidReflectionTexture__TexObj__ : register(t12);

// Must match version in SHCompression.h

static const float kS16SHCoefMultiplier = 1024.0f;
static const float kS16SHCoefMultiplierRcp = 1.0f / kS16SHCoefMultiplier;

void SHToU64V1_UncompressSH( in const SIntPair data, out float4 vec )
{
    vec.x = ((data.lowBits & ((int)0x0000ffff))>>0 );
    vec.y = ((data.lowBits & ((int)0xffff0000))>>16);

    vec.z = ((data.highBits & ((int)0x0000ffff))>>0);
    vec.w = ((data.highBits & ((int)0xffff0000))>>16);

    vec *= kS16SHCoefMultiplierRcp;
}

// Must match version in SHCompression.h

// Factors for scaling float light values to and from unsigned shorts
static const float kU16LightMultiplier    = 8192.f;// allows values up to 8
static const float kU16LightMultiplierRcp = 1.0f / kU16LightMultiplier;

void LightValuesToU64_UncompressVector( in const SUIntPair data, out float4 vec )
{
    vec.x = ((data.lowBits & 0x0000ffff)>>0 );
    vec.y = ((data.lowBits & 0xffff0000)>>16);

    vec.z = ((data.highBits & 0x0000ffff)>>0);
    vec.w = ((data.highBits & 0xffff0000)>>16);

    vec *= kU16LightMultiplierRcp;

    //debug: check for out-of-range values
    //vec = step(7.5f, vec) * 1000;
}

// Must match version in SHCompression.h

// Factors for scaling floats in the 0..1 range to and from unsigned shorts (TODO_LM_IMPROVE: kU16UnitMultiplier should be 65535)
static const float kU16UnitMultiplier = 32767.f;
static const float kU16UnitMultiplierRcp = 1.0f / kU16UnitMultiplier;

void F32ToU16_UncompressValue( in const SUIntPair data, out float4 vec )
{
    vec.x = ((data.lowBits & 0x0000ffff)>>0 );
    vec.y = ((data.lowBits & 0xffff0000)>>16);

    vec.z = ((data.highBits & 0x0000ffff)>>0);
    vec.w = ((data.highBits & 0xffff0000)>>16);

    vec *= kU16UnitMultiplierRcp;
}


float DotPositive( in const float4 v0, in const float4 v1 )
{
    return max( dot(v0, v1), 0 );
}


// do 3 dot and put that in a vector
void DotPositive3Vector( out float4 vOut, 
                                 in const float4 vA0, in const float4 vB0, 
                                 in const float4 vA1, in const float4 vB1, 
                                 in const float4 vA2, in const float4 vB2 )
{

    vOut.x = DotPositive( vA0, vB0 );
    vOut.y = DotPositive( vA1, vB1 );
    vOut.z = DotPositive( vA2, vB2 );
    vOut.w = 0;
}


// Computes the SH projection of an "impulse" in the given direction
float4 DirectionSH( in const float3 dir )
{
    return float4( 0.282095f, 0.488603f, 0.488603f, 0.488603f ) *
            float4( 1.0f, dir.yzx );
}

#ifdef SUNSHADOW
struct SLongRangeShadowParams
{
    bool    enabled;
    float3  positionWS;
    float3  normalWS;
};

float CalculateLongRangeShadowFactor( in float3 longRangeShadowCoords )
{

        float longRangeShadow = LightData__LongRangeShadowVolumeTexture__TexObj__.SampleCmpLevelZero(LongRangeShadowSampler_s, longRangeShadowCoords.xy, longRangeShadowCoords.z).x;

    return longRangeShadow;
}

float3 CalculateLongRangeShadowCoords( in float3 positionWS, float3 normalWS )
{
    float3 longRangeShadowPos = float3( positionWS.xy + normalWS.xy * _LongRangeShadowVolumePosScaleBias.xy, positionWS.z * _LongRangeShadowVolumePosScaleBias.z + _LongRangeShadowVolumePosScaleBias.w );
    float2 longRangeShadowCoords = longRangeShadowPos.xy * _LongRangeShadowVolumeUvScaleBias.xy + _LongRangeShadowVolumeUvScaleBias.zw;
    return float3( longRangeShadowCoords, longRangeShadowPos.z );
}

float CalculateLongRangeShadowFactor( in SLongRangeShadowParams params )
{
    if( params.enabled )
    {
        float3 longRangeShadowCoords = CalculateLongRangeShadowCoords( params.positionWS, params.normalWS );
        return CalculateLongRangeShadowFactor( longRangeShadowCoords );
    }

    return 1.0f;
}
#endif

#ifdef SUNSHADOW
// Take multiple samples from the sun shadow to determine how deeply in-shadow the specified position is
// returns: 0 = completely in shadow .. 1 completely out of shadow
float SampleSunShadow(in const float3 worldPos)
{
    const float kernelWidth     = 9.f;
    const int numSamplesPerAxis = 3;

    float shadow = 0;

    // Sample the long-range shadowmap

    SLongRangeShadowParams longRangeParams = (SLongRangeShadowParams)0;
    longRangeParams.enabled = true;
    longRangeParams.normalWS = 0;   // Not needed for this

    for(int i=0; i<numSamplesPerAxis; i++)
    {
        float3 upOffset = (-0.5f+(i/float(numSamplesPerAxis))) * kernelWidth * _SunShadowUpVec;

        for(int j=0; j<numSamplesPerAxis; j++)
        {
            float3 rightOffset = (-0.5f+(j/float(numSamplesPerAxis))) * kernelWidth * _SunShadowRightVec;

            longRangeParams.positionWS = worldPos + upOffset + rightOffset;

            shadow += CalculateLongRangeShadowFactor(longRangeParams);
        }
    }

    shadow /= (numSamplesPerAxis*numSamplesPerAxis);

    return shadow;
}
#endif// def SUNSHADOW

static const float k_ElectricLightSwitchSpeed = 4.0f;
static const float k_AverageBuildingFloorHeight = 4.0f;
static const float k_MaxBuildingFloors = 120.0f;
static const float k_BuildingSectionFloors = 24.0f;
static const float k_BuildingSectionCount = floor( k_MaxBuildingFloors / k_BuildingSectionFloors );

float GetElectricPowerIntensity( float instancePowerIntensity )
{
    return saturate( k_ElectricLightSwitchSpeed * instancePowerIntensity * k_BuildingSectionCount );
}

float2 ComputeParaboloidProjectionTexCoords( float3 reflectedWS )
{
    float2 transform = float2(1,0);

    if( true )
    {
        transform.x = 0.5f;
        if( reflectedWS.z < 0 )
        {
            transform.y = 0.5f;
            reflectedWS.z = -reflectedWS.z;
        }
    }
	
    float3 R = reflectedWS.yxz;
     
    float2 uv;
    uv.x = -(R.x / (2.0f + 2.0f*R.z));
    uv.y =  (R.y / (2.0f + 2.0f*R.z));
	
    uv.x += 0.5f;
    uv.y += 0.5f;

    uv.y = uv.y * transform.x + transform.y;
	
    uv = saturate( uv );
	
	if (uv.y > 0.5f)
	{
		uv.x = 1.0 - uv.x;
	}
	
    uv.x *= 0.5f;
    uv.x += 0.5f;
  
    return uv;
}

float3 SampleSkyDirection(float3 worldDir)
{
	float2 uvReflection = ComputeParaboloidProjectionTexCoords( worldDir ); 
	
	//skyIllum = float4(Viewport__ParaboloidReflectionTexture__TexObj__.SampleLevel(Viewport__ParaboloidReflectionTexture__SampObj___s, uvReflection, 8.0).xyz, 0.0) / _ScaleFactor;
    return Viewport__ParaboloidReflectionTexture__TexObj__.SampleLevel(Viewport__ParaboloidReflectionTexture__SampObj___s, uvReflection, 0.0).xyz;
}

float4 SHBasis(float3 dir)
{
    // Exact SH_Dir implementation that matches your hardcoded BasisVectorsSH[]
    const float c0 = 0.282095f;
    const float c1 = 0.488603f;
    return float4(
        c0,
        c1 * dir.y,   // slot 1
        c1 * dir.z,   // slot 2
        c1 * dir.x    // slot 3
    );
}

//-----------------------------------------------------------------------------
// Computes the appearance of the probe given the lighting environment
// param: colourVectors - pointer to the array of three colour vectors to calculate
// remarks: call ProjectLightingEnvironment prior to this
//-----------------------------------------------------------------------------
void ProcessProbe(  out float4 colourVectors[3],
                    in const SRadianceTransferProbeCompute radianceTransferProbe,
                    in const float3 worldPos)
{
    // The basis vectors projected into SH, ie. the result of SH_Dir(SRadianceTransferProbe::BasisVectors)
    const float4 BasisVectorsSH[ RADIANCE_TRANSFER_BASIS_COUNT ] = 
    {
        float4( 0.282095, -0.345495, 0.282095, -0.199471 ),    
        float4( 0.282095, 0.345495, 0.282095, -0.199471 ),
        float4( 0.282095, 0, 0.282095, 0.398943 ),
        float4( 0.282095, 0, -0.488603, 0 ),
    };
	
    const float3 BasisVectors[ RADIANCE_TRANSFER_BASIS_COUNT ] = 
    {
        float3( -0.408248, -0.707107,  0.5773503 ),    
        float3( -0.408248,  0.707107,  0.5773503 ),
        float3(  0.816497,  0.0,       0.5773503 ),
        float3(  0.0,       0.0,      -1.0 ),
    };
	
    // ------------------------------------------------------------------
    // Compute SkyDirect SH from the paraboloid texture (global, once)
    // ------------------------------------------------------------------
    float4 SkyReflectDirectR = 0.0f.xxxx;
    float4 SkyReflectDirectG = 0.0f.xxxx;
    float4 SkyReflectDirectB = 0.0f.xxxx;

    const uint NUM_SAMPLES = 256;                    // 128 is fine, 512 is overkill
    const float WEIGHT = 4.0f * 3.1415926535897932 / float(NUM_SAMPLES);

    for (uint i = 0; i < NUM_SAMPLES; i++)
    {
        // Fibonacci spiral ? excellent uniform distribution on the sphere
        const float golden = 3.14159265359f * (3.0f - sqrt(5.0f)); // ? 2.39996
        float theta = float(i) * golden;
        float z = 1.0f - (float(i) + 0.5f) / float(NUM_SAMPLES) * 2.0f;
        float radius = sqrt(1.0f - z * z);
        float x = radius * cos(theta);
        float y = radius * sin(theta);

        float3 dir = normalize(float3(x, y, z));   // unit vector

        float3 radiance = SampleSkyDirection(dir); // YOUR paraboloid sample

        float4 sh = SHBasis(dir);

        SkyReflectDirectR += radiance.r * sh * WEIGHT;
        SkyReflectDirectG += radiance.g * sh * WEIGHT;
        SkyReflectDirectB += radiance.b * sh * WEIGHT;
    }
	
	SkyReflectDirectR /= _ScaleFactor;
	SkyReflectDirectG /= _ScaleFactor;
	SkyReflectDirectB /= _ScaleFactor;

	// Angular tightness value for the interpretation of the PRT for sun bounces.
    // Without it, sun bounces had a tendency to appear too uniformly everywhere and only apply in the upward direction (because our downward basis vector was too dominant in receiving the bounces).
    const float sunBounceTightnessPower = 4.f;// Higher = more localised and directional.  Lower = less accurate but smoother.

    float shadow = 1;// 0 = completely in shadow .. 1 completely out of shadow
#ifdef SUNSHADOW
   shadow = SampleSunShadow(worldPos);
#endif// def SUNSHADOW

    float4 skyVisibilityVec;
    F32ToU16_UncompressValue(radianceTransferProbe.skyVisibility, skyVisibilityVec);
    float skyVisibility[RADIANCE_TRANSFER_BASIS_COUNT] = {skyVisibilityVec.x, skyVisibilityVec.y, skyVisibilityVec.z, skyVisibilityVec.w};

    // Get transferred radiance along each radiance transfer basis
    float4 colour[ RADIANCE_TRANSFER_BASIS_COUNT ];
    for ( int basisIndex = 0; basisIndex < RADIANCE_TRANSFER_BASIS_COUNT; basisIndex++ )
    {
        // Transfer vector for this basis
        float4 radianceTransferR;
        SHToU64V1_UncompressSH(radianceTransferProbe.radianceTransfer[basisIndex], radianceTransferR);

        // TEMP! till we decide if we want coloured PRT or not
        float4 radianceTransferG;
        float4 radianceTransferB;
        radianceTransferG = radianceTransferB = radianceTransferR;

        // Compute the convolution of the sky and sun projection with the transfer vectors
        // in order to get the indirect lighting hitting this probe.

        float4 skyBounce;
        DotPositive3Vector( skyBounce, _SkyBounceR, radianceTransferR, _SkyBounceG, radianceTransferG, _SkyBounceB, radianceTransferB );

        float4 sunBounce;
        DotPositive3Vector( sunBounce, _SunBounceR, radianceTransferR, _SunBounceG, radianceTransferG, _SunBounceB, radianceTransferB );

        // Adjust daylight bounce intensity for indoors
#ifdef INTERIOR
        const float interiorDaylightBounceMultiplier = 0.1f;
        skyBounce *= interiorDaylightBounceMultiplier;
        sunBounce *= interiorDaylightBounceMultiplier;
#endif// def INTERIOR

#ifndef INTERIOR
	    // Apply an angular tightness value to the interpretation of the PRT for the sun bounce.  This is needed in order for the bounces to be nicely localised and directional.
        float3 PRTdir = normalize(radianceTransferG.wyz);// the overall direction of the radiance transfer
        sunBounce *= pow(max(dot(_SunDirection, PRTdir),0), sunBounceTightnessPower);
#endif// ndef INTERIOR

#ifdef SUNSHADOW
        // Use the sun shadow to reduce upward sun bounces in shaded areas.
        // This is needed because the PRT doesn't have enough basis vectors to accurately predict which areas will be in shadow for a given sun direction.
        if (basisIndex == 3)// the downward basis vector
        {
            sunBounce *= lerp(0.25f, 1.f, shadow);
        }
#endif// def SUNSHADOW

        colour[ basisIndex ] = skyBounce + sunBounce;

        float4 basisDirSH = BasisVectorsSH[basisIndex];

        float4 skyIllum;
        DotPositive3Vector( skyIllum, SkyReflectDirectR, basisDirSH, SkyReflectDirectG, basisDirSH, SkyReflectDirectB, basisDirSH );
		
		//float2 uvReflection = ComputeParaboloidProjectionTexCoords( BasisVectors[ basisIndex ] ); 
		
		//skyIllum = float4(Viewport__ParaboloidReflectionTexture__TexObj__.SampleLevel(Viewport__ParaboloidReflectionTexture__SampObj___s, uvReflection, 8.0).xyz, 0.0) / _ScaleFactor;
		
        skyIllum *= skyVisibility[basisIndex];

        // Adjust daylight direct intensity for indoors
#ifdef INTERIOR
        const float interiorSkyDirectMultiplier = 4.f;
        skyIllum *= interiorSkyDirectMultiplier;
#endif// def INTERIOR

        colour[ basisIndex ] += skyIllum;

        // debug: show sky visibility only
        //colour[ basisIndex ].rgb = skyVisibility[basisIndex];
        
    }// end for each basis vector

    // Pack the four colours (one per basis vector) into the three colour vectors
    colourVectors[0] = float4( colour[ 0 ].rgb, colour[ 3 ].r );
    colourVectors[1] = float4( colour[ 1 ].rgb, colour[ 3 ].g );
    colourVectors[2] = float4( colour[ 2 ].rgb, colour[ 3 ].b );

    // Add static irradiance

    float finalLocalLightsMultiplier =
#ifdef INTERIOR
        _LocalLightsMultipliers.y;
#else// ifndef INTERIOR
        _LocalLightsMultipliers.x;
#endif// ndef INTERIOR

#ifdef ELECTRIC_POWER
    // This should match the call to GetElectricPowerIntensity in SetupVisibleLights.h
    float2 clampedPosXY = GetBlackoutClampedPosition(worldPos.xy);   

    for(int regionIndex = 0; regionIndex < _ElectricPowerNumActiveRegions; regionIndex++)
    {
        float clampedDistance = length(clampedPosXY-float2(_ElectricPowerCentreX[regionIndex], _ElectricPowerCentreY[regionIndex]));

        float intensity;
        intensity = 1.0f - saturate( (_ElectricPowerFailureRadius[regionIndex] - clampedDistance) / _ElectricPowerSwitchDistance[regionIndex] );
        intensity += saturate( (_ElectricPowerReturnRadius[regionIndex] - clampedDistance) / _ElectricPowerSwitchDistance[regionIndex] );
        intensity = saturate(intensity);

        finalLocalLightsMultiplier *= GetElectricPowerIntensity(intensity);
    }
#endif// def ELECTRIC_POWER

    float4 staticIrradiance;

    // debug: show static irradiance only
    /* 
    colourVectors[0]= 0.f;
    colourVectors[1]= 0.f;
    colourVectors[2]= 0.f;
    */
    
    LightValuesToU64_UncompressVector(radianceTransferProbe.m_staticIrradianceRGB0_R3, staticIrradiance);
    colourVectors[0] += staticIrradiance * finalLocalLightsMultiplier;

    LightValuesToU64_UncompressVector(radianceTransferProbe.m_staticIrradianceRGB1_G3, staticIrradiance);
    colourVectors[1] += staticIrradiance * finalLocalLightsMultiplier;

    LightValuesToU64_UncompressVector(radianceTransferProbe.m_staticIrradianceRGB2_B3, staticIrradiance);
    colourVectors[2] += staticIrradiance * finalLocalLightsMultiplier;

    colourVectors[0] *= _ScaleFactor;
    colourVectors[1] *= _ScaleFactor;
    colourVectors[2] *= _ScaleFactor;

    // debug: show sampled shadow only
   /* 
#ifdef SUNSHADOW
    colourVectors[0] = shadow;
    colourVectors[1] = shadow;
    colourVectors[2] = shadow;
#endif// def SUNSHADOW
    */
}

[numthreads(CLightProbeRenderer_ms_computeShaderBlockSize,
            CLightProbeRenderer_ms_computeShaderBlockSize,
            CLightProbeRenderer_ms_computeShaderBlockSize)]

void main(const uint3 groupIndicesWithinDispatch    : SV_GroupID,           // XYZ indices of the thread group within the dispatch (group of thread groups covering the whole 3D texture)
            const uint3 threadIndicesWithinDispatch   : SV_DispatchThreadID,  // XYZ indices of the thread within the dispatch (group of thread groups covering the whole 3D texture)
            const uint3 threadIndicesWithinGroup      : SV_GroupThreadID,     // XYZ indices of the thread within the thread group
            const uint  threadIndexWithinGroup        : SV_GroupIndex)        // Flattened index of the thread within the thread group
{
    const uint3 textureSize   = (uint3)(_TextureSize.xyz);
    const uint3 blockSize     = (uint3)(_BlockSize.xyz);                       // Dimensions of the block procesed by the thread group
    const uint3 pixelBase     = groupIndicesWithinDispatch.xyz * blockSize.xyz;// Indicates which block of the texture this thread group is processing
    const uint pixelCount     = blockSize.x * blockSize.y * blockSize.z;      // Number of voxels each thread group processes

    const float maxLinearSliceIndex = _TextureSize.z - _NumZNonLinearSlices - 1;// Index of the slice at the top of the linear distribution section

    const float2 xyLocalProbeSpacing    = float2(1.f/(_TextureSize.x-1), 1.f/(_TextureSize.y-1));
    const float3 localMinCorner         = float3(-0.5f, -0.5f, 0.f);

    uint voxelIndexWithinBlock = threadIndexWithinGroup;

    const uint3 voxelIndicesWithinTexture = pixelBase + threadIndicesWithinGroup;

    uint probeIndex = (voxelIndicesWithinTexture.z*(_TextureSize.x*_TextureSize.y)) + (voxelIndicesWithinTexture.y*_TextureSize.x) + voxelIndicesWithinTexture.x;

    float3 worldPos = localMinCorner;
    worldPos.xy += (voxelIndicesWithinTexture.xy * xyLocalProbeSpacing);
    worldPos = mul(float4(worldPos,1), _LocalToWorldMatrix).xyz;

    // In the lower part of the cell, the slices have linear spacing; in the upper part the spacing is non-linear
    
    worldPos.z += _ZLinearSpacing * min(voxelIndicesWithinTexture.z, maxLinearSliceIndex);
    
    if (_NumZNonLinearSlices != 0.f)
    {
        worldPos.z += _NonLinearHeightRange * pow(abs(max(0.f, (voxelIndicesWithinTexture.z-maxLinearSliceIndex)) / _NumZNonLinearSlices), _ZDistributionPower);
    }
    
    float4 colourVectors[3];
    ProcessProbe(colourVectors, TransferProbes[probeIndex], worldPos);

    OutputTextureR[voxelIndicesWithinTexture] = colourVectors[0];
    OutputTextureG[voxelIndicesWithinTexture] = colourVectors[1];
    OutputTextureB[voxelIndicesWithinTexture] = colourVectors[2];
}

/*
void main)
{
// Needs manual fix for instruction:
// unknown dcl_: dcl_uav_typed_texture3d (float,float,float,float) u0
// Needs manual fix for instruction:
// unknown dcl_: dcl_uav_typed_texture3d (float,float,float,float) u1
// Needs manual fix for instruction:
// unknown dcl_: dcl_uav_typed_texture3d (float,float,float,float) u2
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  uint4 bitmask, uiDest;
  float4 fDest;

// Needs manual fix for instruction:
// unknown dcl_: dcl_thread_group 4, 4, 4
  r0.xyzw = (uint4)cb0[0].xyzz;
  r1.x = -cb0[16].w + cb0[14].z;
  r1.x = -1 + r1.x;
  r1.yz = float2(-1,-1) + cb0[14].xy;
  r1.yz = float2(1,1) / r1.yz;
  r0.xyzw = mad((int4)vThreadGroupID.xyzz, (int4)r0.xyzw, (int4)vThreadIDInGroup.xyzz);
  r2.xyz = (uint3)r0.wyx;
  r1.w = cb0[14].x * cb0[14].y;
  r2.y = cb0[14].x * r2.y;
  r1.w = r2.x * r1.w + r2.y;
  r1.w = r1.w + r2.z;
  r1.w = (uint)r1.w;
  r2.yz = (uint2)r0.xy;
  r3.xy = r2.yz * r1.yz + float2(-0.5,-0.5);
  r3.z = 1;
  r4.x = dot(r3.xyz, cb0[1].xyw);
  r4.y = dot(r3.xyz, cb0[2].xyw);
  r1.y = dot(r3.xyz, cb0[3].xyw);
  r1.z = min(r2.x, r1.x);
  r1.y = cb0[19].x * r1.z + r1.y;
  r1.z = cmp(cb0[16].w != 0.000000);
  r1.x = r2.x + -r1.x;
  r1.x = max(0, r1.x);
  r1.x = r1.x / cb0[16].w;
  r1.x = log2(abs(r1.x));
  r1.x = cb0[18].w * r1.x;
  r1.x = exp2(r1.x);
  r1.x = cb0[15].w * r1.x + r1.y;
  r4.z = r1.z ? r1.x : r1.y;
  r1.xy = float2(0,0);
  while (true) {
    r1.z = cmp((int)r1.y >= 3);
    if (r1.z != 0) break;
    r1.z = (int)r1.y;
    r1.z = r1.z * 0.333333343 + -0.5;
    r1.z = 9 * r1.z;
    r2.xyz = r1.zzz * cb0[17].xyz + r4.xyz;
    r1.z = r1.x;
    r2.w = 0;
    while (true) {
      r3.x = cmp((int)r2.w >= 3);
      if (r3.x != 0) break;
      r3.x = (int)r2.w;
      r3.x = r3.x * 0.333333343 + -0.5;
      r3.x = 9 * r3.x;
      r3.xyz = r3.xxx * cb0[16].xyz + r2.xyz;
      r3.z = r3.z * cb1[39].z + cb1[39].w;
      r3.xy = r3.xy * cb1[40].xy + cb1[40].zw;
      r3.x = t0.SampleCmpLevelZero(s0_s, r3.xy, r3.z).x;
      r1.z = r3.x + r1.z;
      r2.w = (int)r2.w + 1;
    }
    r1.x = r1.z;
    r1.y = (int)r1.y + 1;
  }
  r2.x = t1[r1.w].val[48/4];
  r2.y = t1[r1.w].val[48/4+1];
  r2.z = t1[r1.w].val[48/4+2];
  r2.w = t1[r1.w].val[48/4+3];
  r3.xyzw = (int4)r2.zwxy & int4(0xffff,0xffff,0xffff,0xffff);
  r2.xyzw = (uint4)r2.zwxy >> int4(16,16,16,16);
  r3.xyzw = (uint4)r3.xyzw;
  r2.xyzw = (uint4)r2.xzyw;
  r4.xz = r3.xy;
  r4.yw = r2.xz;
  r4.xyzw = float4(3.05185094e-005,3.05185094e-005,3.05185094e-005,3.05185094e-005) * r4.xyzw;
  r5.x = t1[r1.w].val[0/4];
  r5.y = t1[r1.w].val[0/4+1];
  r5.z = t1[r1.w].val[0/4+2];
  r5.w = t1[r1.w].val[0/4+3];
  r6.xyzw = (int4)r5.xyzw & int4(0xffff,0xffff,0xffff,0xffff);
  r5.xyzw = (uint4)r5.xyzw >> int4(16,16,16,16);
  r6.xyzw = (int4)r6.xyzw;
  r5.xyzw = (int4)r5.xzyw;
  r7.xz = r6.xy;
  r7.yw = r5.xz;
  r7.xyzw = float4(0.0009765625,0.0009765625,0.0009765625,0.0009765625) * r7.xyzw;
  r1.y = dot(cb0[7].xyzw, r7.xyzw);
  r8.x = max(0, r1.y);
  r1.y = dot(cb0[6].xyzw, r7.xyzw);
  r8.y = max(0, r1.y);
  r1.y = dot(cb0[5].xyzw, r7.xyzw);
  r8.z = max(0, r1.y);
  r1.y = dot(cb0[13].xyzw, r7.xyzw);
  r9.x = max(0, r1.y);
  r1.y = dot(cb0[12].xyzw, r7.xyzw);
  r9.y = max(0, r1.y);
  r1.y = dot(cb0[11].xyzw, r7.xyzw);
  r9.z = max(0, r1.y);
  r13.x = dot(r9.xyz, float3(0.792580009,0.182820007,0.0245999992));
  r13.x = max(0, r13.x);
  r13.y = dot(r9.xyz, float3(0.0925799981,0.88282001,0.0245999992));
  r13.y = max(0, r13.y);
  r13.z = dot(r9.xyz, float3(0.0925799981,0.182820007,0.724600017));
  r13.z = max(0, r13.z);
  r9.xyz = r13.xyz;
  r1.y = dot(r7.yzw, r7.yzw);
  r1.y = rsqrt(r1.y);
  r7.xyz = r7.wyz * r1.yyy;
  r1.y = dot(cb0[15].xyz, r7.xyz);
  r1.y = max(0, r1.y);
  r1.y = r1.y * r1.y;
  r1.y = r1.y * r1.y;
  r7.xyz = r9.xyz * r1.yyy + r8.xyz;
  r1.y = dot(cb0[10].xzyw, float4(0.282094985,0.282094985,-0.345494986,-0.199470997));
  r8.x = max(0, r1.y);
  r1.y = dot(cb0[9].xzyw, float4(0.282094985,0.282094985,-0.345494986,-0.199470997));
  r8.y = max(0, r1.y);
  r1.y = dot(cb0[8].xzyw, float4(0.282094985,0.282094985,-0.345494986,-0.199470997));
  r8.z = max(0, r1.y);
  r7.xyz = r8.xyz * r4.xxx + r7.xyz;
  r5.xz = r6.zw;
  r5.xyzw = float4(0.0009765625,0.0009765625,0.0009765625,0.0009765625) * r5.xyzw;
  r1.y = dot(cb0[7].xyzw, r5.xyzw);
  r6.x = max(0, r1.y);
  r1.y = dot(cb0[6].xyzw, r5.xyzw);
  r6.y = max(0, r1.y);
  r1.y = dot(cb0[5].xyzw, r5.xyzw);
  r6.z = max(0, r1.y);
  r1.y = dot(cb0[13].xyzw, r5.xyzw);
  r8.x = max(0, r1.y);
  r1.y = dot(cb0[12].xyzw, r5.xyzw);
  r8.y = max(0, r1.y);
  r1.y = dot(cb0[11].xyzw, r5.xyzw);
  r8.z = max(0, r1.y);
  r1.y = dot(r5.yzw, r5.yzw);
  r1.y = rsqrt(r1.y);
  r5.xyz = r5.wyz * r1.yyy;
  r1.y = dot(cb0[15].xyz, r5.xyz);
  r1.y = max(0, r1.y);
  r1.y = r1.y * r1.y;
  r1.y = r1.y * r1.y;
  r5.xyz = r8.xyz * r1.yyy + r6.xyz;
  r1.y = dot(cb0[10].xzyw, float4(0.282094985,0.282094985,0.345494986,-0.199470997));
  r6.x = max(0, r1.y);
  r1.y = dot(cb0[9].xzyw, float4(0.282094985,0.282094985,0.345494986,-0.199470997));
  r6.y = max(0, r1.y);
  r1.y = dot(cb0[8].xzyw, float4(0.282094985,0.282094985,0.345494986,-0.199470997));
  r6.z = max(0, r1.y);
  r5.xyz = r6.xyz * r4.yyy + r5.xyz;
  r6.x = t1[r1.w].val[16/4];
  r6.y = t1[r1.w].val[16/4+1];
  r6.z = t1[r1.w].val[16/4+2];
  r6.w = t1[r1.w].val[16/4+3];
  r8.xyzw = (int4)r6.xyzw & int4(0xffff,0xffff,0xffff,0xffff);
  r6.xyzw = (uint4)r6.xyzw >> int4(16,16,16,16);
  r8.xyzw = (int4)r8.xyzw;
  r6.xyzw = (int4)r6.xzyw;
  r9.xz = r8.xy;
  r9.yw = r6.xz;
  r9.xyzw = float4(0.0009765625,0.0009765625,0.0009765625,0.0009765625) * r9.xyzw;
  r1.y = dot(cb0[7].xyzw, r9.xyzw);
  r10.x = max(0, r1.y);
  r1.y = dot(cb0[6].xyzw, r9.xyzw);
  r10.y = max(0, r1.y);
  r1.y = dot(cb0[5].xyzw, r9.xyzw);
  r10.z = max(0, r1.y);
  r1.y = dot(cb0[13].xyzw, r9.xyzw);
  r11.x = max(0, r1.y);
  r1.y = dot(cb0[12].xyzw, r9.xyzw);
  r11.y = max(0, r1.y);
  r1.y = dot(cb0[11].xyzw, r9.xyzw);
  r11.z = max(0, r1.y);
  r13.x = dot(r11.xyz, float3(0.792580009,0.182820007,0.0245999992));
  r13.x = max(0, r13.x);
  r13.y = dot(r11.xyz, float3(0.0925799981,0.88282001,0.0245999992));
  r13.y = max(0, r13.y);
  r13.z = dot(r11.xyz, float3(0.0925799981,0.182820007,0.724600017));
  r13.z = max(0, r13.z);
  r11.xyz = r13.xyz;
  r1.y = dot(r9.yzw, r9.yzw);
  r1.y = rsqrt(r1.y);
  r9.xyz = r9.wyz * r1.yyy;
  r1.y = dot(cb0[15].xyz, r9.xyz);
  r1.y = max(0, r1.y);
  r1.y = r1.y * r1.y;
  r1.y = r1.y * r1.y;
  r9.xyz = r11.xyz * r1.yyy + r10.xyz;
  r1.y = dot(cb0[10].xzw, float3(0.282094985,0.282094985,0.398943007));
  r10.x = max(0, r1.y);
  r1.y = dot(cb0[9].xzw, float3(0.282094985,0.282094985,0.398943007));
  r10.y = max(0, r1.y);
  r1.y = dot(cb0[8].xzw, float3(0.282094985,0.282094985,0.398943007));
  r10.z = max(0, r1.y);
  r9.xyz = r10.xyz * r4.zzz + r9.xyz;
  r6.xz = r8.zw;
  r6.xyzw = float4(0.0009765625,0.0009765625,0.0009765625,0.0009765625) * r6.xyzw;
  r1.y = dot(cb0[7].xyzw, r6.xyzw);
  r4.x = max(0, r1.y);
  r1.y = dot(cb0[6].xyzw, r6.xyzw);
  r4.y = max(0, r1.y);
  r1.y = dot(cb0[5].xyzw, r6.xyzw);
  r4.z = max(0, r1.y);
  r1.y = dot(cb0[13].xyzw, r6.xyzw);
  r8.x = max(0, r1.y);
  r1.y = dot(cb0[12].xyzw, r6.xyzw);
  r8.y = max(0, r1.y);
  r1.y = dot(cb0[11].xyzw, r6.xyzw);
  r8.z = max(0, r1.y);
  r13.x = dot(r8.xyz, float3(0.792580009,0.182820007,0.0245999992));
  r13.x = max(0, r13.x);
  r13.y = dot(r8.xyz, float3(0.0925799981,0.88282001,0.0245999992));
  r13.y = max(0, r13.y);
  r13.z = dot(r8.xyz, float3(0.0925799981,0.182820007,0.724600017));
  r13.z = max(0, r13.z);
  r8.xyz = r13.xyz;
  r1.y = dot(r6.yzw, r6.yzw);
  r1.y = rsqrt(r1.y);
  r6.xyz = r6.wyz * r1.yyy;
  r1.y = dot(cb0[15].xyz, r6.xyz);
  r1.y = max(0, r1.y);
  r1.y = r1.y * r1.y;
  r1.y = r1.y * r1.y;
  r6.xyz = r8.xyz * r1.yyy;
  r1.x = r1.x * 0.0833333358 + 0.25;
  r1.xyz = r6.xyz * r1.xxx + r4.xyz;
  r3.x = dot(cb0[10].xz, float2(0.282094985,-0.488602996));
  r4.x = max(0, r3.x);
  r3.x = dot(cb0[9].xz, float2(0.282094985,-0.488602996));
  r4.y = max(0, r3.x);
  r3.x = dot(cb0[8].xz, float2(0.282094985,-0.488602996));
  r4.z = max(0, r3.x);
  r1.xyz = r4.xyz * r4.www + r1.xyz;
  r4.x = t1[r1.w].val[32/4];
  r4.y = t1[r1.w].val[32/4+1];
  r4.z = t1[r1.w].val[32/4+2];
  r4.w = t1[r1.w].val[32/4+3];
  r6.xyzw = (int4)r4.xyzw & int4(0xffff,0xffff,0xffff,0xffff);
  r4.xyzw = (uint4)r4.xyzw >> int4(16,16,16,16);
  r6.xyzw = (uint4)r6.xyzw;
  r4.xyzw = (uint4)r4.xzyw;
  r8.xz = r6.xy;
  r8.yw = r4.xz;
  r8.xyzw = cb0[18].xxxx * r8.xyzw;
  r7.w = r1.x;
  r7.xyzw = r8.xyzw * float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313) + r7.xyzw;
  r4.xz = r6.zw;
  r4.xyzw = cb0[18].xxxx * r4.xyzw;
  r5.w = r1.y;
  r4.xyzw = r4.xyzw * float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313) + r5.xyzw;
  r2.xz = r3.zw;
  r2.xyzw = cb0[18].xxxx * r2.xyzw;
  r9.w = r1.z;
  r1.xyzw = r2.xyzw * float4(0.000122070313,0.000122070313,0.000122070313,0.000122070313) + r9.xyzw;
  r2.xyzw = cb0[17].wwww * r7.xyzw;
  r3.xyzw = cb0[17].wwww * r4.xyzw;
  r1.xyzw = cb0[17].wwww * r1.xyzw;
// No code for instruction (needs manual fix):
store_uav_typed u0.xyzw, r0.xyww, r2.xyzw
// No code for instruction (needs manual fix):
store_uav_typed u1.xyzw, r0.xyww, r3.xyzw
// No code for instruction (needs manual fix):
store_uav_typed u2.xyzw, r0.xyzw, r1.xyzw
  return;
}
*/