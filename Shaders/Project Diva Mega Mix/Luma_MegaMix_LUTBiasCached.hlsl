Texture2D<float2> lutIn : register(t0);
RWTexture2D<float> lutOut : register(u0);
SamplerState smp : register(s0);

#include "./Includes/Common.hlsl"

[numthreads(64, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
  float r1;

  // Decode
  r1.x = dtid.x; // x: input luminance (encoded)
  r1.x /= LUT_CACHE_OUTPUT_SIZE; // unorm
  r1.x *= LUT_CACHE_OUTPUT_SIZE / 512; // match LUT input range

  // Sample
  /*
    Maybe Neutral LUT https://www.desmos.com/calculator/u3bhz0bn62
    r1.x = Saturation (Rolls off to 0 way before 1)
    r1.y = SDR Tonemapped Luma (Rolls off to 1 as it approaches 1)
  */
  float backUpY = r1.x;
  r1.x = lutIn.SampleLevel(smp, float2(r1.x, 0), 0).y;
  float satBeforeBias = r1.x;

  // // early out: upward slope (aka looking back would be a loss)
  // float satOneTexelEarlier = lutIn.SampleLevel(smp, float2(r1.x - (1.0f / 512.f), 0), 0).y;
  // if (backUpY > satOneTexelEarlier) {
  //   lutOut[dtid.xy] = r1.x;
  //   return;
  // }

  // blowout reduction
  {
    float newSat = r1.x;

    // Gaussian, soft-max biased towards higher saturation
    {
      float y = backUpY;
      float satOrig = r1.x;

      #if CUSTOM_LUT_BLOWOUT_GAUSSIAN_STOPS == 0 // sampling step size
        const float lutStep = (1.0f / 512.f) * GS.LUTGaussianBlurStep;
      #else
        const float lutStep = (1.0f / 512.f) * GS.LUTGaussianBlurStep * (HDR_STOPS * 0.5f + 0.5f); 
      #endif 
      const float softMaxStr = GS.LUTGaussianBlurBias; // higher = stronger bias toward peak sat

      float blurredSat = 0, totalWeight = 0;
      [unroll] for (int k = -4; k <= 2; k++) { // biased towards lower luminance (more negative index)
        float ySample = max(0.0430528375734, k * lutStep + y); // most likely neutral LUT peak
        float sat = lutIn.SampleLevel(smp, float2(ySample, 0), 0).y; // sat channel
        if (sat > satOrig)
        {
          float w = exp(-0.5f * (k * k)) * exp(sat * softMaxStr); // gaussian * soft-max bias
          blurredSat = mad(sat, w, blurredSat);
          totalWeight += w;
        }
      }

      float m = safeDivision(blurredSat / totalWeight, 0); //avg
      newSat = max(newSat, m); //clamp chrominance loss
    }

    // high pass (else, shadows may change luminance)
    {
      float hp = backUpY;
      hp *= 8;
      hp = pow(hp, 2.5f);
      hp = saturate(hp);
      r1.x = lerp(satBeforeBias, newSat, hp);
    }
  }

  // x: biased saturation, y: unaltered rolled off luminance
  lutOut[dtid.xy] = r1.x;
}