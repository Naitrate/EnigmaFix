# Ambient Occlusion — Mizuchi / DERQ 1

EnigmaFix, 3440x1440 on Linux/Proton. Session 2026-09-07. **GTAO is parked, not working.**

The shader is written and compiles; the problem is entirely one of getting it bound to the right draw. Eight
attempts failed. This records what is known so the next attempt does not re-derive it.

---

## 1. The pass

`ImageSpaceAO [Technique:ImageSpaceAO]`, one fullscreen `Draw(3)`, followed by `ImageSpaceCrossBilateralH` and
`ImageSpaceCrossBilateralV`.

| | |
|---|---|
| render target | **1720x720 R16G16_FLOAT** (half res) |
| cbuffers | `PerViewCB` b0, `ImageSpaceCommonCB` b3, sometimes `AOCB` b12 and `MaterialShadingParametersCB` b13 |
| output `.x` | the AO term, read by `DeferredShading` |
| output `.y` | **raw linear view depth** — see §4 |

## 2. There are at least four permutations and they disagree about their inputs

This is the single most important fact, and the cause of every failed attempt. Observed across captures and the
runtime probe:

| inputs | notes |
|---|---|
| `u_MainDepth` (t0), `$u_GBuffer_0` (t1) | with `AOCB` + `MaterialShadingParametersCB`, 524 lines |
| `u_MainDepth` (t0), `u_GBuffer_0` (t1) | no `$`, no `AOCB`, 856 lines |
| slot0 = `R16G16B16A16_UNORM`, slot1 = depth | **reversed order** |
| **depth alone, nothing at slot 1** | 893 lines — *this is what runs in-game* |

So: the `$` prefix is not stable, `AOCB` is not always bound, the binding order is not stable, and the normal
plane is not always bound at all. Any identification rule that depends on those is a coin flip.

## 3. What does not work

**Matching at `CreatePixelShader` is a dead end, twice over.**

First, the shader the game actually uses is never created while the hook is live — a probe logging every distinct
texture signature arriving at that hook recorded 37 signatures across a whole session and the in-game AO shader was
not among them. It is created before the hooks install.

Second, and worse: "exactly two textures named `u_MainDepth` and `u_GBuffer_0`" does **not** describe the AO pass.
It describes **`DownSampleGBuffer0`**, which pairs the same two and writes the downsampled normal planes at
1720x720, 960x540 and 640x360. Replacing it corrupted the downsample chain and produced vertical striping in
indoor scenes. That path is now disabled behind `if (false && ...)` — do not re-enable it without a discriminator
that includes the render target.

**Draw-time substitution does not currently reach the pass either.** With a probe logging every `Draw(3)`, all
lines stop before the level loads. In-game, no fullscreen draw reaches the hook — not even the cross bilateral
passes, which had been reaching it in an earlier session. The capture plainly shows `ID3D11DeviceContext::Draw()`
with 3 vertices, so the call exists and is not being intercepted.

## 4. The output contract, which is verified and worth keeping

`.y` must carry **raw linear view depth**, not the normalised variant. The two cross bilateral passes bind no depth
texture of their own: they difference this channel for edge stopping and pass it through untouched. Verified
numerically -- the stock pass writes `-1687.0` where the linearisation gives `-1687.456`, exact at R16F precision.

Linearisation, matching the stock pass:

```
linearZ = (d*u_ProjRatio.w - u_ProjRatio.y) / (-d*u_ProjRatio.z + u_ProjRatio.x)
```

## 5. PerViewCB register map

Recovered by matching known values against a raw dump. Ten 4x4 matrices at c0..c39, then seven float4s:

| register | field |
|---|---|
| c0 | `u_ViewMatrix` (row major) |
| c4 | `u_ProjectionMatrix` — m00 1.0465, m11 2.5 |
| c40 | camera world position |
| c42 | `u_ProjRatio` |
| c43 | `u_ZPlane` — near 10, far 204800 |
| c44 | `u_Frustum` — (left, right, top, bottom) at the near plane |
| c46 | `u_ViewportSizeJitterOffset` |

`ImageSpaceCommonCB` at b3 is the 96 byte variant and already carries `u_PerFrameRandomValue` and `u_FrameIndex`,
so GTAO's temporal rotation needs nothing injected. The stock pass is jitter aware — it subtracts the projection
jitter when reconstructing view rays, and the replacement must too.

## 6. State of the shader

`Source/Shaders/GroundTruthAO.h` compiles cleanly (44384 bytes DXBC, validated under wine against the game's own
`d3dcompiler_47.dll`). It takes **depth only** and reconstructs view normals from it, precisely so it does not
depend on which permutation binds what. Its binding table is `t0 u_MainDepth`, `b0 PerViewCB`, `b3
ImageSpaceCommonCB` — a subset of every permutation, so it is safe to bind over any of them.

It has never actually executed, so it is unvalidated beyond compiling.

## 7. The next thing to test

**Whether `hkDraw` is called at all in-game.** Not whether the filter is right — the filter has been rewritten six
times and the evidence has consistently said the pass is not arriving. A counter of `hkDraw` and `hkDrawIndexed`
invocations per frame, compared against the draw count in a capture, settles it in one run.

If the counts do not match, the likely explanation is a deferred context recording into a command list
(`ExecuteCommandList` appears elsewhere in this engine), which would need the deferred context's vtable hooked
rather than the immediate one.

## 8. Method note

Eight wrong diagnoses, each a plausible reading of real evidence. The pattern: every fix narrowed a filter, and
none of the filters could report what they were rejecting. The moment a probe logged its *input* rather than its
output, the answer appeared in one line of text.

The other lesson is sharper. A matcher that is merely wrong costs a test run; a matcher that is wrong and silently
replaces an unrelated pass corrupts rendering somewhere else entirely, and the only reason it was caught is that
the user noticed striping. Any shader substitution rule needs a discriminator that cannot match a pass with a
different render target.
