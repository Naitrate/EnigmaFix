# Pillarboxing the UI into a centred 16:9 box — DERQ 1

EnigmaFix, 3440x1440. Session 2026-09-06. **Not working yet.** The graphics-API approach was built, tested and
rejected; the memory-patch approach is scoped but not written.

---

## 1. The engine

Strings identify this as `orochi_mizuchi sdk2 2.0 sisdk`. Input goes through **DirectInput 8**
(`siutinput_di_device_mouse_impl.cpp`, `..._keyboard_impl.cpp`, `..._gamepad_impl.cpp`), not window messages,
though `GetCursorPos`/`ClipCursor`/`ShowCursor` are all imported as well.

Ghidra image base for `Application.exe` is **`7ff6cc4c0000`**, so a Cheat Engine `Application.exe+X` is
`7ff6cc4c0000 + X`.

## 2. What the UI actually does

The 2D UI is authored in a fixed **1920x1080 virtual canvas** and stretched across whatever the target is. The two
projections that do it are `gProjectionMatrix2D` (HUD, bustups, battle UI, minimap) and `Mk_ViewProjection` (title
screen, pause menu, VN box), both row-major:

```
m00 =  2/1920   m03 = -1 - 1/1920      (the half-pixel offset, so -1.00052)
m11 = -2/1080   m13 =  1 + 1/1080
```

Verified identical at every resolution, in mod-on and mod-off captures. It is **not** derived from the display
size, and it is **not** static data — searching the image for the byte pattern of `m03` finds nothing, so it is
built at runtime from 1920/1080 literals.

Confining the UI to a centred 16:9 box is therefore just `m00 *= a; m03 *= a` with `a = (H * 16/9) / W`
(`0.744186` at 3440x1440). This matches the `0.75` in `UI Scaling (Via Shader Patches...)`, which is `1920/2560`
for 2560x1080 — the same formula, written for one case.

## 3. Why the constant-buffer hook was rejected

Implemented in `RenderManager.cpp` as `PillarboxUIProjection`, matching buffers on matrix *contents* rather than
size. It works — the title screen and menus pillarbox correctly — but it identifies **the 1920x1080 ortho
projection, not "UI that should be boxed"**, and two things broke:

| symptom | cause |
|---|---|
| area-transition and cutscene blur breaks | the post-process composite draws its fullscreen quad through the same projection, so it got shrunk too |
| minimap breaks completely | its geometry moves inward while its scissor rect, which `CorrectMinimapScissor` maps into real pixel space, does not move with it |

The log narrows this precisely — exactly two buffer layouts are patched:

| buffer | matrix at | what it is |
|---|---|---|
| **64 bytes** | float offset 0 | `gProjectionMatrix2D` — the UI |
| **160 bytes** | float offset 16 | the post-process composite, the same `$Globals` `cbPatchMizuchiCopyback` already targets — **this is the blur** |

So the hook *is* narrowable to the 64-byte buffer. It was still rejected, because it cannot fix the scissor or the
mouse without further hooks, and those are the parts that matter. Decision: **UI goes to memory patches; the
minimap stays a graphics-API patch.** The code is left in place behind `PillarboxUI`, defaulting off.

## 4. Leads for the memory-patch approach

**A 2D canvas descriptor written with literal constants.** `FUN_7ff6cc958fd0` initialises a struct with
`+0x150 = 960.0`, `+0x154 = 540.0`, `+0x15c = 1920.0`, `+0x160 = 1080.0`, `+0x168 = 0.5`, `+0x16c..+0x178 = 1.0`.
That is a virtual canvas with its centre and extent spelled out. There are 20 sites carrying `1920.0f`
(`00 00 F0 44`); several repeat this pattern. This is the most promising lever — it is per-object, so it can be
changed without touching the post-process path.

**A title-safe rect, already aspect-aware.** `FUN_7ff6cc5ece80` computes, from the display object's width
(`+0x40`) and height (`+0x44`):

```
halfX = halfY = 0.870                     // default
if (width/height < 1.5) { halfX = 0.844; halfY = 0.808; }   // 4:3 and similar
obj+0x48 = (1 - halfX) * 0.5 * width      // left      (0.065 * width by default)
obj+0x4c = (1 - halfY) * 0.5 * height     // top
obj+0x50 = (1 + halfX) * 0.5 * width      // right     (0.935 * width)
obj+0x54 = (1 + halfY) * 0.5 * height     // bottom
```

So the engine already builds a centred UI rect and already switches on aspect ratio. Forcing
`halfX = 0.870 * (H * 16/9) / W` would pull edge-anchored HUD into the 16:9 region using the engine's own
machinery. It is a *safe area* though, not the master transform, so it moves edge-anchored elements only.

**The display object.** `+0x40`/`+0x44` are its pixel width/height; `FUN_7ff6cc58c3a0` returns them as a pair, and
`FUN_7ff6cc560090` returns the object. The CT's "UI Adjustment 1/2" scripts (`Application.exe+CC3D0` and
`+236BA5`) redirect reads of these to a custom global at `+28B524`, which is how that author changed UI scale. The
accessor has 50+ callers, so it is far too broad to repoint wholesale.

## 5. The mouse question is still open, and is cheap to settle

Because input is DirectInput, the likely design is that the game accumulates its own cursor in the 1920x1080 UI
space. If so the cursor sprite draws through the same projection as the UI, hit-testing happens in the same space,
and **the cursor stays aligned for free** — the only effect is that it can no longer reach the bars. The failure
case is the cursor being derived from absolute OS coordinates instead.

One test run answers it: pillarbox the UI by any means and look at whether the cursor still lands on what it points
at. That is much cheaper than reversing the DI stack, and it decides how much input work exists.

## 6. Method note

The constant-buffer matcher was content-based and self-limiting, which was right, and it still broke two unrelated
things — because a correct *identification of a matrix* is not an identification of *intent*. Same shape of error
as the GTAO matcher that ate `DownSampleGBuffer0`: the discriminator described the data, not the pass. The
difference is that this time the logging was built in from the start, so one run named the offending buffer
exactly instead of costing another eight diagnoses.
