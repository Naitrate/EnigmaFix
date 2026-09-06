# Ultrawide / high-resolution crop — investigation handoff

Death end re;Quest (DERQ 1), EnigmaFix, 3440x1440 on Linux/Proton.
Session date: 2026-09-03/04. Written for another agent picking this up.

Measurements are separated from inference. Several confident-sounding conclusions
in this session turned out wrong; those are listed explicitly in §5 so they are
not repeated.

---

## 1. The problem

At any resolution above 1920x1080, the 3D scene is rendered **cropped and
magnified toward the top-left**. Roughly the top-left 56% x 75% of the image is
blown up to fill the screen.

Key properties, confirmed by the user early and vindicated by measurement:

- **Resolution dependent, aspect independent.** It happens at 2560x1440 (16:9)
  as well as 3440x1440 (21:9). It does **not** happen at 1920x1080.
- The magnitude is exactly `1920/width` by `1080/height`.

The user predicted both properties before any of it was measured. Any hypothesis
that does not reduce to a `renderSize / 1080p` ratio is almost certainly wrong.

---

## 2. Confirmed by measurement

### 2.1 Where the crop is introduced

Bisected visually through a 3440x1440 capture
(`Application_2026.09.04_01.31_frame1624.rdc`):

| Stage | Resource | Framing |
|---|---|---|
| Scene entering post-processing | `6862` (3440x1440) | **correct** |
| `yebismizuchi2` intermediate, after pass 6082 | `8682` (3440x1440) | **correct** |
| YEBIS composite output (EID ~6988 / ~7360) | `6855` (3440x1440) | **cropped** |
| Final swapchain | `6865` (3440x1440) | cropped (inherits) |

Everything before the composite is correctly framed. Everything after inherits
the crop. **The crop is created by the single YEBIS composite draw.**

The composite is a `DrawIndexed(3)` fullscreen triangle. It samples:
- slot 0: `8682` — the scene, 3440x1440
- slot 1: `8699` — bloom, 688x288
- slot 2: `7125` — 1x1 exposure

and writes `6855` (3440x1440). All source/destination sizes are correct.

### 2.2 The crop is in the texture coordinates

`get_post_vs_data` at the composite. Stride 56 = `SV_POSITION(4)` + 5x`TEXCOORD(2)`.
Note `SV_POSITION` comes **first** in the returned array despite being listed last
in the attribute list.

```
positions : (-1,-1), (-1,3), (3,-1)          <- correct fullscreen triangle
TEXCOORD0 : u 0 -> 1.116279,  v 0.75 -> -0.75
```

The visible screen is half the triangle, so across the visible region:

```
u span = 1.116279 / 2 = 0.5581 = 1920 / 3440
v span = 0.75                  = 1080 / 1440
```

**The vertex shader receives UVs that sample only 55.81% x 75.00% of the source.**
Geometry is correct; only the texcoords are wrong.

### 2.3 The UVs come from a CPU-written vertex buffer

`get_vertex_inputs` at the composite:

- Vertex buffer: `ResourceId::7830`, stride 48
- Layout: `POSITION(R32G32)` @0, `TEXCOORD0..4(R32G32)` @8,16,24,32,40
- Index buffer: `ResourceId::7903`, stride 2

The UVs are **not** computed in the shader from a constant. They are written into
the vertex buffer by the CPU, apparently as `cachedRenderSize / textureSize`.

### 2.4 The stale render size

`Application.exe+0x1339380` holds a **pointer**. At `[ptr+0x40]` / `[ptr+0x44]`
are two ints holding the engine's cached render size.

Read live via Cheat Engine at 3440x1440:

```
qword at [ptr+0x40] = 4638564681600 = 0x00000438_00000780
                                       ^^^ 1080      ^^^ 1920
```

**They stay at 1920x1080 regardless of the actual resolution.** This matches the
crop ratio exactly and is almost certainly the value feeding §2.3.

Ghidra (image base `0x7ff6cc4c0000`, so RVA + that = Ghidra address):

