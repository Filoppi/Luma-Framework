// Always include this instead of the global "Common.hlsl" if you made any changes to the game shaders/cbuffers

// Define the game custom cbuffer structs
#include "GameCBuffers.hlsl"
// Global common
#include "../../Includes/Common.hlsl"
#include "Settings.hlsl"

namespace ACES
{
    // This defines the range you want to cover under log2: 2^14 = 16384,
    // 14 is the minimum value to cover 10k nits.
    static const float LogLinearRange = 14.0; // TODO: in tonemap shader it's 1/14: 25 49 92 3D 25 49 92 3D 25 49 92 3D
    // This is the grey point you want to adjust with the "exposure grey" parameter
    static const float LogLinearGrey = 0.18;
    // This defines what an input matching the "linear grey" parameter will end up at in log space
    static const float LogGrey = 444.0 / 1023.0;
    
    float3 LogToLinear_Internal(float3 LogColor)
    {
        return exp2((LogColor - LogGrey) * LogLinearRange) * LogLinearGrey;
    }
    
    // The min log encoded value
    static const float LogLinearZeroOffset = LogToLinear_Internal(0.0);
    
    float3 LinearToLog(float3 LinearColor)
    {
        LinearColor += LogLinearZeroOffset; // Map "LogLinearZeroOffset" to 0, given we start from that as baseline.
        float3 LogColor = (log2(LinearColor) / LogLinearRange) - (log2(LogLinearGrey) / LogLinearRange) + LogGrey;
        LogColor = saturate(LogColor); // Needed to avoid issues
        return LogColor;
    }
    float3 LogToLinear(float3 LogColor)
    {
        return LogToLinear_Internal(LogColor) - LogLinearZeroOffset; // Map 0 back to "LogLinearZeroOffset", there shouldn't be anything below it.
    }
}