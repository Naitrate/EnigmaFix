# Mizuchi engine — renderer conventions

Cross-game reference. Verified against **DERQ 1** (appid 990050) and **DERQ 2** (appid 1266220), both D3D11.
The DERQ-specific findings live in the sibling notes; this file is what should port to any Mizuchi title.

---

## 1. Every pass is named

The engine annotates the frame through `ID3DUserDefinedAnnotation`, so RenderDoc's event browser names every
technique outright — `TemporalAA [Technique:ImageSpaceTemporalAA]`, `ImageSpaceAO`, `DeferredShading`,
`LayeredShadowing`, and so on. Shaders also keep full reflection data, so texture slots arrive named
(`$u_Color_Hist`, `u_MainDepth`). **Nothing in this engine needs to be identified by guesswork.**

Start any investigation on a new title by listing the technique markers and diffing against the list in §7.

## 2. PerViewCB — 752 bytes, identical in both games

Ten 4x4 matrices at c0..c39, then seven float4s. Verified byte-identical in size and field position across DERQ1
and DERQ2, so these register offsets are safe to reuse:

| register | field |
|---|---|
| c0 | `u_ViewMatrix` (row major) |
| c4 | `u_ProjectionMatrix` |
| c8 | `u_LinearProjectionMatrix` (often zero) |
| c12 | `u_ViewProjectionMatrix` |
| c16 | `u_ViewLinearProjectionMatrix` (often zero) |
| c20 | `u_InverseViewMatrix` |
| c24 | `u_InverseProjectionMatrix` |
| c28 | `u_InverseViewProjectionMatrix` |
| c32 | `u_PreviousViewProjectionMatrix` |
| c36 | `u_BillboardMatrix` |
| c40 | camera world position |
| c42 | `u_ProjRatio` |
| c43 | `u_ZPlane` — near, far, flag, ? |
| c44 | `u_Frustum` — left, right, top, bottom at the near plane |
| c45 | `u_CubeMapFetchScaler` |
| c46 | **`u_ViewportSizeJitterOffset`** — width, height, jitterX, jitterY (pixels) |

Depth linearisation, as the engine's own passes do it:

```
linearZ = (d*u_ProjRatio.w - u_ProjRatio.y) / (-d*u_ProjRatio.z + u_ProjRatio.x)     // negative, view space
```

View space position, right handed with -z forward:

```
viewZ = -linearZ
ndc   = uv*float2(2,-2) + float2(-1,1) - jitterNDC
xy    = float2(ndc.x / m00, ndc.y / m11) * viewZ
```

## 3. ImageSpaceCommonCB comes in two sizes

- **48 bytes** — `u_UVAdjust`, `u_OffsetNextTexel`, `u_OffsetNextTexelHalf`
- **96 bytes** — the above plus `u_OriginalOffsetNextTexel` (full res), **`u_PerFrameRandomValue`** and
  **`u_FrameIndex`**

`u_OffsetNextTexel` is `(1/w, 1/h, w, h)` of the **current target**, so it is half resolution in a half res pass;
`u_OriginalOffsetNextTexel` is full resolution. The random value and frame index are already there for any
temporally-jittered technique — nothing needs injecting.

**Bind points vary per shader.** It was b2 in DERQ1's TAA resolve and b3 in the AO pass. Read the actual
`dcl_constantbuffer cbN[..]` from the disassembly; RenderDoc's reflection lists constant blocks in array order,
which is *not* the bind point. Getting this wrong reads zeroes and produces a black screen.

## 4. GBuffer layout

| plane | format | contents |
|---|---|---|
| `GBuffer_0` | R16G16B16A16_UNORM | `.xy` octahedral normal (16 bit), `.z` low 12 bits = material id, `.w` packed flags |
| `GBuffer_1` | R8G8B8A8_SRGB | albedo |
| `GBuffer_2` | R8G8B8A8_UNORM | surface parameters |
| `GBuffer_3` | **R16G16_SNORM** | **motion vectors, UV space** |
| `GBuffer_4` | R11G11B10_FLOAT | emissive |

Octahedral decode, lifted from `DeferredShading`:

```
e = GBuffer_0.xy*2 - 1 ; n = float3(e, 1 - |e.x| - |e.y|) ; fold on sign when n.z < 0 ; normalize
```

Normals are **world space**; multiply by `u_ViewMatrix` for view space.

