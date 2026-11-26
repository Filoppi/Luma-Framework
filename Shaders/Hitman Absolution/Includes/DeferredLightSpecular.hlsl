#ifndef HMA_DEFERRED_LIGHT_SPECULAR_HLSL
#define HMA_DEFERRED_LIGHT_SPECULAR_HLSL

float HMADeferredSpecularPower(float exponent)
{
#if TONEMAP_TYPE >= 1
    // The last LUT lobe can yield a +20 exponent and an excessive hotspot.
    exponent = min(exponent, 0.0);
#endif
    return exp2(exponent);
}

#endif

