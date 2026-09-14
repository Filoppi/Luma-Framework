namespace renodx {
namespace color {

namespace pq {
static const float M1 = 2610.f / 16384.f;           // 0.1593017578125f;
static const float M2 = 128.f * (2523.f / 4096.f);  // 78.84375f;
static const float C1 = 3424.f / 4096.f;            // 0.8359375f;
static const float C2 = 32.f * (2413.f / 4096.f);   // 18.8515625f;
static const float C3 = 32.f * (2392.f / 4096.f);   // 18.6875f;

float Encode(float color, float scaling = 10000.f) {
  color *= (scaling / 10000.f);
  float y_m1 = pow(color, M1);
  return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
}

float3 Encode(float3 color, float scaling = 10000.f) {
  color *= (scaling / 10000.f);
  float3 y_m1 = pow(color, M1);
  return pow((C1 + C2 * y_m1) / (1.f + C3 * y_m1), M2);
}

float Decode(float color, float scaling = 10000.f) {
  float e_m12 = pow(color, 1.f / M2);
  float out_color = pow(max(0, e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
  return out_color * (10000.f / scaling);
}

float3 Decode(float3 color, float scaling = 10000.f) {
  float3 e_m12 = pow(color, 1.f / M2);
  float3 out_color = pow(max(0, e_m12 - C1) / (C2 - C3 * e_m12), 1.f / M1);
  return out_color * (10000.f / scaling);
}

float3 EncodeSafe(float3 color, float scaling = 10000.f) {
  return Encode(max(0, color), scaling);
}

float3 DecodeSafe(float3 color, float scaling = 10000.f) {
  return Decode(max(0, color), scaling);
}

}  // namespace pq

// According to Dolby
static const float3x3 XYZ_D65_TO_HUNT_POINTER_ESTEVEZ_LMS_MAT = float3x3(
    +0.4002f, 0.7076f, -0.0808f,
    -0.2263f, 1.1653f, +0.0457f,
    +0.0000f, 0.0000f, +0.9182f);

float3x3 Invert3x3(float3x3 m) {
  float a = m[0][0], b = m[0][1], c = m[0][2];
  float d = m[1][0], e = m[1][1], f = m[1][2];
  float g = m[2][0], h = m[2][1], i = m[2][2];

  float A = (e * i - f * h);
  float B = -(d * i - f * g);
  float C = (d * h - e * g);
  float D = -(b * i - c * h);
  float E = (a * i - c * g);
  float F = -(a * h - b * g);
  float G = (b * f - c * e);
  float H = -(a * f - c * d);
  float I = (a * e - b * d);

  float det = a * A + b * B + c * C;
  float invDet = det > 0 ? 1.0 / det : 0.0;

  return float3x3(
             A, D, G,
             B, E, H,
             C, F, I)
         * invDet;
}

static const float3x3 PLMS_TO_IPT_MAT = float3x3(
    0.4f, 0.4f, 0.2f,
    4.4550f, -4.8510f, 0.3960f,
    0.8056f, 0.3572f, -1.1628f);

static const float3x3 BT709_TO_XYZ_MAT = float3x3(
    0.4123907993f, 0.3575843394f, 0.1804807884f,
    0.2126390059f, 0.7151686788f, 0.0721923154f,
    0.0193308187f, 0.1191947798f, 0.9505321522f);

static const float3x3 BT2020_TO_XYZ_MAT = float3x3(
    0.6369580483f, 0.1446169036f, 0.1688809752f,
    0.2627002120f, 0.6779980715f, 0.0593017165f,
    0.0000000000f, 0.0280726930f, 1.0609850577f);

namespace ictcp {
// https://professional.dolby.com/siteassets/pdfs/ictcp_dolbywhitepaper_v071.pdf

static const float3x3 XYZ_TO_ICTCP_LMS_MAT = float3x3(
    0.359168797f, 0.697604775f, -0.0357883982f,
    -0.192186400f, 1.10039842f, 0.0755404010f,
    0.00695759989f, 0.0749168023f, 0.843357980f);

static const float3x3 ICTCP_LMS_TO_XYZ_MAT = float3x3(
    2.07036161f, -1.32659053f, 0.206681042f,
    0.364990383f, 0.680468797f, -0.0454616732f,
    -0.0495028905f, -0.0495028905f, 1.18806946f);

static const float3x3 BT709_TO_ICTCP_LMS_MAT = float3x3(
    0.295764088f, 0.623072445f, 0.0811667516f,
    0.156191974f, 0.727251648f, 0.116557933f,
    0.0351022854f, 0.156589955f, 0.808302998f);

static const float3x3 ICTCP_LMS_TO_BT709_MAT = float3x3(
    6.17353248f, -5.32089900f, 0.147354885f,
    -1.32403194f, 2.56026983f, -0.236238613f,
    -0.0115983877f, -0.264921456f, 1.27652633f);

static const float CROSSTALK = 0.04f;
static const float IPT_OPTIMIZATION = 1.0f;

static const float3x3 CROSSTALK_MAT = float3x3(
    1.0f - (2 * CROSSTALK), CROSSTALK, CROSSTALK,
    CROSSTALK, 1.0f - (2 * CROSSTALK), CROSSTALK,
    CROSSTALK, CROSSTALK, 1.0f - (2 * CROSSTALK));

static const float3x3 XYZ_TO_DOLBY_LMS_MAT = mul(XYZ_D65_TO_HUNT_POINTER_ESTEVEZ_LMS_MAT, CROSSTALK_MAT);

static const float3x3 PLMS_TO_IPT_OPTIMIZED_MAT = float3x3(
    lerp(PLMS_TO_IPT_MAT[0], float3(0.5f, 0.5f, 0.0f), IPT_OPTIMIZATION),
    PLMS_TO_IPT_MAT[1],
    PLMS_TO_IPT_MAT[2]);

static const float VECTORSCOPE_DEGREES = 65.f;
static const float ROTATION_POINT = VECTORSCOPE_DEGREES * 3.14159265358979323846f / 180.f;

static const float3x3 IPT_ROTATION_MAT = float3x3(
    1.f, 0, 0.f,
    0, cos(ROTATION_POINT), -sin(ROTATION_POINT),
    0, sin(ROTATION_POINT), cos(ROTATION_POINT));

static const float SCALE_FACTOR = 1.4f;
static const float3x3 IPT_SCALE_MAT = float3x3(
    1.0f, 1.0f, 1.0f,
    SCALE_FACTOR, SCALE_FACTOR, SCALE_FACTOR,
    1.0f, 1.0f, 1.0f);

static const float3x3 PLMS_TO_ICTCP_MAT = mul(IPT_ROTATION_MAT, PLMS_TO_IPT_OPTIMIZED_MAT) * IPT_SCALE_MAT;

namespace internal {
float3 BT709(float3 bt709_color, float scaling = 100.f) {
  float3 lms = mul(mul(XYZ_TO_DOLBY_LMS_MAT, BT709_TO_XYZ_MAT), bt709_color);
  float3 plms = pq::Encode(max(0, lms), scaling);
  float3 ictcp_color = mul(PLMS_TO_ICTCP_MAT, plms);
  return ictcp_color;
}
float3 BT2020(float3 bt2020_color, float scaling = 100.f) {
  float3 lms = mul(mul(XYZ_TO_DOLBY_LMS_MAT, BT2020_TO_XYZ_MAT), bt2020_color);
  float3 plms = pq::Encode(max(0, lms), scaling);
  float3 ictcp_color = mul(PLMS_TO_ICTCP_MAT, plms);
  return ictcp_color;
}
float3 ICtCpBT709(float3 ictcp_color, float scaling = 100.f) {
  float3 plms_color = mul(Invert3x3(ictcp::PLMS_TO_ICTCP_MAT), ictcp_color);
  float3 lms_color = pq::Decode(plms_color, scaling);
  float3 bt709_color = mul(
      mul(
          Invert3x3(BT709_TO_XYZ_MAT),
          Invert3x3(ictcp::XYZ_TO_DOLBY_LMS_MAT)),
      lms_color);
  return bt709_color;
}
float3 ICtCpBT2020(float3 ictcp_color, float scaling = 100.f) {
  float3 plms_color = mul(Invert3x3(ictcp::PLMS_TO_ICTCP_MAT), ictcp_color);
  float3 lms_color = pq::Decode(plms_color, scaling);
  float3 bt709_color = mul(
      mul(
          Invert3x3(BT2020_TO_XYZ_MAT),
          Invert3x3(ictcp::XYZ_TO_DOLBY_LMS_MAT)),
      lms_color);
  return bt709_color;
}
}

float3 To(float3 rgb, uint colorSpace)
{
    if (colorSpace == 1) return internal::BT2020(rgb);
	return internal::BT709(rgb);
}

float3 From(float3 ictcp_color, uint colorSpace)
{
    if (colorSpace == 1) return internal::ICtCpBT2020(ictcp_color);
    return internal::ICtCpBT709(ictcp_color);
}


} 
}
}