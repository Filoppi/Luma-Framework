#ifndef SRC_DGVOODOO_HLSL
#define SRC_DGVOODOO_HLSL

float4 ApplyDgvMask(float4 value, float4 mask, float4 fill)
{
   return asfloat((asuint(value) & asuint(mask)) | asuint(fill));
}

// 1e37 is the exact sentinel every dgVoodoo dump uses (l(9999999933815812510711506376257961984.0)); it must not be
// rounded up to 1e38: it is multiplied downstream, and 1e38 overflows to inf ten times sooner, turning a zero-weight
// lerp around it into 0 * inf = NaN.
#define DGVOODOO_BIG 1e37

// dgVoodoo's guarded reciprocal: rcp with the huge-constant fallback at exactly 0 (movc in the translated CSO).
float DgVoodooRcp(float x)
{
   return (abs(x) > 0.0) ? (1.0 / x) : DGVOODOO_BIG;
}

// dgVoodoo's guarded log: log(0) = -inf -> -BIG (so exp2 later yields 0). It tests the -inf BIT PATTERN, not isinf(),
// so a +inf input keeps propagating as vanilla does instead of being flipped to ~0 (a white pixel gone black).
float DgVoodooLog2(float x)
{
   float l = log2(abs(x));
   return (asuint(l) == 0xff800000u) ? -DGVOODOO_BIG : l;
}

#endif // SRC_DGVOODOO_HLSL
