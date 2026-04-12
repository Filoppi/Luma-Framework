#ifndef SRC_DARKTABLE_UCS_HLSL
#define SRC_DARKTABLE_UCS_HLSL

// Copyright 2022 - Aurélien PIERRE - darktable project
// Darktable Uniform Color Space (UCS)
// https://eng.aurelienpierre.com/2022/02/color-saturation-control-for-the-21th-century/

struct s_xyY
{
  float2 xy;
  float  Y;
};

//BT.709 To
static const float3x3 Bt709ToXYZ =
  float3x3
  (
   0.412390798f,  0.357584327f, 0.180480793f,
   0.212639003f,  0.715168654f, 0.0721923187f,
   0.0193308182f, 0.119194783f, 0.950532138f
  );

//XYZ To
static const float3x3 XYZToBt709 =
  float3x3
  (
   3.24096989f,   -1.53738319f,  -0.498610764f,
   -0.969243645f,   1.87596750f,   0.0415550582f,
   0.0556300804f, -0.203976958f,  1.05697154f
  );

namespace CieXYZ
{
  namespace XYZTo
  {
    s_xyY xyY(const float3 XYZ)
    {
      const float xyz = XYZ.x + XYZ.y + XYZ.z;
      s_xyY xyY;
      // max because for pure black (RGB(0,0,0) = XYZ(0,0,0)) there is a division by 0
      xyY.xy = max(XYZ.xy / xyz, 0.f); // TODO: division by 0? It doesn't seem to cause issues
      xyY.Y = XYZ.y;
      return xyY;
    }
    
    // scRGB/BT.709
    float3 RGB(const float3 xyz)
    {
      return mul(XYZToBt709, xyz);
    }
  } //XYZTo

  namespace xyYTo
  {
    float3 XYZ(const s_xyY xyY)
    {
      float3 XYZ;
      XYZ.xz = float2(xyY.xy.x, (1.f - xyY.xy.x - xyY.xy.y)) / xyY.xy.y * xyY.Y; // TODO: division by 0? It doesn't seem to cause issues
      XYZ.y = xyY.Y;
      return XYZ;
    }
  } //xyYTo

  // scRGB/BT.709
  namespace RGBTo
  {
    float3 XZY(const float3 rgb)
    {
      return mul(Bt709ToXYZ, rgb);
    }
  } //RGBTo
} //CieXYZ

namespace DarktableUcs
{
  // -  UV is pure colour coordinates
  // - JCH is lightness (J), chroma (C) and hue (H)  [Helmholtz-Kohlrausch effect is corrected]
  //
  // -  UV is a Lab like colour space
  // - JCH is a LCh like colour space
  //
  // -  UV is for perceptually uniform gamut mapping
  // - JCH gets you chroma amount and hue angles and is adjusted for the Helmholtz-Kohlrausch effect

  namespace YTo
  {
    float LStar(const float Y)
    {
      float YHat = pow(max(Y, 0), 0.631651345306265); // Clip negative luminances
      float LStar = 2.098883786377 * YHat / (YHat + 1.12426773749357);
      return LStar;
    }
  } //YTo

  namespace LStarTo
  {
    float Y(float LStar)
    {
      LStar = min(LStar, 2.098883786377 - 0.0000005); // It would be 0 or NaN beyond this
      float powerBase = -1.12426773749357 * LStar / (LStar - 2.098883786377);
      float Y = pow(powerBase, 1.5831518565279648);
      return Y;
    }
  } //LStarTo