- `FUN_7ff6ccba26a0` (RVA `0x6E26A0`) — camera initialiser. Computes
  `aspect = [ptr+0x40] / [ptr+0x44]` into camera `+0x50`. Also writes the default
  FOV `0x42340000` (45.0f) to camera `+0x44`.
- `FUN_7ff6ccba1f40` (RVA `0x6E1F40`) — FOV setter:
  `FOV = 2*atan(K/x) * (180/pi)` into camera `+0x4C4`. `K` at RVA `0xE32910`,
  `180/pi` at RVA `0xE32C68` (the CT mislabels this "Overworld FOV 57.29577637").

### 2.5 Writing the stale size does NOT fix it

Forcing `[ptr+0x40]/[ptr+0x44]` to 3440/1440 (live, via CE):

- Scene geometry becomes correct
- **But the output letterboxes** — black bars, content in a sub-rect
- The crop is **not** resolved

`DAT_7ff6cd7f9380` is a large **engine-context object** with many subsystems at
different offsets (e.g. `+0x1d0` is the shader manager). `get_xrefs_to` on the
pointer lumps all of them together and is misleading. The render size at
`+0x40/+0x44` clearly has consumers beyond the composite's UVs.

Also observed by the user: cycling the in-game resolution (3440x1440 -> 3840x2160
-> back) clears the blackness but **not** the crop. So resource recreation fixes
one half and not the other — the UV generation is cached somewhere a resize does
not touch.

---

## 3. Ruled out (do not re-investigate)

All measured directly, at both 1920x1080 and 3440x1440 where relevant:

| Suspect | Verdict | Evidence |
|---|---|---|
| Scene projection matrix | **Correct** | `u_ProjectionMatrix m[1][1] = 2.5` at *both* resolutions; `m[0][0]` scales with aspect (1.40625 @16:9, 1.04651 @21:9). Textbook Hor+, fovY 43.6° constant, fovX widens 70.8° -> 87.4°. |
| Camera aspect field (`+0x50`) | Not the cause | Derived from §2.4; patching it changed nothing visible. |
| Render target sizes | **Correct** | Full pyramid: 3440x1440 -> 1720x720 -> 860x360 -> 430x180 -> 344x144 -> 172x72 -> 86x36 -> 43x18 -> 21x9 -> 10x4. |
| Viewports / scissors | **Correct** | ~2819 viewport + ~340 scissor corrections applied by EnigmaFix, all valid mappings. |
| Final swapchain composite | **Correct** | `gProjectionMatrix2D` offsets are `1/3440` and `1/1440` half-texel corrections. |
| Mizuchi copyback `gWorld` | **Correct** | Engine already writes 3440x1440; logged in-game, independent of the MCP cbuffer bug. |
| `ResourceId::1897` (1920x1080 blit) | **Real but separate bug** | Write-only in-frame, never sampled. Fixed anyway (see §4). Fixing it did not affect the crop. |
| ~~Progressive "zoom" when stepping the chain in RenderDoc~~ | **THIS WAS THE BUG** | Retracted 2026-09-06. It is not viewer scale-to-fit. Every pass from the second onward re-crops what the previous one already cropped, so the zoom compounds down the chain. Proved by pixel correspondence, not by eye: `43523`@(2800,700) matches its source `43520`@(1563,525) = (2800x0.5581, 700x0.75) and does **not** match (2800,700). See §8. |

---

## 4. Code changes made this session

All in `Source/`. Build: `NIXPKGS_ALLOW_UNSUPPORTED_SYSTEM=1 nix-build --arg buildType '"Debug"'`
Output lands in the **nix store** via `result/Windows/EnigmaFix.asi` — it does
**not** update `Binaries/Windows/`, which still holds a stale Feb-2025 artifact.
Deploy with an explicit `cp` from `result/Windows/`.

### 4.1 Fixes unrelated to the crop — keep these

