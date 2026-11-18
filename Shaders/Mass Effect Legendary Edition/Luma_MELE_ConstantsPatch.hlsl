// Under the engine's render scale the stretched native HBAO+ passes (main.cpp, "RunAO") need a constant the game rewrites before
// every dispatch changed, without a CPU readback: this copies the first 16 rows of the game's constant buffer into a buffer that
// main.cpp copies into a constant buffer, with one row's components multiplied and offset (patch_scale != 1 or patch_bias != 0).
// Other components keep their exact bits (the integer offsets read as floats would flush as denormals).

cbuffer GameConstants : register(b0)
{
   uint4 game_rows[16];
};

cbuffer ConstantsPatch : register(b1)
{
   float4 patch_scale;
   float4 patch_bias;
   uint patch_row;
};

RWBuffer<uint4> patched_rows : register(u0);

[numthreads(16, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
   uint4 row = game_rows[id.x];
   if (id.x == patch_row)
   {
      const bool4 patched = (patch_scale != 1.0) || (patch_bias != 0.0);
      row = patched ? asuint(asfloat(row) * patch_scale + patch_bias) : row;
   }
   patched_rows[id.x] = row;
}
