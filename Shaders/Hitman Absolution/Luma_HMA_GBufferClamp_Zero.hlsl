// Pass A of the G-buffer clamp: blended with MAX against the target, a zero source clamps negative values up to 0.
struct PS_OUTPUT
{
   float4 rt0 : SV_Target0;
   float4 rt1 : SV_Target1;
   float4 rt2 : SV_Target2;
};

PS_OUTPUT main(float4 pos : SV_Position)
{
   PS_OUTPUT output;
   output.rt0 = float4(0.0, 0.0, 0.0, 0.0);
   output.rt1 = float4(0.0, 0.0, 0.0, 0.0);
   output.rt2 = float4(0.0, 0.0, 0.0, 0.0);
   return output;
}