| # | File | Change |
|---|---|---|
| 1 | `Managers/PatchManager.cpp` | `RunPatches()` switch had **no `break` statements**. Harmless only while non-DERQ cases were empty; would have run every game's patches on top of DERQ's. |
| 2 | `Plugins/Plugin_DERQ.cpp` | `resolutionList` resolved `BaseModule` in a namespace-scope constructor, i.e. during static init, before `InitPatch()` assigned it. Every pointer was `nullptr + 0xF587xx`, reading 0, and `ResCheckFunctionHook` wrote **0x0** into the live internal resolution -> crash. Now stores offsets and resolves lazily, and refuses to write values <= 0. |
| 3 | `Managers/RenderManager.cpp` | `hkResizeBuffers` called through `pDevice`/`pContext`/`oResizeBuffers` with **no null checks**. It runs on a different vtable slot than `hkPresent`, so the startup resize hit uninitialised globals -> call through a null vtable slot (`RIP=0`). Now guarded; all originals null-checked. |
| 4 | `Managers/RenderManager.cpp` | Removed two `throw std::runtime_error` calls from D3D hooks — unwinding across the COM boundary is UB. |
| 5 | `Managers/RenderManager.cpp` | Internal-resolution heuristic (`MipLevels == 11 \|\| 12` on `R16G16B16A16_FLOAT`) latched onto a **1024x1024** cube face and later a **1280x720** startup target. Now requires non-square, landscape, >=640x360, a genuine full mip chain, and a match against the configured resolution. |
| 6 | `Managers/RenderManager.cpp` | `BindFlags` compared with `==` instead of a mask, so any RT with an extra bind flag was skipped. This is why the `R8G8B8A8_UNORM` case was marked "not running". |
| 7 | `Managers/RenderManager.cpp` | Viewport/scissor correction: merged `vpResize`/`srResize` into one function, fixed a **render-target-view reference leak** on every non-success path, and removed the `IndexCount == 3\|4\|6` filter (the composite is a mesh draw with thousands of indices and was being skipped). |
| 8 | `Managers/RenderManager.cpp` | Added the missing **GBuffer formats** to the viewport filter: `R16G16_SNORM`, `R16G16B16A16_UNORM`, `R8G8B8A8_UNORM_SRGB`. These are the main scene targets (185/147/147 draws) and had never been corrected. Fixed a sub-rect regression. |
| 9 | `Managers/RenderManager.cpp` | `hkPresent` passed `DXGI_SWAP_EFFECT_FLIP_DISCARD \| DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` as `Present`'s **Flags**, which is `DXGI_PRESENT_RESTART` plus an undefined bit, every frame. Now forwards the game's own flags. |
| 10 | `Managers/LogManager.cpp` | `async_logger` had **no flush policy**, so `EnigmaFix.log` stayed 0 bytes. Now `flush_on(warn)` + `flush_every(1s)`. |
| 11 | `Managers/RenderManager.cpp` | Per-draw resize logging (~2819 lines/session to a file sink *and* an `AllocConsole` window) deduplicated to one line per distinct transition. Real frame-time cost. |
| 12 | `Utilities/CrashHandler.{h,cpp}` (new) | Vectored exception handler writing `Module+RVA`, registers and a stack scan to the log. Needed because Proton produces no usable crash dump. **This is what found fix #3.** |
| 13 | `Managers/RenderManager.cpp` | `cbPatchMizuchiCopyback` rewritten to derive the destination from `gProjection` and rescale `gWorld` to match, instead of forcing `gWorld` up to the internal resolution (the wrong direction). Fixes the `1897` blit. Verified: `gWorld` diagonal goes 3440x1440 -> 1919.9999x1080. |

### 4.2 Changes related to the crop

| Item | State | Notes |
|---|---|---|
| `cbPatchYebis` | **Disabled 2026-09-06** | Not a counterweight and not the wrong constant — it was the *correct* correction applied to exactly one pass out of ~34. Only one vertex shader in the chain has `am44_TransformMatrix` (`cb0[156]` = 2496 bytes, which is where the `>= 2496` guard came from): the second pass, computing `o0 = dot(float4(uv, persp, 1), am44_TransformMatrix[0])`. Scaling that diagonal un-crops that pass exactly, which is why disabling it made things worse. It cannot reach the rest, whose vertex shaders are plain `mov`s with no term to scale. Superseded by the Map/Unmap fix in §8. **If ever revived, check the offset first:** reflection reports 8 matrices, and 8x64 = 512 puts the array at 2496-512 = **1984**, not the 2048 hardcoded in the source — in which case it was writing matrices [1]..[7] and skipping [0], the only one feeding TEXCOORD0. |
| `RefreshCachedRenderSize()` | **Present but not called** | Writes the real resolution into `[ptr+0x40]/[ptr+0x44]` every frame. Caused letterboxing without fixing the crop (§2.5). Left in the file because the value it corrects is still the best lead. |
| `Plugin_DERQ::AspectRatioPatches` aspect mid-hook | **Enabled, unverified** | Mid-hook forcing `RES.InternalAspectRatio` at the camera aspect write. Added on a hypothesis that §3 later disproved. Harmless but unjustified — **recommend reverting**. |

