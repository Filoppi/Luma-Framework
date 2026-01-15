Texture2DMS<float> tex : register(t0);
RWTexture2D<float> uav : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 dtid : SV_DispatchThreadID)
{
    uint w, h, n;
    tex.GetDimensions(w, h, n);

    float c = 0.0;
    for (uint i = 0; i < n; ++i) {
        c = max(c, tex.Load(dtid, i));
    }

    uav[dtid] = c;
}