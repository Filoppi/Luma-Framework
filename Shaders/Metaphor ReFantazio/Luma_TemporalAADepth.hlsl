////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define NUMTHREADS_X 8
#define NUMTHREADS_Y 8

#define BORDER_SIZE 1

#define TILESIZE_X (BORDER_SIZE + NUMTHREADS_X + BORDER_SIZE)
#define TILESIZE_Y (BORDER_SIZE + NUMTHREADS_Y + BORDER_SIZE)

#define BORDER_COUNT (TILESIZE_X * TILESIZE_Y - NUMTHREADS_X * NUMTHREADS_Y)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

cbuffer cbTemporalAADepth : register(b0)
{
	float4 ScreenInfo : packoffset(c0.x);
	int UseVarianceClipping : packoffset(c1.x);
	float VarianceScale : packoffset(c1.y);
	float2 VelocityScale : packoffset(c1.z);
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SamplerState s_samplLinearClamp : register(s7);
SamplerState s_samplPointClamp  : register(s8);

Texture2D<float> t_DepthMap : register(t0);
Texture2D<float> t_TemporalDepthPrevMap : register(t1);
Texture2D<float2> t_VelocityMap : register(t2);

RWTexture2D<float> rw_TemporalDepth : register(u0);

groupshared float g_SharedDepth[TILESIZE_X * TILESIZE_Y];

static const float k_Inv9 = 1.0f / 9.0f;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float LoadDepth(int2 pos)
{
    int2 size = int2(ScreenInfo.xy);

    pos = clamp(pos, int2(0, 0), size - 1);

    return t_DepthMap.Load(int3(pos, 0));
}

float LoadSharedDepth(uint index)
{
    return g_SharedDepth[min(index, TILESIZE_X * TILESIZE_Y - 1)];
}

void StoreSharedDepth(uint index, float value)
{
	g_SharedDepth[min(index, TILESIZE_X * TILESIZE_Y - 1)] = value;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

[numthreads(NUMTHREADS_X, NUMTHREADS_Y, 1)]
void main(
    const uint3 dispatchID   : SV_DispatchThreadID,	// XYZ indices of the thread group within the dispatch
    const uint3 groupID      : SV_GroupID,			// XYZ indices of the thread within the dispatch
    const uint3 groupThread  : SV_GroupThreadID,	// XYZ indices of the thread within the thread group
    const uint  groupIndex   : SV_GroupIndex)		// Flattened index of the thread within the thread group
{
#ifdef HAS_PREVIOUS_FRAME

    uint local_y = groupIndex / TILESIZE_X;
    uint local_x = groupIndex % TILESIZE_X;
    int2 tile_origin = int2(groupID.xy * uint2(NUMTHREADS_X, NUMTHREADS_Y)) - 1;
	
	StoreSharedDepth(local_y * TILESIZE_X + local_x, LoadDepth(tile_origin + int2(local_x, local_y)));
	
    if (groupIndex < BORDER_COUNT)
    {
        uint extra_index = groupIndex + NUMTHREADS_X * NUMTHREADS_Y;

        uint extra_y = extra_index / TILESIZE_X;
        uint extra_x = extra_index % TILESIZE_X;

		StoreSharedDepth(extra_y * TILESIZE_X + extra_x, LoadDepth(tile_origin + int2(extra_x, extra_y)));
    }

	GroupMemoryBarrierWithGroupSync();
	
	float2 uv = (float2(dispatchID.xy) + 0.5f) * ScreenInfo.zw;
	
    //----------------------------------------------------------
    //  a b c
    //  d e f
    //  g h i
    //----------------------------------------------------------

    uint sx = groupThread.x + 1;
    uint sy = groupThread.y + 1;

    float a = LoadSharedDepth((sy - 1) * TILESIZE_X + (sx - 1));
    float b = LoadSharedDepth((sy - 1) * TILESIZE_X + (sx    ));
    float c = LoadSharedDepth((sy - 1) * TILESIZE_X + (sx + 1));

    float d = LoadSharedDepth((sy    ) * TILESIZE_X + (sx - 1));
    float e = LoadSharedDepth((sy    ) * TILESIZE_X + (sx    ));
    float f = LoadSharedDepth((sy    ) * TILESIZE_X + (sx + 1));

    float g = LoadSharedDepth((sy + 1) * TILESIZE_X + (sx - 1));
    float h = LoadSharedDepth((sy + 1) * TILESIZE_X + (sx    ));
    float i = LoadSharedDepth((sy + 1) * TILESIZE_X + (sx + 1));
	
	float neighbor_min = min(min(min(b, d), min(e, f)), h);
	float neighbor_max = max(max(max(b, d), max(e, f)), h);
	float corner_min = min(min(a, c), min(g, i));
	float corner_max = max(max(a, c), max(g, i));
	
	float clip_min = 0.5f * (min(neighbor_min, corner_min) + neighbor_min);
	float clip_max = 0.5f * (max(neighbor_max, corner_max) + neighbor_max);
	
	float variance_clip_min = clip_min;
	float variance_clip_max = clip_max;
	
	float dither_amount = 0.0f;
	if (UseVarianceClipping > 0)
	{
        float mean 	=	(a + b + c +
						 d + e + f +
						 g + h + i) * k_Inv9;

        float mean_sq =	(a*a + b*b + c*c +
						 d*d + e*e + f*f +
						 g*g + h*h + i*i) * k_Inv9;

		float sd = sqrt(abs(mean_sq - mean * mean));
        float sigma = VarianceScale * sd;

        variance_clip_min = max(clip_min, mean - sigma);
        variance_clip_max = min(clip_max, mean + sigma);
		
		float cv = sd / (abs(mean) + 1e-5f);
		
		float dither_threshold = 0.008f;
		float dither_weight = 100.0f;
		dither_amount = saturate((cv -  dither_threshold) * dither_weight);
	}
	
	float2 velocity = t_VelocityMap.SampleLevel(s_samplLinearClamp, uv, 0).xy * VelocityScale;
	
	float velocity_length = saturate( (abs(velocity.x * 2.0f * ScreenInfo.x) + abs(velocity.y * 2.0f * ScreenInfo.y)) * 2.0f );
	
	float history_depth = t_TemporalDepthPrevMap.SampleLevel(s_samplPointClamp, uv + velocity, 0);
	
    float clipped_history = clamp(history_depth, variance_clip_min, variance_clip_max);
	
	float weight_depth = min( abs(variance_clip_min - history_depth), abs(variance_clip_max - history_depth) );
	weight_depth = weight_depth / ((variance_clip_max - variance_clip_min) + weight_depth);
	
	float weight_velocity = ((velocity_length * velocity_length + velocity_length + 1.0f) * (velocity_length + 1.0f)) * 0.125f;
	// (x^2 + x + 1) * (x + 1)
	
	float weight = 1.0f - saturate( weight_depth * weight_velocity );
	
	weight *= 1.0f - min(dither_amount * max(1.0f, velocity_length * 2.0f), 1.0f);
	
	rw_TemporalDepth[dispatchID.xy] = lerp(e, clipped_history, weight);
	
#else // HAS_PREVIOUS_FRAME

	rw_TemporalDepth[dispatchID.xy] = LoadDepth(int2(dispatchID.xy));
	
#endif // HAS_PREVIOUS_FRAME
}