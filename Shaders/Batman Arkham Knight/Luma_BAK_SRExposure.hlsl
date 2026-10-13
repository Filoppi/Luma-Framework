// Batman: Arkham Knight - super resolution exposure.
// The game's eye adaptation writes a 1x1 exposure divisor (the tonemapper input is scene color / value),
// DLSS/FSR want the multiplier instead.

Texture2D<float> EngineExposure : register(t0);
RWTexture2D<float> SRExposure : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    SRExposure[uint2(0, 0)] = 1.0 / max(EngineExposure.Load(int3(0, 0, 0)), 0.0001);
}
