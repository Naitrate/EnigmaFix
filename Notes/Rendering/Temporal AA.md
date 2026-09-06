# Temporal AA — Mizuchi / DERQ 1

EnigmaFix, 3440x1440 on Linux/Proton. Session 2026-09-06.

The engine ships a complete jittered-TAA implementation and then feeds it zero jitter. Everything needed was
already in the shaders; the work was turning it on and replacing the one pass that made it useless.

---

## 1. How to find any of this yourself

The engine annotates every pass through `ID3DUserDefinedAnnotation`, so RenderDoc names them outright:
`TemporalAA [Technique:ImageSpaceTemporalAA]`, `ImageSpaceAO`, `ImageSpaceReflection`, `GBuffer`,
`LayeredShadowing`, `CopyDeferredColor_Hist`, and so on. Shaders keep full reflection data, so texture slots
arrive named (`$u_Color_Curr`, `$u_Color_Hist`). No guessing is required anywhere in this file.

## 2. The pipeline

`TemporalAA` marker holds one fullscreen `Draw(3)`, then `CopyDeferredColor_Hist` copies the result into the
history buffer for next frame.

| slot | name | format |
|---|---|---|
| t0 | `$u_Color_Curr` | R11G11B10_FLOAT, full res |
| t1 | `$u_Color_Hist` | R16G16B16A16_FLOAT |
| t2 | `$u_GBuffer3_Curr` | **R16G16_SNORM — motion vectors** |
| t3 | `$u_MainDepth` | D24S8, depth in `.x` |
| s0 / s1 | point / linear samplers | |
| b2 / b3 | `ImageSpaceCommonCB` (48b) / `TemporalAACB` (80b) | |

**Constant buffers are at registers b2 and b3.** RenderDoc's reflection lists them at array indices 0 and 1,
which is *not* the bind point. The original's `dcl_constantbuffer cb2[2]` / `cb3[5]` is what matters. Getting
this wrong reads zeroes and produces a black screen.

Marker also holds a second draw (PS `2693`) that binds only `$u_Color_Curr` — not a resolve, do not patch it.
At least two resolve variants exist across sessions (`2687`, `2688`).

## 3. Velocity is in UV space and is jitter-free

Measured while moving: signed, spatially varying, ~0.0276 at the largest (95px). Larger on near geometry,
near-zero at distance — correct parallax.

The GBuffer pixel shader **recomputes** the current clip position from the interpolated world position rather
than reusing `SV_POSITION`:

```
166: div r2.xy, r2.xyxx, r2.zzzz      // current NDC, no jitter term
167: add r0.xz, r0.xxzx, -r2.xxyx     // prevNDC - currNDC
168: mul o3.xy, r0.xzxx, l(0.5,-0.5)  // -> UV-space velocity
```

So velocity is a difference of two *unjittered* positions. **Enabling jitter cannot corrupt motion vectors.**
That was the one thing that could have made this a dead end.

## 4. Jitter: present, wired, fed zero

`PerViewCB` (752 bytes) ends with `u_ViewportSizeJitterOffset = (width, height, jitterX, jitterY)` at byte
offset **736**. Every scene vertex shader opens with:

```
0: add r0.xy, u_ViewportSizeJitterOffset.zwzz, u_ViewportSizeJitterOffset.zwzz
1: div r0.xy, r0.xyxx, u_ViewportSizeJitterOffset.xyxx   // 2*jitterPx/viewportSize -> NDC
...
39: mad o0.xy, r0.xyxx, r4.wwww, r4.xyxx                 // clip.xy += jitterNDC * clip.w
```

Textbook jitter application. `.zw` measured as `(-0.0, 0.0)` on both a static and a moving frame. The game
pays TAA's full blur cost and gets none of its supersampling.

`PerViewCB` is `Map`-written **twice per frame** (`CPUWrite` at two events out of 611 usages), so both writes
must carry the same offset — the sequence advances in `Present`, not per `Map`, or the GBuffer and forward
passes jitter against each other.

Sign convention, derived rather than guessed:

```
ndc.x += 2*jx/W  =>  screen.x += jx      (NDC +x is right)
ndc.y += 2*jy/H  =>  screen.y -= jy      (NDC +y is up, texel +y is down)
```

A tap at texel offset `(dx,dy)` samples the true scene at `(dx - jx, dy + jy)`.

## 5. The `t2` centre-tap bypass — the finding that cost the most

This is the one worth remembering. The stock resolve computes:

```
r1 = sharpFiltered + t2*(centreTap - sharpFiltered)
t2 = saturate(min(L1vel,1)*0.5 + 1/(1 + 128*contrast))
```