Velocity is `prevUV - currUV` in UV units and is built from **unjittered** positions on both sides — the GBuffer
pixel shader recomputes the current clip position from the interpolated world position rather than reusing
`SV_POSITION`. Enabling projection jitter therefore does not corrupt motion vectors.

## 5. The temporal AA jitter is implemented and fed zero

This is the single most portable finding. Every scene vertex shader contains:

```
0: add r0.xy, u_ViewportSizeJitterOffset.zwzz, u_ViewportSizeJitterOffset.zwzz
1: div r0.xy, r0.xyxx, u_ViewportSizeJitterOffset.xyxx     // 2*jitterPx / viewportSize -> NDC
…
39: mad o0.xy, r0.xyxx, r4.wwww, r4.xyxx                   // clip.xy += jitterNDC * clip.w
```

Textbook jitter application, complete and wired — and `.zw` is `(0,0)`:

| game | observed |
|---|---|
| DERQ 1 | `(3440, 1440, -0.0, 0.0)` |
| DERQ 2 | `(2560, 1440, 0.0, -0.0)` |

So both titles pay temporal AA's full blur cost and get none of its supersampling. Driving `.zw` with a Halton
sequence turns temporal smoothing into real supersampling. See `Temporal AA.md` for the resolve shader's
centre-tap bypass, which has to be removed for the jitter to actually resolve anything.

Passes that reconstruct view rays **subtract the jitter first** — any replacement must do the same.

## 6. Two traps worth stating plainly

**Bind points and slot order are not stable across permutations.** The engine builds several variants of the same
technique, and they disagree: a resource may be `$u_GBuffer_0` in one and `u_GBuffer_0` in another; the AO pass
binds depth+normals, normals+depth, or depth alone depending on the permutation. Never identify a pass by a
binding name or slot index alone. See `Ambient Occlusion.md` for how expensive that lesson was.

**Techniques can be present but empty.** `LayeredShadowing` and `ImageSpaceShadowFilter1/2` emit markers with no
draws at all in both games — the feature exists in the engine and is switched off. When a title looks like it is
missing a feature, check whether the technique is there and inert before assuming it needs porting.

## 7. Technique list, DERQ1 vs DERQ2

Essentially identical. `DeferredShading`'s binding table matches exactly — same GBuffer planes, `$u_AOBuffer`,
`$u_DepthBuffer`, `$u_DirectionalShadowMap`, `u_LightData`/`u_LightDataNonShadow`/`u_LightIndexList`/`u_LightGrid`,
same samplers, `PerViewCB` 752, `AOCB` 48, `MaterialShadingParametersCB` 65520, `TileBasedCommonCB` 32. The only
difference found is `PerSceneCB`, 160 bytes in DERQ1 against 656 in DERQ2.

**DERQ2 has, DERQ1 does not:** a volumetric fog chain —
`ImageSpaceFogForFilter`, `ImageSpaceFogBoxFilter`, `ImageSpaceFogHorizontalFilter`,
`ImageSpaceFogVerticalFilter`, `ImageSpaceCompositeFog` — plus `CopyToMainSurface`.

Both have the `yebismizuchi2` post processing block (bloom pyramid, glare, tonemap composite).

The practical consequence: work written against these conventions should port between the two with offset checks
rather than rewrites, and a renderer gap between titles is more likely to be a disabled feature than a missing one.

## 8. Tooling that transfers

- Runtime shader replacement via `CreatePixelShader` (kiero index 33) or, when the shader is created before hooks
  install, substitution at draw time. `CreateSamplerState` is 41, `Map`/`Unmap` are 75/76, `Draw` 74,
  `DrawIndexed` 73, `CreateComputeShader` 36.
- Compile replacements at runtime through the `d3dcompiler_47.dll` that ships beside the executable, loaded
  dynamically. No `winedlloverride` needed under Proton.
- Validate a replacement shader **before** deploying by cross-compiling a small `D3DCompile` harness with mingw and
  running it under wine against that same DLL. This has caught real bugs at zero cost.
- Identify shaders by parsing the DXBC `RDEF` chunk rather than substring matching: "DXBC", 16 byte hash, version,
  size, chunk count, chunk offsets; RDEF holds a resource binding array of 32 byte entries for shader model 5,
  each `nameOffset, type, returnType, dimension, numSamples, bindPoint, bindCount, flags`, with type 2 = texture.