### 4.3 Dead code left in place

`cbPatchYebis`'s sibling `cbResize` and the old `cbPatchYebis` scaling are still
present. `cbResize` also leaks an `ID3D11Device` in its creation path. Left alone
to keep the diagnosis legible; safe to delete once the crop is resolved.

---

## 5. Wrong turns — do not repeat

Recorded because each one looked convincing and cost time.

1. **"It's the aspect ratio."** Disproved by measuring `u_ProjectionMatrix` at
   both resolutions: `m[1][1]` is identical, `m[0][0]` scales correctly. The user
   said from the outset it would happen at 16:9 too; that was right.
2. **"It's the FOV."** Same measurement. fovY is constant at 43.6° across
   resolutions. Correct Hor+.
3. **"`ResourceId::1897` is the on-screen image."** It is write-only in-frame;
   the swapchain is `6865`. Fixing its blit was correct but changed nothing visible.
4. **"`cbPatchYebis` causes the crop."** Its scale factors match the crop ratio
   exactly, which looked damning. Disabling it made things **worse** — it is a
   counterweight. Matching ratios prove a relationship, not a direction.
5. **"Force the cached render size."** Causes letterboxing (§2.5).
6. **`get_xrefs_to` on `DAT_7ff6cd7f9380`** returns ~40 refs, but most are
   unrelated subsystems at other offsets (`+0x1d0` = shader manager). Filter by
   the actual offset before drawing conclusions.

The pattern in 1, 2, 4 and 5: reasoning forward from a number that matched,
instead of observing the mechanism. The two things that *did* work were direct
observation — `addr2line` on a crash return address, and logging a guard's actual
value rather than assuming it.

---

## 6. Recommended next step — DONE, see §8

This was the right call. Kept for the reasoning; the outcome is in §8.

**Hook `ID3D11DeviceContext::Map`/`Unmap` for vertex buffer `ResourceId::7830`**
and dump what the CPU writes, together with a return address.

That yields, directly:
- the UV values being generated, confirming §2.2 at the point of creation
- via `addr2line`/Ghidra on the return address, **the function that computes them**

From there the fix is either a targeted mid-hook on that computation, or a
`Map`/`Unmap` interception that rewrites the texcoords. Either is far safer than
overwriting the shared engine-context struct.

Once the texcoords are correct at source, **remove `cbPatchYebis`** — it will
then be over-correcting.

---

## 7. Tooling notes

### RenderDoc MCP (Wine remote replay)
- Remote server: `renderdoccmd.exe remoteserver -d -p 734037`. Ports are 16-bit
  and truncate silently: `734037 & 0xFFFF` = **13141**.
- **One client at a time.** Close the capture before another agent connects, or
  you get `Remote side of network connection is busy`.
- The server leaks and wedges after repeated open/close. Restart it periodically.
- `get_cbuffer_contents` returned all zeros until a patch supplying
  `byteOffset`/`byteSize` to `GetCBufferVariableContents`. **Any constant read
  before that patch is invalid.** Verify with `fParam_ScreenSpaceScale` at the
  composite — it should read `[1.0, -1.0]`, not `[0,0]`.
- `PipeState` viewport/scissor/blend/depth/raster accessors are **still broken**
  in 1.46 (`GetViewports` etc. renamed). Viewport state cannot be read through
  the MCP; in-game logging was used instead.