  namespace xyTo
  {
    float2 UV(const float2 xy)
    {
      static const float3x3 xyToUVD =
        float3x3
        (
          -0.783941002840055,  0.277512987809202,  0.153836578598858,
           0.745273540913283, -0.205375866083878, -0.165478376301988,
           0.318707282433486,  2.16743692732158,   0.291320554395942
        );
      static const float2x2 UVStarToUVStarPrime =
        float2x2
        (
          -1.124983854323892, -0.980483721769325,
           1.86323315098672,   1.971853092390862
        );

      float3 UVD = mul(xyToUVD, float3(xy, 1.f));
      UVD.xy /= UVD.z;
      float2 UVStar = float2(1.39656225667, 1.4513954287) * UVD.xy / (abs(UVD.xy) + float2(1.49217352929, 1.52488637914));
      float2 UVStarPrime = mul(UVStarToUVStarPrime, UVStar);
      return UVStarPrime;
    }
  } //xyTo

  namespace UVTo
  {
    float2 xy(const float2 UVStarPrime)
    {
      static const float2x2 UVStarPrimeToUVStar =
        float2x2
        (
          -5.037522385190711, -2.504856328185843,
           4.760029407436461,  2.874012963239247
        );
      static const float3x3 UVToxyD =
        float3x3
        (
           0.167171472114775,  0.141299802443708, -0.00801531300850582,
          -0.150959086409163, -0.155185060382272, -0.00843312433578007,
           0.940254742367256,  1.0,               -0.0256325967652889
        );

      float2 UVStar = mul(UVStarPrimeToUVStar, UVStarPrime);
      float2 UV = float2(-1.49217352929, -1.52488637914) * UVStar / (abs(UVStar) - float2(1.39656225667, 1.4513954287));
      float3 xyD = mul(UVToxyD, float3(UV, 1.f));
      xyD.xy /= xyD.z;
      return xyD.xy;
    }
  } //UVTo

  namespace xyYTo
  {
    // Simplified version (no white level adjustments)
    float3 LUV(const s_xyY xyY)
    {
      float LStar = YTo::LStar(xyY.Y);
      float2 UVStarPrime = xyTo::UV(xyY.xy);
      return float3(LStar, UVStarPrime);
    }

    // Simplified version (no white level adjustments)
    float3 LCH(const s_xyY xyY)
    {
      float J = YTo::LStar(xyY.Y);
      float2 UVStarPrime = xyTo::UV(xyY.xy);
      float C = sqrt(UVStarPrime.x * UVStarPrime.x + UVStarPrime.y * UVStarPrime.y);
      float H = atan2(UVStarPrime.y, UVStarPrime.x);
      return float3(J, C, H);
    }
      
    float3 JCH(const s_xyY xyY, const float YWhite = 1.0, const float cz = 1 /*0.525*/)
    {
      //input:
      //  * xyY in normalized CIE XYZ for the 2° 1931 observer adapted for D65
      //  * LWhite the lightness of white as dt UCS L* lightness. [is this the max white you want to display? like 10000 nits?]
      //  * cz: c * z
      //    * n = ratio of background luminance and the luminance of white (background/white),
      //    * z = 1 + sqrt(n)
      //    * c = 0.69 for average surround lighting
      //          0.59 for dim surround lighting (sRGB standard)
      //          0.525 for dark surround lighting
      //    * cz = 1 for standard pre-print proofing conditions with average surround and n = 20 %
      //          (background = middle grey, white = perfect diffuse white)
      //range:
      //  * xy in [0; 1]
      //  * Y normalized for perfect diffuse white = 1

      float LStar  = YTo::LStar(xyY.Y);
      float LWhite = YTo::LStar(YWhite);

      float2 UVStarPrime = xyTo::UV(xyY.xy);

      float M2 = UVStarPrime.x * UVStarPrime.x
               + UVStarPrime.y * UVStarPrime.y;

      float C = 15.932993652962535
              * pow(LStar, 0.6523997524738018)
              * pow(M2,    0.6007557017508491)
              / LWhite;
      float J = pow(LStar / LWhite, cz);
      float H = atan2(UVStarPrime.y, UVStarPrime.x);

      return float3(J, C, H);
    }

    float3 XYZ(float3 xyY)
    {
        float3 XYZ;

        XYZ.xz = float2(xyY.x, (1.f - xyY.xy.x - xyY.xy.y)) / xyY.y * xyY[2];

        XYZ.y = xyY[2];

        return XYZ;
    }
  } //xyYTo