Standing still in anything low-contrast, `contrast -> 0`, so `t2 -> 1` and **the reconstruction filter is
discarded entirely** in favour of the raw centre tap. With jitter on, that tap is a subpixel-shifted sample, so
accumulating it over the phases blurs the image.

Consequences that all looked like separate mysteries:

- jitter alone made the image *fuzzier*, not sharper
- recentring the reconstruction filter on the jitter helped **specular aliasing only** — the high-contrast case
  where `t2` is small and the filter survives
- a sharpness slider driving the filter width did nothing visible
- a diagnostic that put the entire filter on one corner tap and flipped corners every frame **did not visibly
  vibrate the screen**

That last one was the trap: it was designed to prove the constant-buffer write reached the shader, and its null
result was consistent with *both* "write fails" and "write lands but is discarded". A capture settled it —
`TemporalAACB` at the draw contained `u_Weight1 = (1,0,0,0)`, `u_WeightCenter = (0, 0.128, 0, 0)`. **The write
landed and the shader ignored it.**

Lesson: a diagnostic whose null result has two explanations is not a diagnostic. Capture the buffer instead.

## 6. TemporalAACB layout

80 bytes, rewritten by the engine every frame (`CPUWrite` immediately before the draw), so it is patchable at
`Map`/`Unmap` with no shader work.

| float4 | taps |
|---|---|
| `u_Weight1` | (-1,-1) (0,-1) (1,-1) (-1,0) — sharp |
| `u_WeightLow1` | same taps — wide |
| `u_Weight2` | (1,0) (-1,1) (0,1) (1,1) — sharp |
| `u_WeightLow2` | same taps — wide |
| `u_WeightCenter` | `.x` sharp centre, `.y` wide centre |

Both shipped kernels are **exactly separable Gaussians**:
`sqrt(0.5516272) * sqrt(0.0165487) = 0.0955444`, matching the edge weight to seven digits; each sums to 1.0.
Sharp sigma **0.534 px**, wide sigma **2.136 px**.

So the width can be recovered from whatever the engine wrote and the kernel rebuilt around the jitter offset,
which reproduces the originals to ~1e-8 at zero jitter and survives a quality-preset change.

**Recover the baseline from a *symmetric* kernel only.** Deriving it in place looked fine and was wrong: after
the first patch the buffer holds a recentred, asymmetric kernel, so `sqrt(corner)/sqrt(centre)` stops being the
side-to-centre ratio. That fed each frame's width from the previous frame's output and settled on a fixed point
that barely moved when the setting changed — which is exactly what "the slider does nothing" looked like from
outside. Only the engine writes a symmetric kernel, so symmetry identifies an untouched buffer.

## 7. What was done

| change | where |
|---|---|
| Halton(2,3) jitter, 8 phases, into `PerViewCB+736` | `hkMap`/`hkUnmap`, advanced in `hkPresent` |
| Jitter-aware reconstruction weights + sharpness | `ApplyJitteredResolveWeights` |
| Replacement resolve pixel shader | `Source/Shaders/TemporalAAResolve.h`, swapped at `CreatePixelShader` |

The replacement keeps what was already right — tonemapped working space `c/(1+c)`, the rounded 3x3+cross clamp
box, depth-dilated velocity — and changes three things: no `t2` bypass, 9-tap Catmull-Rom history instead of one
bilinear fetch, and a velocity-driven blend with a real disocclusion path instead of `min(blend, 0.5)`.

Shader identification is by **content, not hash**: the bytecode is searched for `u_Color_Hist` and
`u_GBuffer3_Curr`, which live as strings in the RDEF chunk. That catches every variant automatically; hashing
would mean chasing them one at a time.

Compiled at runtime via `d3dcompiler_47.dll`, which ships next to `Application.exe`. Loaded dynamically, not
linked. No `winedlloverride` needed under Proton.

### Validating the shader without launching the game

```bash
# cross-compile a harness that calls D3DCompile, run it under wine
x86_64-w64-mingw32-gcc fxc.c -o fxc.exe -L<mcfgthread lib dir>
WINEDEBUG=-all wine ./fxc.exe resolve.hlsl
```

This caught a real bug before deployment: depth had been declared `Texture2D<float>` when the original is
`dcl_resource_texture2d (float,float,float,float)`.

## 8. Still open

- Reconstruction sharpness remains a weak lever even with the bypass gone; the accumulation dominates.
- `TAAJitter` and `TAAReplaceResolve` both default to `false`.
- GTAO will want jitter for its sample rotation — it now inherits a working one.
