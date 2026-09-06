/**
EnigmaFix Copyright (c) 2026 Bryce Q.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
**/

#ifndef ENIGMAFIX_TEMPORALAARESOLVE_H
#define ENIGMAFIX_TEMPORALAARESOLVE_H

namespace EnigmaFix {

// Drop-in replacement for Mizuchi's ImageSpaceTemporalAA resolve, compiled at runtime and substituted at
// CreatePixelShader. The interface is fixed by the engine and reproduced exactly:
//
//   in   SV_POSITION (r0), TEXCOORD0 (r1)          out  SV_TARGET
//   t0 $u_Color_Curr      t1 $u_Color_Hist         t2 $u_GBuffer3_Curr (velocity)   t3 $u_MainDepth
//   s0 $u_Point_Sampler   s1 $u_Linear_Sampler
//   b2 ImageSpaceCommonCB b3 TemporalAACB
//
// The constant buffer registers are b2/b3, not b0/b1. RenderDoc's reflection lists them at array indices 0 and 1,
// which is not the bind point -- the original's `dcl_constantbuffer cb2[2]` and `cb3[5]` are what matter. Getting
// this wrong reads zeroes and produces a black screen.
//
// What is kept from the original, because it was already right:
//   - working in tonemapped space, c/(1+c), so bright pixels cannot dominate the neighbourhood
//   - the rounded neighbourhood clamp: average of the 3x3 box and the 5-tap cross, which is less prone to
//     over-clamping than either alone
//   - dilating the velocity lookup toward the nearest depth, which is what keeps silhouettes from ghosting
//
// What is changed, and why:
//   1. The centre-tap bypass is gone. The original computed
//          r1 = sharpFiltered + t2*(centreTap - sharpFiltered),  t2 = saturate(vel*0.5 + 1/(1+128*contrast))
//      so wherever local contrast was low it threw the reconstruction filter away and used the raw centre tap.
//      With jitter enabled that tap is a subpixel-shifted sample, so accumulating it over the phases blurs the
//      image -- which is exactly what "it looks fuzzier with jitter on" was. Verified by capture: a deliberately
//      destroyed reconstruction kernel reached the shader and barely changed the picture, because this line was
//      discarding it. The jitter-aware weights in TemporalAACB are now always what reconstructs the pixel.
//   2. History is fetched with a 9-tap Catmull-Rom instead of a single bilinear sample. Repeated bilinear
//      resampling at fractional offsets is the classic TAA blur source and compounds every frame.
//   3. The hard min(blend, 0.5) cap is replaced by a velocity-driven blend with a proper disocclusion path, so
//      history that has gone off screen is dropped outright instead of being clamped back in.
const char* const kTemporalAAResolveHLSL = R"HLSL(
Texture2D<float4> u_Color_Curr    : register(t0);
Texture2D<float4> u_Color_Hist    : register(t1);
Texture2D<float4> u_GBuffer3_Curr : register(t2);
// Declared four-component to match the original's `dcl_resource_texture2d (float,float,float,float) $u_MainDepth`.
// The SRV over the D24S8 target carries depth in .x; declaring it single-component would disagree with how the
// engine created the view.
Texture2D<float4> u_MainDepth     : register(t3);

SamplerState u_Point_Sampler  : register(s0);
SamplerState u_Linear_Sampler : register(s1);

cbuffer ImageSpaceCommonCB : register(b2)
{
    float4 u_UVAdjust;
    float4 u_OffsetNextTexel;      // xy = 1/size, zw = size
    float4 u_OffsetNextTexelHalf;
};

cbuffer TemporalAACB : register(b3)
{
    float4 u_Weight1;              // (-1,-1) (0,-1) (1,-1) (-1,0)
    float4 u_WeightLow1;
    float4 u_Weight2;              // ( 1, 0) (-1,1) (0, 1) ( 1,1)
    float4 u_WeightLow2;
    float4 u_WeightCenter;         // x = sharp centre, y = wide centre
};

float3 Tonemap(float3 c)    { return c / (1.0 + c); }
float3 TonemapInv(float3 c) { return c / max(1.0 - c, 1.0e-4); }
float  Luma(float3 c)       { return dot(c, float3(0.299, 0.587, 0.114)); }

// Nine bilinear taps weighted by the Catmull-Rom kernel. Sharper and far more stable under repeated reprojection
// than the single bilinear fetch the original used.
float3 SampleHistoryCatmullRom(float2 uv, float2 texSize, float2 invTexSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1   = floor(samplePos - 0.5) + 0.5;
    float2 f         = samplePos - texPos1;

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12      = w1 + w2;
    float2 offset12 = w2 / max(w12, 1.0e-5);

    float2 tp0  = (texPos1 - 1.0)       * invTexSize;
    float2 tp3  = (texPos1 + 2.0)       * invTexSize;
    float2 tp12 = (texPos1 + offset12)  * invTexSize;

    float3 r = 0.0;
    r += u_Color_Hist.SampleLevel(u_Linear_Sampler, float2(tp0.x,  tp0.y),  0).rgb * (w0.x  * w0.y);
    r += u_Color_Hist.SampleLevel(u_Linear_Sampler, float2(tp12.x, tp0.y),  0).rgb * (w12.x * w0.y);
    r += u_Color_Hist.SampleLevel(u_Linear_Sampler, float2(tp3.x,  tp0.y),  0).rgb * (w3.x  * w0.y);
    r += u_Color_Hist.SampleLevel(u_Linear_Sampler, float2(tp0.x,  tp12.y), 0).rgb * (w0.x  * w12.y);
    r += u_Color_Hist.SampleLevel(u_Linear_Sampler, float2(tp12.x, tp12.y), 0).rgb * (w12.x * w12.y);
    r += u_Color_Hist.SampleLevel(u_Linear_Sampler, float2(tp3.x,  tp12.y), 0).rgb * (w3.x  * w12.y);
    r += u_Color_Hist.SampleLevel(u_Linear_Sampler, float2(tp0.x,  tp3.y),  0).rgb * (w0.x  * w3.y);
    r += u_Color_Hist.SampleLevel(u_Linear_Sampler, float2(tp12.x, tp3.y),  0).rgb * (w12.x * w3.y);
    r += u_Color_Hist.SampleLevel(u_Linear_Sampler, float2(tp3.x,  tp3.y),  0).rgb * (w3.x  * w3.y);
    return r;
}

float4 main(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    const float2 invSize = u_OffsetNextTexel.xy;
    const float2 texSize = u_OffsetNextTexel.zw;

    // --- current frame neighbourhood, tonemapped -------------------------------------------------------------
    float3 s0 = Tonemap(u_Color_Curr.SampleLevel(u_Point_Sampler, uv, 0, int2(-1, -1)).rgb);
    float3 s1 = Tonemap(u_Color_Curr.SampleLevel(u_Point_Sampler, uv, 0, int2( 0, -1)).rgb);
    float3 s2 = Tonemap(u_Color_Curr.SampleLevel(u_Point_Sampler, uv, 0, int2( 1, -1)).rgb);
    float3 s3 = Tonemap(u_Color_Curr.SampleLevel(u_Point_Sampler, uv, 0, int2(-1,  0)).rgb);
    float3 s4 = Tonemap(u_Color_Curr.SampleLevel(u_Point_Sampler, uv, 0, int2( 0,  0)).rgb);
    float3 s5 = Tonemap(u_Color_Curr.SampleLevel(u_Point_Sampler, uv, 0, int2( 1,  0)).rgb);
    float3 s6 = Tonemap(u_Color_Curr.SampleLevel(u_Point_Sampler, uv, 0, int2(-1,  1)).rgb);
    float3 s7 = Tonemap(u_Color_Curr.SampleLevel(u_Point_Sampler, uv, 0, int2( 0,  1)).rgb);
    float3 s8 = Tonemap(u_Color_Curr.SampleLevel(u_Point_Sampler, uv, 0, int2( 1,  1)).rgb);

    // Reconstruction, using the jitter-aware weights the mod writes into TemporalAACB. No centre-tap bypass.
    float3 filtered = s0 * u_Weight1.x + s1 * u_Weight1.y + s2 * u_Weight1.z + s3 * u_Weight1.w
                    + s4 * u_WeightCenter.x
                    + s5 * u_Weight2.x + s6 * u_Weight2.y + s7 * u_Weight2.z + s8 * u_Weight2.w;

    float3 wide = s0 * u_WeightLow1.x + s1 * u_WeightLow1.y + s2 * u_WeightLow1.z + s3 * u_WeightLow1.w
                + s4 * u_WeightCenter.y
                + s5 * u_WeightLow2.x + s6 * u_WeightLow2.y + s7 * u_WeightLow2.z + s8 * u_WeightLow2.w;

    // --- rounded neighbourhood box, as the original built it -------------------------------------------------
    float3 boxMin = min(min(min(s0, s1), min(s2, s3)), min(min(s4, s5), min(min(s6, s7), s8)));
    float3 boxMax = max(max(max(s0, s1), max(s2, s3)), max(max(s4, s5), max(max(s6, s7), s8)));
    float3 crossMin = min(min(s1, s3), min(s4, min(s5, s7)));
    float3 crossMax = max(max(s1, s3), max(s4, max(s5, s7)));
    float3 clampMin = min(0.5 * (boxMin + crossMin), wide);
    float3 clampMax = max(0.5 * (boxMax + crossMax), wide);

    // --- velocity, dilated toward the nearest depth ----------------------------------------------------------
    float  dC = u_MainDepth.SampleLevel(u_Point_Sampler, uv, 0, int2( 0,  0)).x;
    float  dA = u_MainDepth.SampleLevel(u_Point_Sampler, uv, 0, int2(-2, -2)).x;
    float  dB = u_MainDepth.SampleLevel(u_Point_Sampler, uv, 0, int2( 2, -2)).x;
    float  dD = u_MainDepth.SampleLevel(u_Point_Sampler, uv, 0, int2(-2,  2)).x;
    float  dE = u_MainDepth.SampleLevel(u_Point_Sampler, uv, 0, int2( 2,  2)).x;

    float2 bestOffset = float2(0.0, 0.0);
    float  bestDepth  = dC;
    if (dA < bestDepth) { bestDepth = dA; bestOffset = float2(-2.0, -2.0); }
    if (dB < bestDepth) { bestDepth = dB; bestOffset = float2( 2.0, -2.0); }
    if (dD < bestDepth) { bestDepth = dD; bestOffset = float2(-2.0,  2.0); }
    if (dE < bestDepth) { bestDepth = dE; bestOffset = float2( 2.0,  2.0); }

    float2 velocity = u_GBuffer3_Curr.SampleLevel(u_Point_Sampler, uv + bestOffset * invSize, 0).xy;
    float2 histUV   = uv + velocity;
    float  velPx    = length(velocity * texSize);

    // --- history ---------------------------------------------------------------------------------------------
    float4 histRaw  = u_Color_Hist.SampleLevel(u_Linear_Sampler, histUV, 0);
    float3 history  = Tonemap(max(SampleHistoryCatmullRom(histUV, texSize, invSize), 0.0));
    float  histAlpha = histRaw.a;

    // Clip toward the wide-filtered current colour along the ray, rather than clamping per channel: clamping each
    // channel independently shifts hue on strong edges.
    float3 boxCentre = 0.5 * (clampMin + clampMax);
    float3 boxExtent = max(0.5 * (clampMax - clampMin), 1.0e-5);
    float3 offsetFromCentre = history - boxCentre;
    float3 unitDist = abs(offsetFromCentre) / boxExtent;
    float  maxUnit  = max(unitDist.x, max(unitDist.y, unitDist.z));
    if (maxUnit > 1.0) { history = boxCentre + offsetFromCentre / maxUnit; }

    // --- blend -----------------------------------------------------------------------------------------------
    // Reprojecting outside the frame means there is no history to keep, so take the current estimate outright
    // instead of clamping stale colour back in, which is what the original's hard 0.5 cap ended up doing.
    bool  offscreen = (histUV.x < 0.0) || (histUV.y < 0.0) || (histUV.x > 1.0) || (histUV.y > 1.0);
    float blend     = lerp(0.10, 0.50, saturate(velPx / 16.0));
    if (offscreen) { blend = 1.0; }

    float3 result = lerp(history, filtered, blend);

    // Alpha carries motion into the next frame, matching the original's feedback so the history stays meaningful.
    float outAlpha = max(histAlpha * 0.5, saturate(velPx * 2.0));

    return float4(max(TonemapInv(result), 0.0), outAlpha);
}
)HLSL";

}  // namespace EnigmaFix

#endif  // ENIGMAFIX_TEMPORALAARESOLVE_H