  namespace LUVTo
  {
    s_xyY xyY(const float3 LUV)
    {
      s_xyY xyY;

      xyY.xy = UVTo::xy(LUV.yz);

      xyY.Y = LStarTo::Y(LUV[0]);

      return xyY;
    }
  } //LUVTo
  
  namespace LCHTo
  {
    s_xyY xyY(const float3 LCH)
    {
      float2 UVStarPrime = LCH[1] * float2(cos(LCH[2]), sin(LCH[2]));

      s_xyY xyY;

      xyY.xy = UVTo::xy(UVStarPrime);

      xyY.Y = LStarTo::Y(LCH[0]);

      return xyY;
    }
  } //LCHTo

  namespace JCHTo
  {
    s_xyY xyY(const float3 JCH, const float  YWhite = 1.0, const float  cz = 1 /*0.525*/)
    {
      //output: xyY in normalized CIE XYZ for the 2° 1931 observer adapted for D65
      //range:
      //  * xy in [0; 1]
      //  * Y normalized for perfect diffuse white = 1

      float J = JCH[0];
      float C = JCH[1];
      float H = JCH[2];

      float LWhite = YTo::LStar(YWhite);

      float LStar = pow(J, (1.f / cz)) * LWhite;

      float M = pow((C * LWhite / (15.932993652962535 * pow(LStar, 0.6523997524738018))), 0.8322850678616855);

      float2 UVStarPrime = M * float2(cos(H), sin(H));

      s_xyY xyY;
      xyY.xy = UVTo::xy(UVStarPrime);
      xyY.Y = LStarTo::Y(LStar);

      return xyY;
    }
  } //JCHTo
  
  // scRGB/BT.709
  // Paper white is expected to not have been multiplied in yet
  float3 RGBToUCSLCH(float3 rgb, float paperWhite = ITU_WhiteLevelNits / sRGB_WhiteLevelNits) // TODO: review pw
  {
    float3 XYZ = CieXYZ::RGBTo::XZY(rgb);
    s_xyY xyY = CieXYZ::XYZTo::xyY(XYZ);
#if 1
    float3 UCSLCH = xyYTo::JCH(xyY, paperWhite);
#else // Simplified implementation
    xyY.Y /= paperWhite;
    float3 UCSLCH = xyYTo::LCH(xyY);
#endif
    return UCSLCH;
  }

  // scRGB/BT.709
  float3 UCSLCHToRGB(float3 UCSLCH, float paperWhite = ITU_WhiteLevelNits / sRGB_WhiteLevelNits)
  {
#if 1
    s_xyY xyY = JCHTo::xyY(UCSLCH, paperWhite);
#else // Simplified implementation
    s_xyY xyY = LCHTo::xyY(UCSLCH);
    xyY.Y *= paperWhite;
#endif
    float3 XYZ = CieXYZ::xyYTo::XYZ(xyY);
    float3 rgb = CieXYZ::XYZTo::RGB(XYZ);
    return rgb;
  }

  // scRGB/BT.709
  float3 RGBToUCSLUV(float3 rgb)
  {
    float3 XYZ = CieXYZ::RGBTo::XZY(rgb);
    s_xyY xyY = CieXYZ::XYZTo::xyY(XYZ);
    float3 UCSLUV = xyYTo::LUV(xyY);
    return UCSLUV;
  }

  // scRGB/BT.709
  float3 UCSLUVToRGB(float3 UCSLUV)
  {
    s_xyY xyY = LUVTo::xyY(UCSLUV);
    float3 XYZ = CieXYZ::xyYTo::XYZ(xyY);
    float3 rgb = CieXYZ::XYZTo::RGB(XYZ);
    return rgb;
  }
} //DarktableUcs

#endif // SRC_DARKTABLE_UCS_HLSL