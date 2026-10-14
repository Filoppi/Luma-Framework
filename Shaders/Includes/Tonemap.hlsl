#ifndef SRC_TONEMAP_HLSL
#define SRC_TONEMAP_HLSL

#include "Common.hlsl"
#include "ACES.hlsl"
#include "CInfinityRolloff.hlsl"
#include "DICE.hlsl"
#include "ColorGradingLUT.hlsl"

static const float HableShoulderScale = 4.0;
static const float HableLinearScale = 1.0;
static const float HableToeScale = 1.0;
static const float HableWhitepoint = 2.0;

// TODO: this is the same as the UC2 TM below, merge them...
float4 Tonemap_Hable_Eval(in float4 x, float inShoulderScale, float inLinearScale, float inToeScale)
{
	const float A = 0.22 * inShoulderScale, // Shoulder strength
	           B = 0.3 * inLinearScale,    // Linear strength
	           C = 0.1,                    // Linear angle
	           D = 0.2,                    // Toe strength
	           E = 0.01 * inToeScale,      // Toe numerator
	           F = 0.3;                    // Toe denominator
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 Tonemap_Hable_Inverse_Eval(in float3 x, float inShoulderScale, float inLinearScale, float inToeScale)
{
	const float A = 0.22 * inShoulderScale, // Shoulder strength
	           B = 0.3 * inLinearScale,    // Linear strength
	           C = 0.1,                    // Linear angle
	           D = 0.2,                    // Toe strength
	           E = 0.01 * inToeScale,      // Toe numerator
	           F = 0.3;                    // Toe denominator
	float3 subPart1 = B*C*F-B*E-B*F*x;
	float3 denominator = 2*A*(E+F*x-F);
	float3 part1 = subPart1 / denominator;
	float3 part2 = sqrt(sqr(-subPart1)-4.0*D*sqr(F)*x*(A*E+A*F*x-A*F)) / denominator;
	return max(part1 - part2, part1 + part2); // Take the max of the two, it's likely always the right one (we could probably discard the one with the subtraction)
}

// Note: Hable is 100% per channel so you can pass in a single channel and exclusively retrieve the result on that if you don't need three channels.
float3 Tonemap_Hable_Inverse(in float3 compressedCol, float inShoulderScale = HableShoulderScale, float inLinearScale = HableLinearScale, float inToeScale = HableToeScale, float inWhitepoint = HableWhitepoint)
{
	float3 colorSigns = float3(compressedCol.x >= 0.0 ? 1.0 : -1.0, compressedCol.y >= 0.0 ? 1.0 : -1.0, compressedCol.z >= 0.0 ? 1.0 : -1.0);
	compressedCol = abs(compressedCol);

    float uncompressWhitepoint = Tonemap_Hable_Eval(inWhitepoint, inShoulderScale, inLinearScale, inToeScale).x;
	compressedCol *= uncompressWhitepoint;
	float3 uncompressCol = Tonemap_Hable_Inverse_Eval(compressedCol, inShoulderScale, inLinearScale, inToeScale);
	uncompressCol *= colorSigns;
	return uncompressCol;
}

// The wider the color space, the more saturated colors are generated in shadow
float3 Tonemap_Hable(in float3 color, float inShoulderScale = HableShoulderScale /*= HDRFilmCurve.x*/, float inLinearScale = HableLinearScale /*= HDRFilmCurve.y*/ /*mid tones*/, float inToeScale = HableToeScale /*= HDRFilmCurve.z*/, float inWhitepoint = HableWhitepoint /*= HDRFilmCurve.w*/)
{
	// Filmic response curve as proposed by J. Hable. Uncharted 2 tonemapper.

#if 1 // hardcode curve (also assumed in "Tonemap_Hable_Inverse()")
	inShoulderScale = HableShoulderScale;
	inLinearScale = HableLinearScale;
	inToeScale = HableToeScale;
	inWhitepoint = HableWhitepoint;
#endif

	float3 colorSigns = float3(color.x >= 0.0 ? 1.0 : -1.0, color.y >= 0.0 ? 1.0 : -1.0, color.z >= 0.0 ? 1.0 : -1.0); // sign() returns zero for zero and we don't want that
	float4 x = float4(abs(color), inWhitepoint); // LUMA FT: changed from clipping to zero to abs(), to generate negative (scRGB) colors

	float4 compressedCol = Tonemap_Hable_Eval(x, inShoulderScale, inLinearScale, inToeScale);
	// LUMA FT: if "compressedCol.xyz" was already negative with >= "color" values, we'd risk flipping it again if the original "color" sign was negative,
	// but currently the hardcoded math values don't allow it to ever go negative, so we don't have to worry about it
	// LUMA FT: this can output values higher than 1, but they got clipped in SDR. It seems like this can't reach pure zero for an input of zero, but it gets really close to it.
	// The white point value is calibrated, based on the other settings, so that the output range is mostly within 0-1.
	float3 result = (compressedCol.xyz * colorSigns) / compressedCol.w;
#if 0 // LUMA FT: disabled saturate(), it's unnecessary
	result = saturate(result);
#endif

#if 0 // Test inverse hable
	return Tonemap_Hable_Inverse(result, inShoulderScale, inLinearScale, inToeScale, inWhitepoint);
#endif

	return result;
}

float3 Tonemap_Uncharted2_Eval(float3 x, float a, float b, float c, float d, float e, float f)
{
  return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - (e / f);
}

// One channel only, given they are all the same
float Tonemap_Uncharted2_Inverse_Eval(float y, float a, float b, float c, float d, float e, float f)
{
  float ef = e / f;
  float yp = y + ef;

  float A = a * (yp - 1.0);
  float B = b * (yp - c);
  float C = d * (f * yp - e);

  float discriminant = B * B - 4.0 * A * C;

  float sqrtD = sqrt(abs(discriminant)) * sign(discriminant);

  float x1 = (-B + sqrtD) / (2.0 * A);
  float x2 = (-B - sqrtD) / (2.0 * A);

  // Choose the root that makes sense in your context (e.g., positive, in [0,1])
  return (x1 >= 0.0) ? x1 : x2;
}

// Derivates formulas from Musa/ShortFuse (RenoDX)
namespace Uncharted2
{
	float Derivative(float x,
		float a, float b, float c,
		float d, float e, float f)
	{
		float num = -a * b * (c - 1.0) * x * x
					+ 2.0 * a * d * (f - e) * x
					+ b * d * (c * f - e);

		float den = x * (a * x + b) + d * f;
		den = den * den;

		return num / den;
	}

	// Root of f'(x) = 0 for the raw "Tonemap_Uncharted2_Eval", using quadratic formula.
	// With a,b,c,d,e,f > 0 and 0 < c < 1, this is well-defined.
	float FindDerivativeRoot(float a, float b, float c, float d, float e, float f)
	{
		// Quadratic coefficients for numerator of f'(x)
		// -a*b*(c - 1) * x^2 + 2*a*d*(f - e)*x + b*d*(c*f - e) = 0
		float Aq = a * b * (1.f - c);  // -a*b*(c-1)
		float Bq = 2.f * a * d * (f - e);
		float Cq = b * d * (c * f - e);

		// Discriminant
		float disc = Bq * Bq - 4.f * Aq * Cq;
		disc = max(disc, 0.f);  // just in case of tiny negatives

		float sqrtDisc = sqrt(disc);

		float r1 = (-Bq + sqrtDisc) / (2.f * Aq);
		float r2 = (-Bq - sqrtDisc) / (2.f * Aq);

		// Larger root of the quadratic
		float root = max(r1, r2);

		// Only care about non-negative x in our domain
		return max(root, 0.f);
	}

	float SecondDerivative(float x,
		float a, float b, float c,
		float d, float e, float f)
	{
		// Common denom: (x*(a*x + b) + d*f)^3
		float t = x * (a * x + b) + d * f;
		float den = t * t * t;

		// Numerator pieces from WA:
		// 2 * ( a*b*x*(a*(c-1)*x^2 + 3*d*(e - c*f))
		//     + a*d*(e - f)*(3*a*x^2 - d*f)
		//     + b*b*d*(e - c*f) )
		float term1 = a * b * x * (a * (c - 1.f) * x * x + 3.f * d * (e - c * f));
		float term2 = a * d * (e - f) * (3.f * a * x * x - d * f);
		float term3 = b * b * d * (e - c * f);

		float num = 2.f * (term1 + term2 + term3);

		return num / den;
	}

	float FindSecondDerivativeRoot(float a, float b, float c, float d, float e, float f)
	{
		// Coefficients of the numerator of f''(x):
		// num(x) = A3 x^3 + A2 x^2 + A1 x + A0

		float A3 = a * a * b * (c - 1.0f);
		float A2 = 3.0f * a * a * d * (e - f);
		float A1 = 3.0f * a * b * d * (e - c * f);
		float A0 = a * d * d * (f * f - e * f) + b * b * d * (e - c * f);

		// If A3 = 0, curve is degenerate → no inflection
		if (abs(A3) < 1e-12f)
			return 0.f;

		// Normalize to monic cubic: x^3 + ax^2 + bx + c = 0
		float invA3 = 1.0f / A3;
		float an = A2 * invA3;
		float bn = A1 * invA3;
		float cn = A0 * invA3;

		// Depressed cubic t^3 + p t + q = 0  with x = t - a/3
		float an_3 = an / 3.0f;
		float p = bn - an * an_3;
		float q = 2.0f * an * an * an / 27.0f - an * bn / 3.0f + cn;

		float half_q = 0.5f * q;
		float Delta = half_q * half_q + (p / 3.0f) * (p / 3.0f) * (p / 3.0f);

		// Real root output
		float t;

		if (Delta >= 0.f)
		{
			float sqrtD = sqrt(Delta);
			float u = (-half_q + sqrtD);
			float v = (-half_q - sqrtD);

			// Use signed cube root
			float u_c = pow_mirrored(u, 1.0f / 3.0f);
			float v_c = pow_mirrored(v, 1.0f / 3.0f);
			t = u_c + v_c;
		}
		else
		{
			// 3 real roots → trig branch
			float m = 2.0f * sqrt(-p / 3.0f);
			float angle = acos((-half_q) / sqrt(-(p * p * p) / 27.0f));
			t = m * cos(angle / 3.0f);
		}

		float x = t - an_3;

		// Only meaningful inflection is positive
		return max(x, 0.f);
	}

	float ThirdDerivative(float x,
		float a, float b, float c,
		float d, float e, float f)
	{
		// Common denom: (x*(a*x + b) + d*f)^4
		float t = x * (a * x + b) + d * f;
		float den = t * t * t * t;

		// Numerator from WA:
		// -6 * (
		//   a*b*(a^2*(c-1)*x^4 + 6*a*d*x^2*(e - c*f) + d^2*f*(c*f - 2*e + f))
		//   + 4*a^2*d*x*(e - f)*(a*x^2 - d*f)
		//   + 4*a*b*b*d*x*(e - c*f)
		//   + b^3*d*(e - c*f)
		// )
		float x2 = x * x;
		float x4 = x2 * x2;

		float term1 = a * b * (a * a * (c - 1.f) * x4 + 6.f * a * d * x2 * (e - c * f) + d * d * f * (c * f - 2.f * e + f));

		float term2 = 4.f * a * a * d * x * (e - f) * (a * x2 - d * f);
		float term3 = 4.f * a * b * b * d * x * (e - c * f);
		float term4 = b * b * b * d * (e - c * f);

		float num = -6.f * (term1 + term2 + term3 + term4);

		return num / den;
	}

	// Analytic knee root of f'''(x) = 0 for "Tonemap_Uncharted2_Eval"
	// a,b,c,d,e,f > 0, typically 0 < c < 1.
	// Returns the smallest positive real root ("first knee") in x > 0.
	float FindThirdDerivativeRoot(float a, float b, float c, float d, float e, float f)
	{
		// sqrt(a b^2 c^2 - 2 a b^2 c + a b^2)
		float sqrt_ab = sqrt(
			a * b * b * c * c
			- 2.f * a * b * b * c
			+ a * b * b);

		// sqrt(a d^2 e^2 - 2 a d^2 e f + a d^2 f^2
		//    + b^2 c^2 d f + b^2 (-c) d e - b^2 c d f + b^2 d e)
		float sqrt_df = sqrt(
			a * d * d * e * e
			- 2.f * a * d * d * e * f
			+ a * d * d * f * f
			+ b * b * c * c * d * f
			+ b * b * (-c) * d * e
			- b * b * c * d * f
			+ b * b * d * e);

		// Precompute (d e - d f)
		float de_df = d * e - d * f;

		// Inner big piece: sqrt_ab * (...) / (8 * sqrt_df)
		float term_top =
			32.f * (a * d * d * e * f - a * d * d * f * f + b * b * c * d * f - b * b * d * e)
			/ (a * a * b * (c - 1.f));

		float term_mid =
			96.f * de_df * (c * d * f - d * e)
			/ (a * b * (c - 1.f) * (c - 1.f));

		float de_df2 = de_df * de_df;
		float de_df3 = de_df2 * de_df;

		float term_tail =
			64.f * de_df3
			/ (b * b * b * (c - 1.f) * (c - 1.f) * (c - 1.f));

		float Tfrac = sqrt_ab * (term_top - term_mid - term_tail)
						/ (8.f * sqrt_df);

		// (12 a^2 b c d f - 12 a^2 b d e) / (6 (a^3 b c - a^3 b))
		float Tmid2_num = 12.f * a * a * b * c * d * f
							- 12.f * a * a * b * d * e;
		float Tmid2_den = 6.f * (a * a * a * b * c - a * a * a * b);
		float Tmid2 = Tmid2_num / Tmid2_den;

		// (6 (c d f - d e))/(a (c - 1))
		float T3 = 6.f * (c * d * f - d * e)
					/ (a * (c - 1.f));

		// (8 (d e - d f)^2)/(b^2 (c - 1)^2)
		float T4 = 8.f * de_df2
					/ (b * b * (c - 1.f) * (c - 1.f));

		// Centers for the ± branches
		float centerNeg = -Tfrac + Tmid2 + T3 + T4;  // used with sqrt(-centerNeg)
		float centerPos = Tfrac + Tmid2 + T3 + T4;   // used with sqrt( centerPos)

		// Branch square roots: use sqrt_mirrored for robustness and correct branch behaviour
		float sNeg = sqrt_mirrored(-centerNeg);
		float sPos = sqrt_mirrored(centerPos);

		// Shifts:
		//  - first two roots use:  - sqrt_df/sqrt_ab - (d e - d f)/(b (c - 1))
		//  - last two use:          sqrt_df/sqrt_ab - (d e - d f)/(b (c - 1))
		float shift1 = sqrt_df / sqrt_ab + de_df / (b * (c - 1.f));  // we subtract this
		float shift2 = sqrt_df / sqrt_ab - de_df / (b * (c - 1.f));  // we add this

		// The four analytic roots from WA, mapped to floats:
		float r1 = -0.5f * sNeg - shift1;  // -1/2 * sqrt(-centerNeg) - shift1
		float r2 = 0.5f * sNeg - shift1;   //  1/2 * sqrt(-centerNeg) - shift1
		float r3 = -0.5f * sPos + shift2;  // -1/2 * sqrt( centerPos) + shift2
		float r4 = 0.5f * sPos + shift2;   //  1/2 * sqrt( centerPos) + shift2

		// Max root seems to be always be the right one
		float root = saturate(max(max(r1, r2), max(r3, r4)));

		return root;
	}

	// Extrapolates the tonemapper, taking the exact tangent at a specific pivot point, so to not break any gradients, and acknowledge all the curve parameters (even the ones beyond mid gray)
	// 
	// pivotMode:
	// 0: inflection point (the actual mid point, usually close to mid grey, a good place to match HDR and SDR, though it might miss the influence of some highlights parameters)
	// 1: highlights shoulder start (this might dim colors more, due to keeping more of the SDR tonemapper range applied, though it will also be more faithful to the original params)
	// 2: custom: specify your own pivot (the linear value outputted by the direct formula will be used to find the curve tangent, a good value is mid grey for SDR/HDR matching)
	// 
	// whiteScalingMode:
	// 0: the original white "W" value (from the formula above)
	// 1: the precomputed "1/HableTM(W, coeffs)" value, which we can directly multiply in with the other calculations (skipping divisions)
	float3 Tonemap_Uncharted2_Extended(
		float3 input, // Untonemapped linear HDR color (x)
		bool specifyOriginalOutput /*= false*/, // Use in case you already had it calculated for some reason
		inout float3 originalOutput /*= 0*/, // The original output, pre-scaled by "white"
		uint pivotMode /*= 0*/,
		float customPivotPoint /*= 0.18*/, // Not re-scaled by "white" in any way
		uint whiteScalingMode /*= 0*/,
		float whiteValue,
		float A, float B, float C, float D, float E, float F // Coeffs
		)
	{
		if (whiteScalingMode == 0)
		{
			whiteValue = 1.0 / Tonemap_Uncharted2_Eval(whiteValue, A, B, C, D, E, F).x;
		}

		if (!specifyOriginalOutput)
		{
			originalOutput = Tonemap_Uncharted2_Eval(abs(input), A, B, C, D, E, F) * whiteValue * sign(input); // Do abs*sign to properly handle negative values (sign mult works because the formula always maps 0 to 0) (we don't also flip the extended values as we never expect them to be big enough, and tonemapping negative values is random anyway)
		}
		
		float pivot_x = Tonemap_Uncharted2_Inverse_Eval(customPivotPoint / whiteValue, A, B, C, D, E, F);
		if (pivotMode == 0)
			pivot_x = FindSecondDerivativeRoot(A, B, C, D, E, F);
		else if (pivotMode == 1)
			pivot_x = FindThirdDerivativeRoot(A, B, C, D, E, F);
		float pivot_y = Tonemap_Uncharted2_Eval(pivot_x, A, B, C, D, E, F).x * whiteValue; // Might match "customPivotPoint" in some cases but these are compiled in anyway
		float slope = Derivative(pivot_x, A, B, C, D, E, F) * whiteValue;
		float offset = pivot_y - pivot_x * slope;

		float3 extendedOutput = input * slope + offset; // Match slope (offset and scale), meaning we take its tangent
		return input > pivot_x ? extendedOutput : originalOutput;
	}
}

float3 Tonemap_DICE(float3 color, float peakWhite, float paperWhite = 1.0)
{
	DICESettings settings = DefaultDICESettings();
	return DICETonemap(color * paperWhite, peakWhite, settings);
}

float3 Tonemap_ACES(float3 color, float peakWhite, float paperWhite = 1.0)
{
	ACESSettings settings = DefaultACESSettings();
	return ACESTonemap(color, paperWhite, peakWhite, settings);
}

// From RenoDX, by ShortFuse
float3 UpgradeToneMap(
    float3 color_untonemapped,
    float3 color_tonemapped,
    float3 color_tonemapped_graded,
    float post_process_strength = 1.f,
    float auto_correction = 0.f) {
  float ratio = 1.f;

  float y_untonemapped = GetLuminance(color_untonemapped, CS_BT709);
  float y_tonemapped = GetLuminance(color_tonemapped, CS_BT709);
  float y_tonemapped_graded = GetLuminance(color_tonemapped_graded, CS_BT709);

  if (y_untonemapped < y_tonemapped) {
    // If substracting (user contrast or paperwhite) scale down instead
    // Should only apply on mismatched HDR
    ratio = y_untonemapped / y_tonemapped;
  } else {
    float y_delta = y_untonemapped - y_tonemapped;
    y_delta = max(0, y_delta);  // Cleans up NaN
    const float y_new = y_tonemapped_graded + y_delta;

    const bool y_valid = (y_tonemapped_graded > 0);  // Cleans up NaN and ignore black
    ratio = y_valid ? (y_new / y_tonemapped_graded) : 0;
  }
  float auto_correct_ratio = lerp(1.f, ratio, saturate(y_untonemapped));
  ratio = lerp(ratio, auto_correct_ratio, auto_correction);

  float3 color_scaled = color_tonemapped_graded * ratio;
  // Match hue
  color_scaled = RestoreHueAndChrominance(color_scaled, color_tonemapped_graded, 1.0, 0.0, 0.0, FLT_MAX, 0.0, CS_BT709);
  return lerp(color_untonemapped, color_scaled, post_process_strength);
}

#endif // SRC_TONEMAP_HLSL
