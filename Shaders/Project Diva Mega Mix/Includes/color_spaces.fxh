/*********************************************************************NVMH3****
*******************************************************************************
$Revision: #4 $

Copyright NVIDIA Corporation 2008
TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED
*AS IS* AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS
OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL NVIDIA OR ITS SUPPLIERS
BE LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES
WHATSOEVER (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS,
BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, OR ANY OTHER PECUNIARY
LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE THIS SOFTWARE, EVEN IF
NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

% Color space conversion functions: CMY, CMYK, YUV, HSV, and of course RGB!



To learn more about shading, shaders, and to bounce ideas off other shader
    authors and users, visit the NVIDIA Shader Library Forums at:

    http://developer.nvidia.com/forums/

*******************************************************************************
******************************************************************************/



#ifndef _H_COLOR_SPACES
#define _H_COLOR_SPACES

#include <include\\Quad.fxh>

///////////////////////////////////////////////////
// CMYK ///////////////////////////////////////////
///////////////////////////////////////////////////

// CMY with no 'K' is verrry simple...

QUAD_REAL3 rgb2cmy(QUAD_REAL3 rgbColor) {
    return (QUAD_REAL3(1,1,1) - rgbColor);	// simplest conversion
}

QUAD_REAL3 cmy2rgb(QUAD_REAL3 cmyColor) {
    return (QUAD_REAL3(1,1,1) - cmyColor);	// simplest conversion
}

//
// There are many device-specific methods for determining 'K' (black ink).
//		This is a popular and simple one.
//
QUAD_REAL4 cmy2cmyk(QUAD_REAL3 cmyColor)
{
    QUAD_REAL k = ((QUAD_REAL)1.0);
    k = min(k,cmyColor.x);
    k = min(k,cmyColor.y);
    k = min(k,cmyColor.z);
    QUAD_REAL4 cmykColor;
    cmykColor.xyz = (cmyColor - (QUAD_REAL3)k)/((QUAD_REAL3)(((QUAD_REAL)1.0)-k).xxx);
    cmykColor.w = k;
    return (cmykColor);
}

QUAD_REAL3 cmyk2cmy(QUAD_REAL4 cmykColor)
{
    QUAD_REAL3 k = cmykColor.www;
    return ((cmykColor.xyz * (QUAD_REAL3(1,1,1)-k)) + k);
}

/////////

QUAD_REAL4 rgb2cmyk(QUAD_REAL3 rgbColor) {
    return cmy2cmyk(rgb2cmy(rgbColor));
}

QUAD_REAL3 cmyk2rgb(QUAD_REAL4 cmykColor) {
    return cmy2rgb(cmyk2cmy(cmykColor));
}

/////////////////////////////////////////////
// HSV //////////////////////////////////////
/////////////////////////////////////////////

QUAD_REAL __min_channel(QUAD_REAL3 v)
{
    QUAD_REAL t = (v.x<v.y) ? v.x : v.y;
    t = (t<v.z) ? t : v.z;
    return t;
}

QUAD_REAL __max_channel(QUAD_REAL3 v)
{
    QUAD_REAL t = (v.x>v.y) ? v.x : v.y;
    t = (t>v.z) ? t : v.z;
    return t;
}

QUAD_REAL3 rgb_to_hsv(QUAD_REAL3 RGB)
{
    QUAD_REAL3 HSV = (0.0).xxx;
    QUAD_REAL minVal = __min_channel(RGB);
    QUAD_REAL maxVal = __max_channel(RGB);
    QUAD_REAL delta = maxVal - minVal;             //Delta RGB value 
    HSV.z = maxVal;
    if (delta != 0) {                    // If gray, leave H & S at zero
       HSV.y = delta / maxVal;
       QUAD_REAL3 delRGB;
       delRGB = ( ( ( maxVal.xxx - RGB ) / 6.0 ) + ( delta / 2.0 ) ) / delta;
       if      ( RGB.x == maxVal ) HSV.x = delRGB.z - delRGB.y;
       else if ( RGB.y == maxVal ) HSV.x = ( 1.0/3.0) + delRGB.x - delRGB.z;
       else if ( RGB.z == maxVal ) HSV.x = ( 2.0/3.0) + delRGB.y - delRGB.x;
       if ( HSV.x < 0.0 ) { HSV.x += 1.0; }
       if ( HSV.x > 1.0 ) { HSV.x -= 1.0; }
    }
    return (HSV);
}

