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

---

## 9. The post processing settings struct

Both games apply post processing settings the same way: a run of `mov [base + offset], al` stores into one settings
struct, inside the routine the options menu calls. Forcing `AL` at the store is how every toggle in this mod works,
and it is also what the Cheat Engine tables in the repo root do — those tables are the origin of the DERQ1
signatures, and are worth reading before deriving anything by hand.

The struct **layouts differ between games**; they are not the same offsets shifted. Base register differs too.

### DERQ 1 — base `rcx+r13`

| offset | feature | RVA |
|---|---|---|
| 0x04 | Color Correction | 29C858 |
| 0x58 | Motion Blur | 29CFF3 |
| 0x60 | Shutter Ratio (float) | 29D056 |
| 0x64 | Max Blur Length (float) | 29D093 |
| 0x68 | Post / generic AA | 29CF64 |
| 0x69 | Temporal AA | 29CF8D |
| 0x70 | Tonemapping | 29C420 |
| 0xBC | **Glare** | 29CB44 |
| 0xEC | Lens Flare | 29CE43 |
| 0x118 | Depth of Field | 29C600 |
| 0x160 | Camera Distortion | 29CEF5 |
| 0x164 | Vignette intensity (float) | 29CF36 |
| 0x168 | SSAO | 29D0BE |
| 0x1C4 | RLR | 29D3DF |
| — | Fog, base `rcx+rsi` +0x04 | 29D495 |

Note 0xBC: the table calls it **Glare**, and the mod exposes it as `RS.Bloom`. In YEBIS terms those are the same
effect, but the table adds "needs to be disabled alongside Depth of Field to prevent glare flickering".

Two DERQ1 entries the mod does **not** implement yet:
- **Disable all post processing** — `cmp eax,01` at `6D8639` patched to `cmp eax,00`. The mod finds this signature
  and only logs it.
- **Vignette intensity** — the table redirects the store at `29CF36` to read a float it owns, so intensity is
  adjustable rather than just on/off. The mod hardcodes `0.0f`.

### DERQ 2 — base `r14+r15`

Verified against the installed executable: every one of these stores is **unique in `.text` on its own**, so the
five to eight byte anchor is a sufficient signature — no trailing context needed, unlike DERQ1.

| offset | feature | RVA | signature |
|---|---|---|---|
| 0x0C | Color Correction | 77BFE1 | `43 88 44 3E 0C` |
| 0x60 | Motion Blur | 77D31D | `43 88 44 3E 60` |
| 0x68 | Shutter Ratio (float) | 77D3D2 | `movss [r14+r15+68],xmm1` |
| 0x6C | Max Blur Length (float) | 77D430 | `movss [r14+r15+6C],xmm1` |
| 0x70 | Generic AA | 77CDB1 | `43 88 44 3E 70` |
| 0x71 | Temporal AA | 77CDFD | `43 88 44 3E 71` |
| 0x78 | **SMAA enable** | 77CEAC | `mov [r14+r15+78],eax` |
| 0x7C | **SMAA threshold** (float, default 0.1) | 77CF0F | `movss [r14+r15+7C],xmm1` |
| 0xA4 | unknown | 77B841 | `43 88 84 3E A4 00 00 00` |
| 0xF0 | Glare | 77C4DB | `43 88 84 3E F0 00 00 00` |
| 0x14C | Depth of Field | 77BB58 | `43 88 84 3E 4C 01 00 00` |
| 0x184 | **Chromatic Aberration** | 77CCFD | `43 88 84 3E 84 01 00 00` |
| 0x198 | **Lens Distortion** | 77CABD | `43 88 84 3E 98 01 00 00` |
| 0x1A0 | SSAO | 77D47D | `43 88 84 3E A0 01 00 00` |
| 0x1FC | RLR | 77D992 | `43 88 84 3E FC 01 00 00` |
| 0x981 | unknown | 77B82C | `43 88 84 3E 81 09 00 00` |
| 0x988 | Fog | 77DA2F | `43 88 84 3E 88 09 00 00` |

### What DERQ2 has that DERQ1 does not

- **SMAA.** Offsets 0x78 and 0x7C. The table's "SMAA?" script sets `0x70 = 1`, `0x71 = 0`, `0x78 = 1` and
  overrides the threshold — i.e. it switches the game from temporal AA to SMAA. There is no DERQ1 equivalent.
- **Chromatic Aberration** and **Lens Distortion** as separate toggles, where DERQ1 has only Camera Distortion.
- A volumetric fog chain in the technique list (see §7).

`Plugin_DERQ2::GraphicsSettingsPatches` currently has no signatures at all, so all of the above is unimplemented.
Given the signatures are single anchors and the mid-hook helper already exists in `Plugin_DERQ`, filling it in is
mechanical.
