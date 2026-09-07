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

#ifndef ENIGMAFIX_GROUNDTRUTHAO_H
#define ENIGMAFIX_GROUNDTRUTHAO_H

namespace EnigmaFix {

// Ground Truth Ambient Occlusion (Jimenez et al. 2016) replacing Mizuchi's ImageSpaceAO pass, compiled at runtime
// and substituted at CreatePixelShader. The interface is fixed by the engine and reproduced exactly:
//
//   in   SV_POSITION (r0), TEXCOORD0 (r1)          out  SV_TARGET, rendered at half resolution
//   t0 u_MainDepth (full res)    s0 u_MainDepthSampler
//   b0 PerViewCB (752)   b3 ImageSpaceCommonCB (96)
//
// Depth is the only input, deliberately. The engine builds several AO permutations and they disagree about the
// normal plane: one binds $u_GBuffer_0 at slot 1, one binds it at slot 0 with depth at slot 1, and the one that
// actually runs binds no second texture at all. Reading normals from it therefore means depending on which
// permutation a scene happens to build, which cost six wrong diagnoses before the runtime probe showed
// "Draw(3) inputs slot0 format 44 slot1 format 0". Normals are reconstructed from depth instead.
//
// Output contract, both channels load bearing:
//   .x  the AO term itself, which DeferredShading reads
//   .y  raw linear view depth, which the two ImageSpaceCrossBilateral passes use as their edge-stopping guide --
//       they bind no depth texture of their own, they difference this channel and pass it through untouched.
//       Verified numerically: the stock pass writes -1687.0 where the linearisation gives -1687.456, exact at
//       R16F precision. Writing anything else here makes the blur bleed across silhouettes.
//
// PerViewCB register assignments were recovered by matching known values against a raw dump of the buffer: ten
// 4x4 matrices occupy c0..c39, then seven float4s. Only the ones used are declared, the rest are padding.
//
// Temporal noise comes from u_PerFrameRandomValue and u_FrameIndex, which the engine already puts in
// ImageSpaceCommonCB. The pass is jitter aware exactly as the stock one was -- it subtracts the projection jitter
// when reconstructing view rays, so the TAA jitter does not smear the AO.
const char* const kGroundTruthAOHLSL = R"HLSL(
// Depth is the only input. The AO permutation that actually runs binds nothing at slot 1 -- confirmed at runtime,
// "Draw(3) inputs slot0 format 44 slot1 format 0" -- so normals are reconstructed from depth rather than read from
// GBuffer_0. Other permutations do bind the normal plane, and one binds it at slot 0 with depth at slot 1, so
// depending on it at all means depending on which permutation the scene happens to build. This does not.
Texture2D<float4> u_MainDepth  : register(t0);
SamplerState u_MainDepthSampler  : register(s0);

cbuffer PerViewCB : register(b0)
{
    row_major float4x4 u_ViewMatrix              : packoffset(c0);
    row_major float4x4 u_ProjectionMatrix        : packoffset(c4);
    float4             u_ProjRatio               : packoffset(c42);
    float4             u_ZPlane                  : packoffset(c43);
    float4             u_Frustum                 : packoffset(c44);
    float4             u_CubeMapFetchScaler      : packoffset(c45);
    float4             u_ViewportSizeJitterOffset: packoffset(c46);
};

cbuffer ImageSpaceCommonCB : register(b3)
{
    float4 u_UVAdjust                : packoffset(c0);
    float4 u_OffsetNextTexel         : packoffset(c1);   // half res: 1/w, 1/h, w, h
    float4 u_OffsetNextTexelHalf     : packoffset(c2);
    float4 u_OriginalOffsetNextTexel : packoffset(c3);   // full res: 1/w, 1/h, w, h
    float4 u_PerFrameRandomValue     : packoffset(c4);
    uint4  u_FrameIndex              : packoffset(c5);
};

cbuffer AOCB : register(b12)
{
    float4 u_AOParameters0     : packoffset(c0);
    float4 u_AOParameters1     : packoffset(c1);
    float4 u_AOParametersGroup : packoffset(c2);
};

#define EF_PI      3.14159265359
#define EF_SLICES  2
#define EF_STEPS   6

// Radius in world units and final strength. Deliberately literals rather than reusing AOCB: those constants are
// tuned for the engine's own falloff and mean nothing to a horizon search.
static const float kRadiusWorld  = EF_AO_RADIUS;
static const float kIntensity    = EF_AO_INTENSITY;
static const float kMaxRadiusPx  = 96.0;   // Caps the screen-space march so near-camera pixels cannot thrash cache.
static const float kThicknessMix = 0.35;   // Heuristic for the unknowable thickness behind a depth sample.

// Matches the engine's own linearisation, lines 4-6 of the stock pass. Returns view space z, which is negative.
float LinearViewZ(float rawDepth)
{
    float num = rawDepth * u_ProjRatio.w - u_ProjRatio.y;
    float den = -rawDepth * u_ProjRatio.z + u_ProjRatio.x;
    return num / den;
}

// The projection carries the TAA jitter, so it has to come back out before a UV can be turned into a view ray.
// The stock pass does the same thing at lines 10-13.
float2 UnjitteredNDC(float2 uv)
{
    float2 jitterNDC = (u_ViewportSizeJitterOffset.zw + u_ViewportSizeJitterOffset.zw)
                     / u_ViewportSizeJitterOffset.xy;
    return uv * float2(2.0, -2.0) + float2(-1.0, 1.0) - jitterNDC;
}

// Right handed view space with -z forward, built from the projection rather than from u_Frustum. Both agree --
// u_Frustum is (left, right, top, bottom) at the near plane and 1/m00 * near = 9.5556 -- but going through m00/m11
// keeps the sign convention explicit instead of inheriting it from a packed vector.
float3 ViewPosition(float2 uv, float rawDepth)
{
    float  viewZ = -LinearViewZ(rawDepth);        // positive distance along the forward axis
    float2 ndc   = UnjitteredNDC(uv);
    float2 xy    = float2(ndc.x / u_ProjectionMatrix[0][0], ndc.y / u_ProjectionMatrix[1][1]) * viewZ;
    return float3(xy, -viewZ);
}

// Reconstructed from the depth buffer using the closer of the forward and backward difference on each axis, which
// is what keeps silhouettes from producing normals that lean across the discontinuity. The result is forced to face
// the viewer, so there is no handedness assumption left to get wrong.
float3 ReconstructViewNormal(float2 uv, float3 centre)
{
    float2 texel = u_OriginalOffsetNextTexel.xy;

    float3 left  = ViewPosition(uv - float2(texel.x, 0.0), u_MainDepth.SampleLevel(u_MainDepthSampler, uv - float2(texel.x, 0.0), 0).x);
    float3 right = ViewPosition(uv + float2(texel.x, 0.0), u_MainDepth.SampleLevel(u_MainDepthSampler, uv + float2(texel.x, 0.0), 0).x);
    float3 down  = ViewPosition(uv - float2(0.0, texel.y), u_MainDepth.SampleLevel(u_MainDepthSampler, uv - float2(0.0, texel.y), 0).x);
    float3 up    = ViewPosition(uv + float2(0.0, texel.y), u_MainDepth.SampleLevel(u_MainDepthSampler, uv + float2(0.0, texel.y), 0).x);

    float3 dx = (abs(right.z - centre.z) < abs(centre.z - left.z)) ? (right - centre) : (centre - left);
    float3 dy = (abs(up.z    - centre.z) < abs(centre.z - down.z)) ? (up    - centre) : (centre - down);

    float3 normal = cross(dx, dy);
    float  len    = length(normal);
    if (len < 1.0e-8) { return float3(0.0, 0.0, 1.0); }
    normal /= len;
    return (dot(normal, -centre) < 0.0) ? -normal : normal;
}

float4 main(float4 svpos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    float rawDepth = u_MainDepth.SampleLevel(u_MainDepthSampler, uv, 0).x;
    float linearZ  = LinearViewZ(rawDepth);

    // Sky and anything past the usable range gets no occlusion, but still has to carry a depth guide or the
    // bilateral will pull occluded pixels across the horizon.
    if (rawDepth >= 0.99999)
    {
        return float4(1.0, linearZ, 0.0, 0.0);
    }

    float3 P = ViewPosition(uv, rawDepth);
    float3 V = normalize(-P);

    float3 N = ReconstructViewNormal(uv, P);

    // World radius projected to screen. m11 * 0.5 * height converts a view space length at this depth into pixels.
    float viewDist    = max(-P.z, 1.0e-4);
    float radiusPx    = kRadiusWorld * (u_ProjectionMatrix[1][1] * 0.5 * u_OriginalOffsetNextTexel.w) / viewDist;
    radiusPx          = min(radiusPx, kMaxRadiusPx);
    if (radiusPx < 1.0) { return float4(1.0, linearZ, 0.0, 0.0); }
    float2 radiusUV   = radiusPx * u_OriginalOffsetNextTexel.xy;

    // Interleaved gradient noise on the half res pixel, offset per frame. The engine already supplies both pieces,
    // and the TAA downstream is what turns two slices per frame into a converged result.
    float2 pixel      = svpos.xy + u_PerFrameRandomValue.xy * 64.0;
    float  noise      = frac(52.9829189 * frac(0.06711056 * pixel.x + 0.00583715 * pixel.y));
    float  noiseSlice = frac(noise + u_PerFrameRandomValue.z);
    float  noiseStep  = frac(noise * 1.6180339887 + u_PerFrameRandomValue.w);

    float visibility = 0.0;

    [unroll]
    for (int slice = 0; slice < EF_SLICES; ++slice)
    {
        float  phi      = (float(slice) + noiseSlice) * (EF_PI / float(EF_SLICES));
        float2 sliceDir = float2(cos(phi), sin(phi));

        float3 direction = float3(sliceDir, 0.0);
        float3 axis      = normalize(cross(direction, V));
        float3 projected = N - axis * dot(N, axis);
        float  projLen   = length(projected);

        if (projLen < 1.0e-5) { continue; }

        float3 projNormal = projected / projLen;
        float3 tangent    = cross(V, axis);
        float  cosN       = clamp(dot(projNormal, V), -1.0, 1.0);
        float  angleN     = -sign(dot(projNormal, tangent)) * acos(cosN);

        float2 horizonCos = float2(-1.0, -1.0);

        [unroll]
        for (int side = 0; side < 2; ++side)
        {
            float sideSign = (side == 0) ? -1.0 : 1.0;
            float best     = -1.0;

            [unroll]
            for (int step = 0; step < EF_STEPS; ++step)
            {
                float  t        = (float(step) + noiseStep) / float(EF_STEPS);
                float2 sampleUV = uv + sideSign * sliceDir * (t * radiusUV);

                float sampleDepth = u_MainDepth.SampleLevel(u_MainDepthSampler, sampleUV, 0).x;
                if (sampleDepth >= 0.99999) { continue; }

                float3 delta  = ViewPosition(sampleUV, sampleDepth) - P;
                float  len    = length(delta);
                if (len < 1.0e-5) { continue; }

                float cosH = dot(delta, V) / len;

                // Beyond the radius a sample says nothing about occlusion here. Fading to -1 rather than rejecting
                // outright keeps the horizon continuous, which is what stops the result from banding.
                float fade = saturate((len - kRadiusWorld) / max(kRadiusWorld * kThicknessMix, 1.0e-4));
                cosH = lerp(cosH, -1.0, fade);

                best = max(best, cosH);
            }

            horizonCos[side] = best;
        }

        // The GTAO visibility integral. Clamping each horizon to within pi/2 of the normal is what makes this
        // ground truth rather than a cosine-weighted approximation.
        float h1 = angleN + max(-acos(clamp(horizonCos.x, -1.0, 1.0)) - angleN, -EF_PI * 0.5);
        float h2 = angleN + min( acos(clamp(horizonCos.y, -1.0, 1.0)) - angleN,  EF_PI * 0.5);

        float sinN = sin(angleN);
        float arc1 = 0.25 * (-cos(2.0 * h1 - angleN) + cos(angleN) + 2.0 * h1 * sinN);
        float arc2 = 0.25 * (-cos(2.0 * h2 - angleN) + cos(angleN) + 2.0 * h2 * sinN);

        visibility += projLen * (arc1 + arc2);
    }

    visibility /= float(EF_SLICES);

    float ao = saturate(pow(saturate(visibility), kIntensity));

    // .y must stay raw linear view depth: the cross bilateral passes have no depth of their own and difference
    // this channel to find edges.
    return float4(ao, linearZ, 0.0, 0.0);
}
)HLSL";

}  // namespace EnigmaFix

#endif  // ENIGMAFIX_GROUNDTRUTHAO_H