QUAD_REAL3 hsv_to_rgb(QUAD_REAL3 HSV)
{
    QUAD_REAL3 RGB = HSV.z;
    if ( HSV.y != 0 ) {
       QUAD_REAL var_h = HSV.x * 6;
       QUAD_REAL var_i = floor(var_h);   // Or ... var_i = floor( var_h )
       QUAD_REAL var_1 = HSV.z * (1.0 - HSV.y);
       QUAD_REAL var_2 = HSV.z * (1.0 - HSV.y * (var_h-var_i));
       QUAD_REAL var_3 = HSV.z * (1.0 - HSV.y * (1-(var_h-var_i)));
       if      (var_i == 0) { RGB = QUAD_REAL3(HSV.z, var_3, var_1); }
       else if (var_i == 1) { RGB = QUAD_REAL3(var_2, HSV.z, var_1); }
       else if (var_i == 2) { RGB = QUAD_REAL3(var_1, HSV.z, var_3); }
       else if (var_i == 3) { RGB = QUAD_REAL3(var_1, var_2, HSV.z); }
       else if (var_i == 4) { RGB = QUAD_REAL3(var_3, var_1, HSV.z); }
       else                 { RGB = QUAD_REAL3(HSV.z, var_1, var_2); }
   }
   return (RGB);
}

//// hsv operations

QUAD_REAL3 hsv_safe(QUAD_REAL3 InColor)
{
    float3 safeC = InColor;
    safeC.x = frac(safeC.x);
    /* if (safeC.x < 0.0) {
	safeC.x += 1.0;
    } else if (safeC.x > 1.0) {
	safeC.x -= 1.0;
    } */
    return(safeC);
}

QUAD_REAL3 hsv_complement(QUAD_REAL3 InColor)
{
    float3 complement = InColor;
    complement.x -= 0.5;
    if (complement.x<0.0) { complement.x += 1.0; } // faster than hsv_safe()
    return(complement);
}

#define COLOR_PI (3.141592652589793238)
#define COLOR_TWO_PI (2.0 * COLOR_PI)

QUAD_REAL3 color_cylinder(QUAD_REAL3 hsv)
{
    QUAD_REAL a = hsv.x * COLOR_TWO_PI;
    QUAD_REAL3 p;
    p.x = hsv.y * cos(a);
    p.y = hsv.y * sin(a);
    p.z = hsv.z;
    return p;
}

QUAD_REAL3 from_cylinder(QUAD_REAL3 p)
{
    QUAD_REAL3 hsv;
    hsv.z = p.z;
    QUAD_REAL q = p.x*p.x+p.y*p.y;
    q = sqrt(q);
    hsv.y = (q);
    QUAD_REAL a = atan2(p.y,p.x);
    hsv.x = a / COLOR_TWO_PI;
    // return hsv_safe(hsv);
    return (hsv);
}

// lerp the shortest distance through the color solid
QUAD_REAL3 hsv_lerp(QUAD_REAL3 C0,QUAD_REAL3 C1,QUAD_REAL T)
{
    QUAD_REAL3 p0 = color_cylinder(C0);
    QUAD_REAL3 p1 = color_cylinder(C1);
    QUAD_REAL3 pg = lerp(p0,p1,T);
    return from_cylinder(pg);
}

// lerp the shorterst distance around the color wheel - ONLY color
QUAD_REAL3 hsv_tint(QUAD_REAL3 SrcColor,QUAD_REAL3 TintColor,QUAD_REAL T)
{
    QUAD_REAL3 tt = hsv_lerp(SrcColor,TintColor,T);
    tt.yz = SrcColor.yz;
    return(tt);
}

////////////////////////////////////////////////////////////
// YUV /////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

QUAD_REAL3 rgb_to_yuv(QUAD_REAL3 RGB)
{
    QUAD_REAL y = dot(RGB,QUAD_REAL3(0.299,0.587,0.114));
    QUAD_REAL u = (RGB.z - y) * 0.565;
    QUAD_REAL v = (RGB.x - y) * 0.713;
    return QUAD_REAL3(y,u,v);
}

QUAD_REAL3 yuv_to_rgb(QUAD_REAL3 YUV)
{
   QUAD_REAL u = YUV.y;
   QUAD_REAL v = YUV.z;
   QUAD_REAL r = YUV.x + 1.403*v;
   QUAD_REAL g = YUV.x - 0.344*u - 1.403*v;
   QUAD_REAL b = YUV.x + 1.770*u;
   return QUAD_REAL3(r,g,b);
}

#endif /* _H_COLOR_SPACES */

//////////////////////////////////////// eof ///
