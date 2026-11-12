#ifndef LUMA_MAFIADE_HDR_PROXY_HLSLI
#define LUMA_MAFIADE_HDR_PROXY_HLSLI

// A fixed nonnegative HDR proxy with unit slope at black before gamma encoding.
// a solves ln(1+a*65504)=a, so the finite HDR range fits without lowering the
// shadow exposure seen by the LDR model. The inverse needs no original image.
static const float MafiaDE_HDRProxyA = 13.707835182644992;

float3 MafiaDE_EncodeHDRProxy(float3 colour)
{
   // Avoid losing small half-float values in 1+x before the logarithm.
   float3 z = MafiaDE_HDRProxyA * colour;
   float3 small = colour * (1.0 - z * 0.5 + z * z / 3.0);
   float3 logarithm = z < 0.001 ? small : log2(1.0 + z) * 0.05056576558766293;
   // R11 source values are nonnegative; state the pow domain explicitly for FXC.
   return pow(max(logarithm, 0.0), 1.0 / 2.2);
}

float3 MafiaDE_DecodeHDRProxy(float3 proxy)
{
   float3 linear_proxy = pow(max(proxy, 0.0), 2.2);
   float3 z = MafiaDE_HDRProxyA * linear_proxy;
   float3 small = linear_proxy * (1.0 + z * 0.5 + z * z / 6.0);
   return z < 0.001 ? small : (exp2(linear_proxy * 19.77622583932519) - 1.0) / MafiaDE_HDRProxyA;
}

#endif