### Viewing HDR / TYPELESS render targets
They export correctly but need exposure and an alpha fix. The alpha channel
contains garbage (values around -63000) which poisons `-auto-level`:

```bash
magick LEVEL.exr -alpha off -evaluate max 0 -auto-level -resize 700x view.png
```

`typeCast` is **not** the problem — Typeless and Float exports are byte-identical.

### Symbolicating EnigmaFix crash addresses
The ASI is built with `-ggdb3`, so `Module+RVA` from the crash handler resolves
to an exact source line:

```bash
x86_64-w64-mingw32-addr2line -f -C -e result/Windows/EnigmaFix.asi <ImageBase+RVA>
```

`objdump -p` gives the image base. Addresses inside `Application.exe` need Ghidra
(image base `0x7ff6cc4c0000`).

### Ghidra
Image base is `0x7ff6cc4c0000`, **not** `0x140000000`. Runtime RVAs from the
EnigmaFix log must be rebased: `Ghidra address = 0x7ff6cc4c0000 + RVA`.

---

## 8. Resolved — 2026-09-06

Fixed in `Managers/RenderManager.cpp` by rewriting the texture coordinates at `Unmap`.

### 8.1 What it actually was

Every pass in the chain draws the same fullscreen triangle out of a pool of **13 vertex buffers of exactly
192 bytes**, CPU-written through `Map`/`Unmap` immediately before the draw that consumes it. Every vertex set in
them carried:

```
POSITION  (-1, 1) (-1,-3) (3, 1)            <- a correct fullscreen triangle
TEXCOORD  ( 0,0.75) (0,-0.75) (1.116279, 0.75)
```

`1.116279` is `2 * 1920/3440`; `0.75` is `1080/1440`. The engine builds the texcoords as `renderSize/textureSize`
with a renderSize stuck at 1920x1080, so each pass samples the top-left 55.81% x 75% of its source and stretches it
over the whole target. Because every pass does that to a source the previous pass already cropped, it **compounds**.

This is not fixable in a shader. The composite's vertex shader is five `mov`s and a position multiply:

```
0: mov o0.xy, v1.xyxx   ...   5: mul o5.xy, v0.xyxx, fParam_ScreenSpaceScale.xyxx
```

There is no term to scale. The vertex buffer is the only place that reaches the whole chain.

### 8.2 The fix

`hkMap` records the pointer for any 192-byte vertex buffer; `hkUnmap` rewrites it before forwarding. No GPU readback
— the data is still in CPU-visible memory the game just wrote.

Identification is by fingerprint, not by resource ID: the POSITION triple `(-1,1) (-1,-3) (3,1)`, then per texcoord
channel the shape `(0,V) (0,-V) (U,V)` where `U ≈ 2*1920/W` and `|V| ≈ 1080/H`. Matching the *exact stale ratio*
rather than "anything below full coverage" is what keeps it off the composite's TEXCOORD2/3, which are not screen
UVs at all. It is idempotent — corrected values no longer match — which matters because these are pooled pages and
`MAP_WRITE_NO_OVERWRITE` leaves already-corrected neighbours visible.

### 8.3 The bug in the first version of the fix

Sub-allocations are packed sequentially at **float2 granularity**, not at stride-aligned offsets. The first version
stepped `base` by the stride and silently missed sets: a cutscene frame laid out stride 16 at 0, stride 24 at 48,
then stride 16 at **120**, and 120 is not a multiple of 16. Gameplay frames happened to land everything on 0/48/96,
each a multiple of its own stride, so it looked complete. Stepping by 8 is the only assumption that holds.

The reason this took a second round: the summary log line fired once per session, and the passes that *were* being
corrected printed it, so a layout only reached in cutscenes never announced itself. It now logs once per distinct
`(offset, stride)` layout. **A log line that only proves "something worked" is worse than none.**

### 8.4 Method note

Three of the wrong turns in §5 and the retraction in §3 share one cause: reasoning forward from a number that
matched instead of measuring the mechanism. The two things that settled this were both direct measurement —
pixel correspondence between a pass and its source, and dumping the vertex buffer bytes. When a hypothesis and an
observation disagree, dump the buffer.
