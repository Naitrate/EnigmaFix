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

// Internal Functionality
#include "RenderManager.h"
#include "../Settings/PlayerSettings.h"
#include "../Localization/Localization.h"
#include "../Utilities/DXHelper.h"
#include "../Utilities/DXEnums.h"
#include "UIManager.h"
#include "../Shaders/TemporalAAResolve.h"
// System Libraries
//#include <comip.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <set>
// Third Party Libraries
#include <map>
#include <spdlog/spdlog.h>
#include "kiero.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

// Variables
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
//// D3D11 related variables
HWND window = nullptr;
WNDPROC oWndProc;
ID3D11Device* pDevice = nullptr;
ID3D11DeviceContext* pContext = nullptr;
ID3D11RenderTargetView* mainRenderTargetView;
//// Various PlayerSettings and Localization String Accessors
auto& PlayerSettingsRm      = EnigmaFix::PlayerSettings::Get();
auto& LocalizationRm        = EnigmaFix::Localization::Get();
// These two stay pointers so they track PlayerSettings live. Anything else read out of PlayerSettings here would be a
// copy taken at DLL load, before the config is read, so the rest of the settings are read at their point of use.
int* InternalHorizontalRes  = &PlayerSettingsRm.INS.InternalResolution.x;
int* InternalVerticalRes    = &PlayerSettingsRm.INS.InternalResolution.y;
//// Hook Init Check
bool InitHook               = false;
// Namespaces
using namespace ImGui;
using namespace kiero;
using namespace std; // This is for unique_ptrs
// Singleton References
auto& UIManagerRenMan = EnigmaFix::UIManager::Get();
// Singleton Instance
EnigmaFix::RenderManager EnigmaFix::RenderManager::rm_Instance; // Seemingly need this declared in RenderManager.cpp so a bunch of linker errors don't happen.



void InitImGui() // Initializes ImGui, alongside the needed fonts.
{
    CreateContext();
    ImGuiIO& io = GetIO();
    // TODO: Figure out how to disable gamepad input to the game when the interface is open, and also make sure gamepad input is consistent.
    // TODO: Find a way to add a gamepad hotkey to open the menu.
    io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
    LocalizationRm.InitFont(PlayerSettingsRm.INS.dpiScale / 100.0f * PlayerSettingsRm.INS.dpiScaleMultiplier);

    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX11_Init(pDevice, pContext);
}

// TODO: Figure out why this isn't disabling input from gameplay.
LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (InitHook) {
        // Toggle UI visibility with key presses
        if (uMsg == WM_KEYUP) {
            if (wParam == VK_DELETE)
                PlayerSettingsRm.ShowEFUI = !PlayerSettingsRm.ShowEFUI;
            else if (wParam == VK_OEM_3)
                PlayerSettingsRm.ShowDevConsole = !PlayerSettingsRm.ShowDevConsole;
        }
        // If UI is active, block input from the game, but allow ImGui to handle it
        if (PlayerSettingsRm.ShowEFUI) {
            // Pass input to ImGui first
            if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) {
                return true; // ImGui handled the input, return early
            }
            // If ImGui didn't handle the input, block it from reaching the game
            switch (uMsg)
            {
                case WM_MOUSEMOVE:
                case WM_LBUTTONDOWN: case WM_LBUTTONUP:
                case WM_RBUTTONDOWN: case WM_RBUTTONUP:
                case WM_MBUTTONDOWN: case WM_MBUTTONUP:
                case WM_MOUSEWHEEL:
                case WM_KEYDOWN: case WM_KEYUP:
                case WM_CHAR:
                case WM_INPUT:  // Block Raw Input if game uses DirectInput
                    return 0;  // Absorb the event (block the game)
                default: break;
            }
        }
    }
    // If UI is not active, pass input to the original window procedure (game)
    if (oWndProc)
        return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
    else
        return DefWindowProc(hWnd, uMsg, wParam, lParam);  // Fallback if oWndProc is invalid
}

// Identifies the CopyDeferredColor_Hist target, whose size is the game's actual internal rendering resolution.
//
// A 16 bit float target carrying a full mip chain used to be enough, but plenty of unrelated resources match that: a
// 1024x1024 cube face has exactly 11 mips, the same as a 1080p target, and latching onto one sets the internal
// resolution to 1024x1024 and sends every downstream resize to the wrong size. Requiring a non-square, landscape,
// plausibly screen sized surface whose mip count really is the full chain for its dimensions rejects those.
bool LooksLikeInternalResolutionTarget(const D3D11_TEXTURE2D_DESC* pDesc)
{
    if (pDesc->Format != DXGI_FORMAT_R16G16B16A16_FLOAT) { return false; }
    if (pDesc->Width == pDesc->Height) { return false; }  // No display is square.
    if (pDesc->Width < pDesc->Height)  { return false; }  // Nor is one taller than it is wide.
    if (pDesc->Width < 640 || pDesc->Height < 360) { return false; }

    UINT fullMipChain = 1;
    for (UINT dimension = pDesc->Width; dimension > 1; dimension >>= 1) { ++fullMipChain; }
    if (pDesc->MipLevels != fullMipChain) { return false; }  // 11 at 1920x1080, 12 at 3440x1440.

    // The shape tests above are not enough on their own. During startup the engine creates a 1280x720 target that is
    // landscape, large enough, and carries a genuine 11 entry mip chain, so it satisfies every one of them -- and
    // latching onto it sets the internal resolution to 1280x720 until the real target shows up a second later. Every
    // render target created in that window then gets sized against the wrong resolution. Requiring the dimensions to
    // match the resolution actually being asked for removes the ambiguity entirely.
    if (PlayerSettingsRm.RES.UseCustomRes) {
        return pDesc->Width  == static_cast<UINT>(PlayerSettingsRm.RES.Resolution.x)
            && pDesc->Height == static_cast<UINT>(PlayerSettingsRm.RES.Resolution.y);
    }
    return true;
}

// An easily callable function to make the CreateTexture2D hook a little cleaner.
// Returns whether it actually resized anything, so callers can log the match rather than logging on every candidate
// texture. The previous logging sat outside this check and reported things like "changing resolution from 1x1 to
// 1920x1080" for targets it never touched.
bool resizeRt(D3D11_TEXTURE2D_DESC *pDesc, int Width, int Height, int DesiredWidth, int DesiredHeight)
{
    if (pDesc->Width == Width && pDesc->Height == Height) {
        if (DesiredWidth <= 0 || DesiredHeight <= 0) {
            spdlog::error("Refusing to resize a {}x{} target to {}x{}.", Width, Height, DesiredWidth, DesiredHeight);
            return false;
        }
        pDesc->Width  = static_cast<UINT>(DesiredWidth);
        pDesc->Height = static_cast<UINT>(DesiredHeight);
        return true;
    }
    return false;
}

// Resizes constant buffers
bool cbResize(ID3D11DeviceContext* pContext, D3D11_RENDER_TARGET_VIEW_DESC pDesc, D3D11_TEXTURE2D_DESC texdesc, D3D11_VIEWPORT vp)
{
    // TODO: Investigate the pixel shader constant buffer responsible for the YebisMizuchi draw calls. There's a few interesting parameters that might need to be investigated.
    // 1. fParam_ScreenSpaceScale - This is located at offset 80 with a value of "1.00, -1.00" This might be able to adjust the scaling of the post process effects.
    // 2. afUVWQ_TexCoordScaleOffset
    // 3. afParam_TexCoordScaler8
    // 4. am44_TransformMatrix

    // Define the structure for pixel shader constants.
    struct CustomPixelShaderConstants {
        DirectX::XMFLOAT2 fParam_ScreenSpaceScale;  // Screen space scale
    };

    static std::map<UINT, ID3D11Buffer*> buffers;

    // If we are not rendering to a mip map for hierarchical Z, the format is
    // [ 0.5f / W, 0.5f / H, W, H ] (half-pixel size and total dimensions)
    if(pDesc.Texture2D.MipSlice == 0) {
        spdlog::info("Found Texture2D with a MipSlice of 0.");
        auto iter = buffers.find(texdesc.Width);
        ID3D11Buffer* replacementBuffer = nullptr;
        ID3D11Device* dev = nullptr;

        if(iter == buffers.cend()) {
            // Create a new constant buffer if it doesn't exist
            CustomPixelShaderConstants constants;
            constants.fParam_ScreenSpaceScale = DirectX::XMFLOAT2(1.0f / vp.Width, -1.0f / vp.Height);

            D3D11_SUBRESOURCE_DATA initialData;
            initialData.pSysMem = &constants;

            pContext->GetDevice(&dev);

            // Buffer descriptor for the constant buffer
            D3D11_BUFFER_DESC bufferDesc;
            bufferDesc.ByteWidth = sizeof(CustomPixelShaderConstants);  // Size of your constant buffer struct
            bufferDesc.Usage = D3D11_USAGE_DYNAMIC;  // DYNAMIC to allow mapping
            bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // Allow modification
            bufferDesc.MiscFlags = 0;
            bufferDesc.StructureByteStride = 0;

            // Create the constant buffer
            dev->CreateBuffer(&bufferDesc, &initialData, &replacementBuffer);
            buffers[texdesc.Width] = replacementBuffer;
        } else {
            replacementBuffer = iter->second;
        }

        // Now modify only the fParam_ScreenSpaceScale if necessary
        if (replacementBuffer) {
            CustomPixelShaderConstants* mappedData = nullptr;
            D3D11_MAPPED_SUBRESOURCE mappedResource;

            // Map the buffer to modify it
            pContext->Map(replacementBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
            mappedData = static_cast<CustomPixelShaderConstants*>(mappedResource.pData);

            // Update only the fParam_ScreenSpaceScale parameter
            mappedData->fParam_ScreenSpaceScale = DirectX::XMFLOAT2(1.0f / vp.Width, -1.0f / vp.Height);

            // Unmap the buffer after modification
            pContext->Unmap(replacementBuffer, 0);

            // Set the modified buffer for the pixel shader
            pContext->PSSetConstantBuffers(5, 1, &replacementBuffer);
            spdlog::info("Set Pixel Shader's Constant Buffer");
            return true;
        }
    }
    return false;
}

void cbPatchYebis(ID3D11DeviceContext* pContext)
{
    if (!PlayerSettingsRm.RES.UseCustomRes) return;
    if (*InternalHorizontalRes == 1920 && *InternalVerticalRes == 1080) return; // Only needed when the resolution isn't 1920x1080

    // --- 1. Get the VS constant buffer at slot 0 ---
    ID3D11Buffer* vsBuffer = nullptr;
    pContext->VSGetConstantBuffers(0, 1, &vsBuffer);
    if (!vsBuffer) return;

    D3D11_BUFFER_DESC bufDesc = {};
    vsBuffer->GetDesc(&bufDesc);

    // Verify this is the YEBIS $Globals buffer by size.
    // The analysis confirmed it needs to be at least 2496 bytes to contain am44_TransformMatrix.
    if (bufDesc.ByteWidth < 2496) {
        vsBuffer->Release();
        return;
    }

    // --- 2. Build a staging replacement buffer (once, or lazily) ---
    // We create a DYNAMIC mirror if one doesn't exist for this byte width.
    static std::map<UINT, ID3D11Buffer*> yebisBuffers;
    ID3D11Buffer* patchBuffer = nullptr;
    ID3D11Device* dev = nullptr;

    auto iter = yebisBuffers.find(bufDesc.ByteWidth);
    if (iter == yebisBuffers.cend()) {
        pContext->GetDevice(&dev);

        D3D11_BUFFER_DESC patchDesc = {};
        patchDesc.ByteWidth      = bufDesc.ByteWidth;
        patchDesc.Usage          = D3D11_USAGE_DYNAMIC;
        patchDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        patchDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        dev->CreateBuffer(&patchDesc, nullptr, &patchBuffer);
        yebisBuffers[bufDesc.ByteWidth] = patchBuffer;
        dev->Release();
    } else {
        patchBuffer = iter->second;
    }

    if (!patchBuffer) { vsBuffer->Release(); return; }

    // --- 3. Copy the original buffer contents via a staging buffer ---
    // We need to read the GPU buffer to avoid clobbering unrelated constants.
    // This requires a STAGING buffer for the readback.
    static std::map<UINT, ID3D11Buffer*> stagingBuffers;
    ID3D11Buffer* stagingBuffer = nullptr;

    auto stageIter = stagingBuffers.find(bufDesc.ByteWidth);
    if (stageIter == stagingBuffers.cend()) {
        pContext->GetDevice(&dev);

        D3D11_BUFFER_DESC stageDesc = {};
        stageDesc.ByteWidth      = bufDesc.ByteWidth;
        stageDesc.Usage          = D3D11_USAGE_STAGING;
        stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        dev->CreateBuffer(&stageDesc, nullptr, &stagingBuffer);
        stagingBuffers[bufDesc.ByteWidth] = stagingBuffer;
        dev->Release();
    } else {
        stagingBuffer = stageIter->second;
    }

    if (!stagingBuffer) { vsBuffer->Release(); return; }

    // Copy GPU -> staging so we can read it
    pContext->CopyResource(stagingBuffer, vsBuffer);

    D3D11_MAPPED_SUBRESOURCE stageMap = {};
    HRESULT hr = pContext->Map(stagingBuffer, 0, D3D11_MAP_READ, 0, &stageMap);
    if (FAILED(hr)) { vsBuffer->Release(); return; }

    // Copy the raw data into a local heap buffer we can modify
    std::vector<uint8_t> bufData(bufDesc.ByteWidth);
    memcpy(bufData.data(), stageMap.pData, bufDesc.ByteWidth);
    pContext->Unmap(stagingBuffer, 0);

    // --- 4. Patch am44_TransformMatrix at offset 2048 ---
    // Each matrix is 64 bytes (4x float4). The analysis found up to 8 matrices.
    // We scale X and Y diagonal elements (offsets 0 and 20 bytes into each matrix).
    const float scaleX = static_cast<float>(*InternalHorizontalRes) / 1920.0f;
    const float scaleY = static_cast<float>(*InternalVerticalRes)   / 1080.0f;

    constexpr size_t matrixBaseOffset = 2048;
    constexpr size_t matrixSize       = 64;   // sizeof(float4x4)
    constexpr int    numMatrices      = 7;    // Analysis confirmed first 7 are UV transform matrices

    for (int i = 0; i < numMatrices; ++i) {
        size_t base = matrixBaseOffset + (i * matrixSize);

        // Sanity check — don't write out of bounds
        if (base + matrixSize > bufData.size()) break;

        float* mat = reinterpret_cast<float*>(bufData.data() + base);

        // Row-major float4x4 layout:
        // [0]  = M[0][0] = X scale  <-- patch this
        // [5]  = M[1][1] = Y scale  <-- patch this
        // [10] = M[2][2] = Z scale  (leave alone)
        // [15] = M[3][3] = W/homogeneous (leave alone)

        // Only scale if the matrix looks like it has a non-zero diagonal
        // (avoids stomping on matrices used for other purposes)
        if (mat[0] != 0.0f) mat[0] *= scaleX;
        if (mat[5] != 0.0f) mat[5] *= scaleY;
    }

    // --- 5. Write the patched data into the dynamic patch buffer and bind it ---
    D3D11_MAPPED_SUBRESOURCE patchMap = {};
    hr = pContext->Map(patchBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &patchMap);
    if (SUCCEEDED(hr)) {
        memcpy(patchMap.pData, bufData.data(), bufDesc.ByteWidth);
        pContext->Unmap(patchBuffer, 0);
        pContext->VSSetConstantBuffers(0, 1, &patchBuffer);
        spdlog::info("Patched YEBIS VS constant buffer ({}x{} -> scaled {}x{}).",
                     1920, 1080, static_cast<int>(*InternalHorizontalRes), static_cast<int>(*InternalVerticalRes));
    }

    vsBuffer->Release();
}

void cbPatchMizuchiCopyback(ID3D11DeviceContext* pContext)
{
    if (!PlayerSettingsRm.RES.UseCustomRes) return;

    ID3D11Buffer* vsBuffer = nullptr;
    pContext->VSGetConstantBuffers(0, 1, &vsBuffer);
    if (!vsBuffer) return;

    D3D11_BUFFER_DESC bufDesc = {};
    vsBuffer->GetDesc(&bufDesc);

    // Identify by the exact size of this $Globals buffer (160 bytes, 4 variables)
    if (bufDesc.ByteWidth != 160) {
        vsBuffer->Release();
        return;
    }

    // Staging readback
    static ID3D11Buffer* stagingBuffer = nullptr;
    if (!stagingBuffer) {
        ID3D11Device* dev = nullptr;
        pContext->GetDevice(&dev);
        D3D11_BUFFER_DESC stageDesc = {};
        stageDesc.ByteWidth = 160;
        stageDesc.Usage = D3D11_USAGE_STAGING;
        stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        dev->CreateBuffer(&stageDesc, nullptr, &stagingBuffer);
        dev->Release();
    }
    if (!stagingBuffer) { vsBuffer->Release(); return; }

    pContext->CopyResource(stagingBuffer, vsBuffer);
    D3D11_MAPPED_SUBRESOURCE stageMap = {};
    HRESULT hr = pContext->Map(stagingBuffer, 0, D3D11_MAP_READ, 0, &stageMap);
    if (FAILED(hr)) { vsBuffer->Release(); return; }

    std::vector<uint8_t> bufData(160);
    memcpy(bufData.data(), stageMap.pData, 160);
    pContext->Unmap(stagingBuffer, 0);

    // $Globals here is gWorld(16) + gProjection(16) + gUVOffsetAndSize(4) + gColor(4) floats.
    //
    // gWorld scales the fullscreen quad in source pixels, and gProjection converts those pixels to NDC using the
    // destination size: its diagonal is 2/destWidth and -2/destHeight. When the two disagree the quad overshoots the
    // viewport and the blit shows a magnified top-left crop of the source instead of the whole thing.
    //
    // That is exactly what happens once the scene is rendered above 1080p: RenderDoc measured gWorld at 3440x1440 (the
    // source) against a gProjection built for 1920x1080 (the destination), putting the quad's far corner at NDC
    // (2.583, -1.667) instead of (1, -1), so only 2.0/3.583 = 55.8% by 2.0/2.667 = 75% of the image survives. In stock
    // 1080p both sides are 1920x1080, the scale works out to 1.0 and the bug never appears.
    //
    // The destination is whatever gProjection says it is, so gWorld is brought into line with it rather than the other
    // way round. An earlier version of this forced gWorld up to the internal resolution, which is the direction that
    // causes the crop; it only ever looked harmless because the engine had already written the source size there.
    float* gWorld      = reinterpret_cast<float*>(bufData.data());       // offset 0
    float* gProjection = reinterpret_cast<float*>(bufData.data()) + 16;  // offset 64

    if (gProjection[0] == 0.0f || gProjection[5] == 0.0f) { vsBuffer->Release(); return; }

    const float destWidth  =  2.0f / gProjection[0];
    const float destHeight = -2.0f / gProjection[5];
    if (destWidth <= 0.0f || destHeight <= 0.0f) { vsBuffer->Release(); return; }

    // Already consistent, or not the quad we are looking for.
    if (gWorld[0] == destWidth && gWorld[5] == destHeight) { vsBuffer->Release(); return; }
    if (gWorld[0] <= 0.0f || gWorld[5] <= 0.0f) { vsBuffer->Release(); return; }

    static bool loggedFixup = false;
    if (!loggedFixup) {
        spdlog::info("Fullscreen blit fixup: quad was {}x{} against a {}x{} destination ({:.1f}% x {:.1f}% of the "
                     "image was visible). Rescaling the quad to match.", gWorld[0], gWorld[5], destWidth, destHeight,
                     100.0f * destWidth / gWorld[0], 100.0f * destHeight / gWorld[5]);
        loggedFixup = true;
    }
    gWorld[0] = destWidth;
    gWorld[5] = destHeight;

    // gWorld has already been brought into line with gProjection above.

    // Write back via dynamic patch buffer
    static ID3D11Buffer* patchBuffer = nullptr;
    if (!patchBuffer) {
        ID3D11Device* dev = nullptr;
        pContext->GetDevice(&dev);
        D3D11_BUFFER_DESC patchDesc = {};
        patchDesc.ByteWidth = 160;
        patchDesc.Usage = D3D11_USAGE_DYNAMIC;
        patchDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        patchDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&patchDesc, nullptr, &patchBuffer);
        dev->Release();
    }
    if (!patchBuffer) { vsBuffer->Release(); return; }

    D3D11_MAPPED_SUBRESOURCE patchMap = {};
    hr = pContext->Map(patchBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &patchMap);
    if (SUCCEEDED(hr)) {
        memcpy(patchMap.pData, bufData.data(), 160);
        pContext->Unmap(patchBuffer, 0);
        pContext->VSSetConstantBuffers(0, 1, &patchBuffer);
        spdlog::info("Patched Mizuchi gWorld buffer ({}x{}).",
            *InternalHorizontalRes, *InternalVerticalRes);
    }

    vsBuffer->Release();
}

// ---------------------------------------------------------------------------------------------------------------
// YEBIS fullscreen-triangle texture coordinate correction.
//
// Every pass in the post processing chain draws the same fullscreen triangle out of a pool of tiny vertex buffers,
// each exactly 192 bytes, which the engine CPU-writes through Map/Unmap immediately before the draw that consumes it.
// Measured in Application_2026.09.06_16.00_frame2634.rdc at 3440x1440: 13 buffers of that size exist, and every
// vertex set in them carries
//
//     POSITION  (-1, 1) (-1,-3) (3, 1)        <- a correct fullscreen triangle
//     TEXCOORD  ( 0,0.75) (0,-0.75) (1.116279, 0.75)
//
// where 1.116279 is 2 * 1920/3440 and 0.75 is 1080/1440. The engine builds the texcoords as renderSize/textureSize
// with a renderSize that is stuck at 1920x1080, so each pass samples only the top-left 55.81% x 75% of its source and
// stretches that over the whole target. Because every pass does it to a source that the previous pass already cropped,
// the error compounds down the chain rather than appearing once.
//
// Confirmed by pixel correspondence rather than by inference: the output of the second pass at (2800,700) matches its
// source at (2800*0.5581, 700*0.75) = (1563,525) to within resampling error, and does not match (2800,700).
//
// This is not fixable in a shader. The composite's vertex shader is five movs and a position multiply:
//     mov o0.xy, v1.xyxx  ...  mul o5.xy, v0.xyxx, fParam_ScreenSpaceScale.xyxx
// There is no term to scale. Correcting the vertex buffer at Unmap is the one place that reaches the whole chain, and
// it needs no GPU readback because the data is still sitting in CPU-visible memory the game just wrote.
namespace {
    // 192 bytes holds three vertex sets at stride 16, or four at stride 48. The chain uses strides 16, 24 and 48.
    constexpr UINT  kYebisQuadBufferSize = 192;
    constexpr float kQuadEpsilon         = 1e-3f;

    // Two different buffers are intercepted through the same Map/Unmap pair, so the record says which fixup to run.
    enum class MappedBufferKind { YebisQuad, PerViewConstants, TemporalAAConstants };

    struct PendingQuadMap {
        void*            Data;
        UINT             ByteWidth;
        MappedBufferKind Kind;
    };

    // Map and Unmap are legal on deferred contexts from other threads, so the handoff between the two hooks is guarded.
    // The immediate context is the only one DERQ appears to use, but an unguarded std::map would be a latent crash.
    std::mutex quadMapMutex;
    std::map<ID3D11Resource*, PendingQuadMap> pendingQuadMaps;

    bool NearlyEqual(const float a, const float b) { return fabsf(a - b) < kQuadEpsilon; }

    // The position triple is the fingerprint. It is discriminating enough on its own that no stride is mistaken for
    // another: reading 42134 (stride 16) as stride 48 puts a second (-1,1) where (-1,-3) has to be, and fails.
    // Both y conventions are accepted because fParam_ScreenSpaceScale flips y on some passes.
    bool IsFullscreenTrianglePosition(const float* v0, const float* v1, const float* v2)
    {
        if (!NearlyEqual(v0[0], -1.0f) || !NearlyEqual(v1[0], -1.0f) || !NearlyEqual(v2[0], 3.0f)) { return false; }
        if (NearlyEqual(v0[1],  1.0f) && NearlyEqual(v1[1], -3.0f) && NearlyEqual(v2[1],  1.0f)) { return true; }
        if (NearlyEqual(v0[1], -1.0f) && NearlyEqual(v1[1],  3.0f) && NearlyEqual(v2[1], -1.0f)) { return true; }
        return false;
    }

    // Rewrites one texcoord channel across the three vertices, but only if it carries the stale-1080p signature.
    //
    // Matching against the exact expected ratio rather than "anything below full coverage" is what keeps this from
    // touching coordinates that only look similar. The composite's stride-48 vertex has five texcoords: TEXCOORD0 and
    // TEXCOORD1 are screen UVs and get corrected, while TEXCOORD2 (-0.147, 0.083) and TEXCOORD3 (-0.754, 2048.29) are
    // something else entirely and must be left alone. It also means the first pass of the chain, which already writes
    // the correct (0,1) (0,-1) (2,1), is skipped rather than re-scaled.
    //
    // Idempotent by construction: once corrected the values no longer match the signature, so a repeated scan is a
    // no-op. That matters because these are pooled 192-byte pages and a MAP_WRITE_NO_OVERWRITE leaves already-corrected
    // neighbours visible in the same page.
    bool CorrectQuadTexcoord(float* t0, float* t1, float* t2)
    {
        const float expectedU = 2.0f * 1920.0f / static_cast<float>(*InternalHorizontalRes);
        const float expectedV = 1080.0f / static_cast<float>(*InternalVerticalRes);

        // The shape the engine writes: (0, V) (0, -V) (U, V).
        if (!NearlyEqual(t0[0], 0.0f) || !NearlyEqual(t1[0], 0.0f)) { return false; }
        if (!NearlyEqual(t0[1], -t1[1])) { return false; }
        if (!NearlyEqual(t2[0], expectedU) || !NearlyEqual(t2[1], t0[1])) { return false; }
        if (!NearlyEqual(fabsf(t0[1]), expectedV)) { return false; }

        const float sign = (t0[1] >= 0.0f) ? 1.0f : -1.0f;  // Preserve which way the source is sampled vertically.
        t0[0] = 0.0f;  t0[1] =  sign;
        t1[0] = 0.0f;  t1[1] = -sign;
        t2[0] = 2.0f;  t2[1] =  sign;
        return true;
    }

    // Walks a 192 byte page looking for vertex sets to correct. Layout within a set is uniform across the chain: float2
    // POSITION at +0 followed by N float2 texcoords at +8, so the stride is always 8*(N+1) and the texcoord count falls
    // out of it.
    //
    // The page holds several independently sub-allocated sets, and they are packed sequentially at float2 granularity
    // rather than at stride-aligned offsets. Stepping base by the stride is what an earlier version of this did, and it
    // silently missed sets: a cutscene frame laid out stride 16 at 0, stride 24 at 48, then stride 16 at 120, and 120
    // is not a multiple of 16, so that last set kept its cropped coordinates while every other pass was corrected.
    // Gameplay frames happened to land everything on 0/48/96, which are multiples of their own strides, which is why it
    // looked complete at first. Stepping by 8 is the only assumption that holds; the position fingerprint is what keeps
    // the wider search from matching anything it should not.
    void CorrectYebisQuadBuffer(void* data, const UINT byteWidth)
    {
        static constexpr UINT strides[]  = { 16, 24, 32, 40, 48, 56, 64 };  // 64 is the widest that fits three vertices.
        static constexpr UINT kSubAlloc  = 8;  // Every attribute is R32G32_FLOAT, so sets start on a float2 boundary.
        auto* floats = static_cast<float*>(data);

        for (const UINT stride : strides) {
            const UINT floatStride = stride / sizeof(float);
            for (UINT base = 0; base + (stride * 3) <= byteWidth; base += kSubAlloc) {
                float* v0 = floats + (base / sizeof(float));
                float* v1 = v0 + floatStride;
                float* v2 = v1 + floatStride;
                if (!IsFullscreenTrianglePosition(v0, v1, v2)) { continue; }

                const UINT texcoordCount = (stride - 8) / 8;
                for (UINT t = 0; t < texcoordCount; ++t) {
                    const UINT offset = 2 + (t * 2);  // Skip POSITION.xy, then index into the texcoord pairs.
                    if (!CorrectQuadTexcoord(v0 + offset, v1 + offset, v2 + offset)) { continue; }

                    // Logged once per distinct layout rather than once per session. The offset-120 miss above was
                    // invisible in the log because the one summary line had already been printed by the passes that did
                    // get corrected, so a layout that is only reached in cutscenes never announced itself.
                    static std::set<UINT> loggedLayouts;
                    const UINT layout = (base << 8) | stride;  // Bounded: 24 offsets x 7 strides at most.
                    if (loggedLayouts.insert(layout).second) {
                        spdlog::info("Correcting YEBIS fullscreen quad texture coordinates at offset {} stride {}. The "
                                     "engine builds them as 1920x1080 over the real target size, so this pass was "
                                     "sampling only {:.2f}% x {:.2f}% of its source. Now sampling the full source at "
                                     "{}x{}.", base, stride,
                                     1920.0f / static_cast<float>(*InternalHorizontalRes) * 100.0f,
                                     1080.0f / static_cast<float>(*InternalVerticalRes) * 100.0f,
                                     *InternalHorizontalRes, *InternalVerticalRes);
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // TAA subpixel jitter.
    //
    // The engine ships a complete jittered-TAA path and then feeds it zero. PerViewCB's last float4,
    // u_ViewportSizeJitterOffset, is (width, height, jitterX, jitterY) in pixels, and every scene vertex shader opens
    // with
    //     0: add r0.xy, u_ViewportSizeJitterOffset.zwzz, u_ViewportSizeJitterOffset.zwzz
    //     1: div r0.xy, r0.xyxx, u_ViewportSizeJitterOffset.xyxx      <- 2 * jitterPixels / viewportSize, i.e. NDC
    //    ...
    //    39: mad o0.xy, r0.xyxx, r4.wwww, r4.xyxx                     <- clip.xy += jitterNDC * clip.w
    // which is the textbook jitter application. Measured on both a static and a moving frame, .zw is always (-0.0, 0.0).
    // So the game pays TAA's full blur cost and gets none of its supersampling.
    //
    // Enabling it does not corrupt motion vectors. The GBuffer pixel shader rebuilds the current clip position from the
    // interpolated world position and u_ViewProjectionMatrix rather than reusing SV_POSITION:
    //    166: div r2.xy, r2.xyxx, r2.zzzz         <- current NDC, no jitter term
    //    167: add r0.xz, r0.xxzx, -r2.xxyx        <- previous NDC minus current NDC
    // so velocity is a difference of two unjittered positions either way.
    //
    // What this does not fix: TemporalAACB's reconstruction weights are compile-time constants (bit-identical across
    // two captures from different scenes), so the resolve filter is not jitter aware. That needs the resolve shader
    // replaced, which is the next step. Jitter is worth doing first because it is small, reversible, and live.
    constexpr int kJitterPhases = 8;  // 8 phases suits the resolve's 3x3 neighbourhood clamp; more risks ghosting.

    float HaltonSequence(uint32_t index, const uint32_t base)
    {
        float fraction = 1.0f;
        float result   = 0.0f;
        while (index > 0) {
            fraction /= static_cast<float>(base);
            result   += fraction * static_cast<float>(index % base);
            index    /= base;
        }
        return result;
    }

    // Advanced once per present, never per draw: every draw in a frame has to be jittered by the same amount or the
    // geometry tears against itself between passes.
    std::atomic<uint32_t> jitterFrameIndex{ 0 };

    void CurrentJitter(float& outX, float& outY)
    {
        // +1 because Halton(0) is 0 for every base, which would waste a phase on no jitter at all.
        const uint32_t index = (jitterFrameIndex.load(std::memory_order_relaxed) % kJitterPhases) + 1;
        outX = HaltonSequence(index, 2) - 0.5f;  // Halton is [0,1), so this centres the sequence on the pixel.
        outY = HaltonSequence(index, 3) - 0.5f;
    }

    // PerViewCB is 752 bytes with u_ViewportSizeJitterOffset as its final float4, so the jitter sits at byte 736.
    // Rather than trusting that offset alone, the first two components have to match the resolution we are rendering
    // at. That is what keeps this off the shadow cascades and any other view: they run this same constant buffer layout
    // with their own viewport size, and (3440,1440) appears nowhere else in the 188 floats.
    constexpr UINT kPerViewCBSize     = 752;
    constexpr UINT kJitterFloatOffset = 184;  // 736 / sizeof(float)

    void ApplyTemporalJitter(void* data, const UINT byteWidth)
    {
        if (byteWidth != kPerViewCBSize) { return; }

        auto* floats = static_cast<float*>(data);
        if (floats[kJitterFloatOffset + 0] != static_cast<float>(*InternalHorizontalRes)) { return; }
        if (floats[kJitterFloatOffset + 1] != static_cast<float>(*InternalVerticalRes))   { return; }

        float jitterX = 0.0f;
        float jitterY = 0.0f;
        CurrentJitter(jitterX, jitterY);
        floats[kJitterFloatOffset + 2] = jitterX;
        floats[kJitterFloatOffset + 3] = jitterY;

        // Logged once per distinct resolution rather than once ever. Resolution detection runs a second or so after
        // the first views are built, so a single log line fires against the startup 1080p view and then claims that
        // is the resolution being jittered for the rest of the session.
        static int loggedJitterWidth  = 0;
        static int loggedJitterHeight = 0;
        if (loggedJitterWidth != *InternalHorizontalRes || loggedJitterHeight != *InternalVerticalRes) {
            loggedJitterWidth  = *InternalHorizontalRes;
            loggedJitterHeight = *InternalVerticalRes;
            spdlog::info("Applying TAA subpixel jitter. The engine left u_ViewportSizeJitterOffset.zw at (0,0), so its "
                         "temporal AA was smoothing without supersampling. Now driving it with a {} phase Halton(2,3) "
                         "sequence at {}x{}.", kJitterPhases, *InternalHorizontalRes, *InternalVerticalRes);
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Jitter aware reconstruction weights.
    //
    // Jitter alone makes the image softer standing still, which is expected: the resolve's 3x3 filter is a fixed kernel
    // centred on the pixel, so accumulating eight subpixel-shifted renders through it averages them into a box blur
    // instead of resolving them. The filter has to follow the sample.
    //
    // TemporalAACB is only 80 bytes and, usefully, the engine rewrites it every frame (CPUWrite immediately before the
    // TAA draw), so this needs no shader replacement -- the same Map/Unmap seam works. Tap order was read off the
    // resolve disassembly and then confirmed against the shipped values:
    //     u_Weight1    = (-1,-1) (0,-1) (1,-1) (-1,0)          sharp filter
    //     u_Weight2    = ( 1, 0) (-1,1) (0, 1) ( 1,1)
    //     u_WeightCenter.x / .y                                 sharp centre / low centre
    //     u_WeightLow1 / u_WeightLow2                           same taps, wide filter
    //
    // Both shipped kernels are exactly separable Gaussians: sqrt(0.5516272) * sqrt(0.0165487) = 0.0955444, which is the
    // edge weight to seven digits, and each sums to 1.0. So rather than hardcoding a width, the width is recovered from
    // whatever the engine wrote this frame and the kernel is rebuilt around the jitter offset. At zero jitter that
    // reproduces the original values exactly, and it survives the engine using different constants for another quality
    // level.
    constexpr UINT kTemporalAACBSize = 80;

    // Sign convention, derived from the clip space maths rather than guessed:
    //   ndc.x += 2*jx/W  =>  screen.x += jx        (NDC +x is right, so geometry moves right by jx)
    //   ndc.y += 2*jy/H  =>  screen.y -= jy        (NDC +y is up, but texel +y is down, so the sign flips)
    // A tap at texel offset (dx,dy) therefore samples the true scene at (dx - jx, dy + jy) relative to the pixel
    // centre, which is where the kernel has to be evaluated.
    bool RebuildJitteredKernel(const float centreWeight, const float cornerWeight,
                               const float jitterX, const float jitterY, const float sigmaScale,
                               float outWeights[3][3])
    {
        if (centreWeight <= 0.0f || cornerWeight <= 0.0f) { return false; }

        const float centre1D = sqrtf(centreWeight);
        const float corner1D = sqrtf(cornerWeight);
        if (corner1D >= centre1D) { return false; }  // Not a peaked kernel; nothing sensible to recentre.

        const float ratio = corner1D / centre1D;
        // exp(-1 / (2*sigma^2)) == ratio, folded to one term. Scaling sigma scales this by the square.
        const float twoSigmaSquared = (-1.0f / logf(ratio)) * (sigmaScale * sigmaScale);
        if (!(twoSigmaSquared > 0.0f)) { return false; }

        float weightsX[3];
        float weightsY[3];
        for (int i = 0; i < 3; ++i) {
            const float offset = static_cast<float>(i - 1);
            const float dx = offset - jitterX;
            const float dy = offset + jitterY;
            weightsX[i] = expf(-(dx * dx) / twoSigmaSquared);
            weightsY[i] = expf(-(dy * dy) / twoSigmaSquared);
        }

        float total = 0.0f;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                outWeights[y][x] = weightsX[x] * weightsY[y];
                total += outWeights[y][x];
            }
        }
        if (!(total > 0.0f)) { return false; }

        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) { outWeights[y][x] /= total; }
        }
        return true;
    }

    void ApplyJitteredResolveWeights(void* data, const UINT byteWidth)
    {
        if (byteWidth != kTemporalAACBSize) { return; }

        auto* floats = static_cast<float*>(data);
        // Float indices into the five float4s.
        constexpr int kWeight1 = 0, kWeightLow1 = 4, kWeight2 = 8, kWeightLow2 = 12, kWeightCentre = 16;

        // Fingerprint: both kernels have to be normalised the way the engine writes them. This is what keeps the patch
        // off any other 80 byte constant buffer that happens to pass through Map.
        const float sharpSum = floats[kWeight1 + 0] + floats[kWeight1 + 1] + floats[kWeight1 + 2] + floats[kWeight1 + 3]
                             + floats[kWeight2 + 0] + floats[kWeight2 + 1] + floats[kWeight2 + 2] + floats[kWeight2 + 3]
                             + floats[kWeightCentre + 0];
        const float lowSum = floats[kWeightLow1 + 0] + floats[kWeightLow1 + 1] + floats[kWeightLow1 + 2]
                           + floats[kWeightLow1 + 3] + floats[kWeightLow2 + 0] + floats[kWeightLow2 + 1]
                           + floats[kWeightLow2 + 2] + floats[kWeightLow2 + 3] + floats[kWeightCentre + 1];
        if (fabsf(sharpSum - 1.0f) > 1e-3f || fabsf(lowSum - 1.0f) > 1e-3f) { return; }

        // The width has to come from the engine's own kernel, never from whatever is in the buffer right now.
        //
        // Deriving it in place looked fine and was wrong: after the first patch the buffer holds a recentred kernel,
        // which is asymmetric, so sqrt(corner)/sqrt(centre) stops being the side-to-centre ratio. That fed each frame's
        // width from the previous frame's output and settled on a fixed point that barely moved when the sharpness
        // setting changed, which is precisely what "the slider does nothing" looked like from the outside.
        //
        // Only the engine writes a symmetric kernel, so symmetry is what identifies an untouched buffer worth caching.
        // Re-checking every frame rather than latching once means a quality preset change is still picked up.
        static float baselineSharpCentre = 0.0f, baselineSharpCorner = 0.0f;
        static float baselineLowCentre   = 0.0f, baselineLowCorner   = 0.0f;

        const bool symmetric = fabsf(floats[kWeight1 + 0] - floats[kWeight2 + 3]) < 1e-6f   // (-1,-1) vs ( 1, 1)
                            && fabsf(floats[kWeight1 + 2] - floats[kWeight2 + 1]) < 1e-6f;  // ( 1,-1) vs (-1, 1)
        if (symmetric) {
            baselineSharpCentre = floats[kWeightCentre + 0];
            baselineSharpCorner = floats[kWeight1 + 0];
            baselineLowCentre   = floats[kWeightCentre + 1];
            baselineLowCorner   = floats[kWeightLow1 + 0];
        }
        if (baselineSharpCentre <= 0.0f) { return; }  // Never seen an untouched kernel; nothing to derive a width from.

        float jitterX = 0.0f;
        float jitterY = 0.0f;
        CurrentJitter(jitterX, jitterY);

        // 0 keeps the engine's width, 100 narrows to 0.6x. Only the sharp filter is narrowed: the wide one feeds the
        // neighbourhood clamp box, and tightening that would clip more history and trade softness for flicker.
        const int   sharpness  = std::clamp(PlayerSettingsRm.RS.TAASharpness, 0, 100);
        const float sigmaScale = 1.0f - (0.4f * static_cast<float>(sharpness) / 100.0f);

        float sharp[3][3];
        float low[3][3];
        if (!RebuildJitteredKernel(baselineSharpCentre, baselineSharpCorner,
                                   jitterX, jitterY, sigmaScale, sharp)) { return; }
        if (!RebuildJitteredKernel(baselineLowCentre, baselineLowCorner,
                                   jitterX, jitterY, 1.0f, low)) { return; }

        // Logged once per distinct sharpness setting. Only sampled on one jitter phase, because the centre weight also
        // moves with the jitter and logging on whichever frame the setting changed made the numbers incomparable.
        static int loggedSharpness = -1;
        const bool onReferencePhase = (jitterFrameIndex.load(std::memory_order_relaxed) % kJitterPhases) == 0;
        if (loggedSharpness != sharpness && onReferencePhase) {
            loggedSharpness = sharpness;
            spdlog::info("TAA reconstruction filter: sharpness {} scales the engine's sigma by {:.3f}, taking the "
                         "centre tap weight from {:.4f} to {:.4f} (measured on a fixed jitter phase).", sharpness,
                         sigmaScale, baselineSharpCentre, sharp[1][1]);
        }

        // [y][x] with y=0 the row above, matching the tap order above.
        floats[kWeight1 + 0] = sharp[0][0];  floats[kWeightLow1 + 0] = low[0][0];  // (-1,-1)
        floats[kWeight1 + 1] = sharp[0][1];  floats[kWeightLow1 + 1] = low[0][1];  // ( 0,-1)
        floats[kWeight1 + 2] = sharp[0][2];  floats[kWeightLow1 + 2] = low[0][2];  // ( 1,-1)
        floats[kWeight1 + 3] = sharp[1][0];  floats[kWeightLow1 + 3] = low[1][0];  // (-1, 0)
        floats[kWeightCentre + 0] = sharp[1][1];
        floats[kWeightCentre + 1] = low[1][1];                                     // ( 0, 0)
        floats[kWeight2 + 0] = sharp[1][2];  floats[kWeightLow2 + 0] = low[1][2];  // ( 1, 0)
        floats[kWeight2 + 1] = sharp[2][0];  floats[kWeightLow2 + 1] = low[2][0];  // (-1, 1)
        floats[kWeight2 + 2] = sharp[2][1];  floats[kWeightLow2 + 2] = low[2][1];  // ( 0, 1)
        floats[kWeight2 + 3] = sharp[2][2];  floats[kWeightLow2 + 3] = low[2][2];  // ( 1, 1)

        static bool loggedWeights = false;
        if (!loggedWeights) {
            spdlog::info("Recentring the TAA reconstruction filter on the jitter offset. The engine's kernel is fixed "
                         "on the pixel centre, which turns jittered samples into a blur instead of resolving them.");
            loggedWeights = true;
        }
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Temporal AA resolve replacement.
    //
    // Identification is by content, not by hash. There are at least three resolve variants in play across captures
    // (pixel shaders 2687, 2688 and 2693) and no way to tell from outside which one a given scene will build, so
    // hashing would mean chasing them one at a time. Every variant reflects the same four named resources, and the
    // names live as plain strings in the bytecode's RDEF chunk, so searching for "u_Color_Hist" catches all of them
    // and nothing else -- no other shader in the frame samples a colour history.
    //
    // The replacement is compiled at runtime through d3dcompiler_47.dll, which ships next to Application.exe. Loading
    // it dynamically rather than linking avoids adding an import that would have to resolve under Proton before the
    // mod could report anything about why it failed.
    bool BytecodeContains(const void* bytecode, const SIZE_T length, const char* needle)
    {
        const auto* bytes = static_cast<const char*>(bytecode);
        const size_t needleLength = strlen(needle);
        if (bytecode == nullptr || length < needleLength) { return false; }
        for (size_t i = 0; i + needleLength <= length; ++i) {
            if (memcmp(bytes + i, needle, needleLength) == 0) { return true; }
        }
        return false;
    }

    bool IsTemporalAAResolve(const void* bytecode, const SIZE_T length)
    {
        return BytecodeContains(bytecode, length, "u_Color_Hist")
            && BytecodeContains(bytecode, length, "u_GBuffer3_Curr");
    }

    using D3DCompileFn = HRESULT (WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR,
                                           LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

    // Compiled once and reused. A null result after the first attempt means compilation failed and every later call
    // should fall through to the engine's own shader rather than retry and re-log.
    ID3DBlob* CompiledResolveBlob()
    {
        static bool       attempted = false;
        static ID3DBlob*  blob      = nullptr;
        if (attempted) { return blob; }
        attempted = true;

        const HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
        if (compiler == nullptr) {
            spdlog::error("Temporal AA: could not load d3dcompiler_47.dll, so the resolve cannot be replaced.");
            return nullptr;
        }

        const auto compile = reinterpret_cast<D3DCompileFn>(
            reinterpret_cast<void*>(GetProcAddress(compiler, "D3DCompile")));
        if (compile == nullptr) {
            spdlog::error("Temporal AA: d3dcompiler_47.dll has no D3DCompile export.");
            return nullptr;
        }

        ID3DBlob* errors = nullptr;
        const HRESULT hr = compile(EnigmaFix::kTemporalAAResolveHLSL, strlen(EnigmaFix::kTemporalAAResolveHLSL), "TemporalAAResolve",
                                   nullptr, nullptr, "main", "ps_5_0",
                                   D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
        if (FAILED(hr) || blob == nullptr) {
            spdlog::error("Temporal AA: resolve shader failed to compile (0x{:08X}): {}", static_cast<uint32_t>(hr),
                          errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler output");
            if (errors != nullptr) { errors->Release(); }
            if (blob != nullptr) { blob->Release(); blob = nullptr; }
            return nullptr;
        }
        if (errors != nullptr) { errors->Release(); }  // Warnings only; the compile succeeded.

        spdlog::info("Temporal AA: compiled the replacement resolve shader ({} bytes).", blob->GetBufferSize());
        return blob;
    }

    bool ShouldReplaceTemporalAAResolve()
    {
        if (PlayerSettingsRm.GameMode != EnigmaFix::PlayerSettings::DERQ) { return false; }
        return PlayerSettingsRm.RS.TAAReplaceResolve;
    }

    bool ShouldApplyTemporalJitter()
    {
        if (PlayerSettingsRm.GameMode != EnigmaFix::PlayerSettings::DERQ) { return false; }
        if (!PlayerSettingsRm.RS.TAAJitter) { return false; }
        if (!PlayerSettingsRm.RS.TAA) { return false; }  // Pointless without a resolve to accumulate the phases.
        return *InternalHorizontalRes > 0 && *InternalVerticalRes > 0;
    }

    // Cheap rejection for the Map hook, which runs on every single Map the game makes. GetType is a virtual call with
    // no refcount traffic, and once it says Buffer the static_cast is valid, so nothing here needs a QueryInterface.
    bool IsYebisQuadBuffer(ID3D11Resource* pResource)
    {
        if (pResource == nullptr) { return false; }

        D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        pResource->GetType(&dimension);
        if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER) { return false; }

        D3D11_BUFFER_DESC desc = {};
        static_cast<ID3D11Buffer*>(pResource)->GetDesc(&desc);
        if (desc.ByteWidth != kYebisQuadBufferSize) { return false; }
        return (desc.BindFlags & D3D11_BIND_VERTEX_BUFFER) != 0;
    }

    bool IsConstantBufferOfSize(ID3D11Resource* pResource, const UINT byteWidth)
    {
        if (pResource == nullptr) { return false; }

        D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        pResource->GetType(&dimension);
        if (dimension != D3D11_RESOURCE_DIMENSION_BUFFER) { return false; }

        D3D11_BUFFER_DESC desc = {};
        static_cast<ID3D11Buffer*>(pResource)->GetDesc(&desc);
        if (desc.ByteWidth != byteWidth) { return false; }
        return (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
    }

    bool ShouldCorrectYebisQuads()
    {
        if (PlayerSettingsRm.GameMode != EnigmaFix::PlayerSettings::DERQ) { return false; }
        if (!PlayerSettingsRm.RES.UseCustomRes) { return false; }
        if (*InternalHorizontalRes <= 0 || *InternalVerticalRes <= 0) { return false; }
        // At 1080p the ratio is 1.0 and there is nothing to correct.
        return !(*InternalHorizontalRes == 1920 && *InternalVerticalRes == 1080);
    }
}

// The post processing chain derives every one of its viewport and scissor sizes from a hardcoded 1920x1080, so this
// set of sizes is what identifies one as belonging to it. Matching on the size rather than on the shape of the draw
// call is what lets unrelated viewports (UI, shadow passes, the main scene) pass through untouched.
bool IsHardcodedPostProcessSize(const UINT width, const UINT height)
{
    static constexpr struct { UINT Width; UINT Height; } sizes[] = {
        { 1920, 1080 }, { 960, 540 }, { 640, 360 }, { 480, 270 }, { 384, 216 },
        {  320,  180 }, { 240, 135 }, { 192, 108 }, { 160,  90 }, {  96,  54 },
        {   80,   46 }, {  48,  28 }, {  48,  27 }, {  40,  24 }, {  24,  14 },
        {   20,   12 }, {  12,   8 }, {  12,   7 }, {  10,   6 }, {   6,   4 },
    };
    for (const auto& size : sizes) {
        if (size.Width == width && size.Height == height) { return true; }
    }
    return false;
}

// Formats the post processing chain renders into. Anything else is left alone.
bool IsPostProcessFormat(const DXGI_FORMAT format)
{
    switch (format) {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        // The three below are the main scene GBuffer targets, and they were missing: a capture shows R16G16_SNORM with
        // 185 draws, R16G16B16A16_UNORM with 147 and R8G8B8A8_SRGB with 147, none of which were being matched. So the
        // whole opaque pass was rendering at whatever viewport the engine set, while only the post processing chain got
        // corrected.
        case DXGI_FORMAT_R16G16_SNORM:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return true;
        default:
            return false;
    }
}

// Describes the texture behind the currently bound render target. Every COM reference taken here is released here,
// which the previous inline version of this got wrong: it only released the render target view on the one path where
// it actually resized something, so every other exit leaked a reference and pinned stale render targets alive.
bool GetBoundRenderTargetDesc(ID3D11DeviceContext* pContext, D3D11_TEXTURE2D_DESC& outTexDesc, DXGI_FORMAT& outViewFormat)
{
    ID3D11RenderTargetView* rtView = nullptr;
    pContext->OMGetRenderTargets(1, &rtView, nullptr);
    if (rtView == nullptr) { return false; }

    D3D11_RENDER_TARGET_VIEW_DESC viewDesc = {};
    rtView->GetDesc(&viewDesc);
    outViewFormat = viewDesc.Format;

    ID3D11Resource* rt = nullptr;
    rtView->GetResource(&rt);
    rtView->Release();
    if (rt == nullptr) { return false; }

    ID3D11Texture2D* rtTex = nullptr;
    const HRESULT hr = rt->QueryInterface<ID3D11Texture2D>(&rtTex);
    rt->Release();
    if (FAILED(hr) || rtTex == nullptr) { return false; }

    rtTex->GetDesc(&outTexDesc);
    rtTex->Release();
    return true;
}

// Remembers which resize transitions have already been reported, so the per draw logging below stays at one line per
// distinct "AxB -> CxD" instead of one line per draw call.
bool ShouldLogResize(const UINT fromW, const UINT fromH, const UINT toW, const UINT toH)
{
    static std::map<uint64_t, bool> seen;
    const uint64_t key = (static_cast<uint64_t>(fromW) << 48) | (static_cast<uint64_t>(fromH) << 32)
                       | (static_cast<uint64_t>(toW)   << 16) |  static_cast<uint64_t>(toH);
    if (seen.find(key) != seen.cend()) { return false; }
    seen[key] = true;
    return true;
}

// Death end re;Quest caches its render size in a struct reached through a pointer at Application.exe+0x1339380, at
// +0x40 (width) and +0x44 (height). The engine leaves it at 1920x1080 no matter what resolution is actually in use.
//
// That matters because the YEBIS composite builds its fullscreen quad's texture coordinates on the CPU as
// cachedSize / textureSize. At 3440x1440 that gives 1920/3440 = 0.5581 and 1080/1440 = 0.75, so the composite samples
// only the top-left 55.8% by 75% of the scene and stretches it over the whole screen. Measured directly off the
// vertex data: TEXCOORD0 runs 0 to 1.116279 across a fullscreen triangle, which is 0.5581 across the visible half.
// At 1920x1080 the ratio is exactly 1.0, which is why this only ever appeared above 1080p and looked independent of
// aspect ratio.
//
// Refreshed every frame rather than patched once, because the engine rewrites the struct on resolution changes.
void RefreshCachedRenderSize()
{
    if (PlayerSettingsRm.GameMode != EnigmaFix::PlayerSettings::DERQ) { return; }
    if (!PlayerSettingsRm.RES.UseCustomRes) { return; }
    if (*InternalHorizontalRes <= 0 || *InternalVerticalRes <= 0) { return; }

    const auto baseModule = reinterpret_cast<uintptr_t>(GetModuleHandleA("Application.exe"));
    if (baseModule == 0) { return; }

    const auto slot = reinterpret_cast<uintptr_t*>(baseModule + 0x1339380);
    if (IsBadReadPtr(slot, sizeof(uintptr_t))) { return; }

    const uintptr_t cachedSize = *slot;
    if (cachedSize == 0) { return; }  // Not constructed yet; it appears during camera setup.

    auto* width  = reinterpret_cast<int*>(cachedSize + 0x40);
    if (IsBadWritePtr(width, sizeof(int) * 2)) { return; }
    auto* height = reinterpret_cast<int*>(cachedSize + 0x44);

    if (*width == *InternalHorizontalRes && *height == *InternalVerticalRes) { return; }

    static bool loggedRefresh = false;
    if (!loggedRefresh) {
        spdlog::info("Cached render size was {}x{} while rendering at {}x{}. Correcting it so the post processing "
                     "composite stops sampling only part of the scene.", *width, *height,
                     *InternalHorizontalRes, *InternalVerticalRes);
        loggedRefresh = true;
    }
    *width  = *InternalHorizontalRes;
    *height = *InternalVerticalRes;
}

// Corrects the viewport and scissor rect for the post processing chain, which the engine leaves hardcoded at 1920x1080
// no matter how large the render target actually is.
//
// This used to be reached only for draws with 3, 4 or 6 indices, on the assumption that every post process pass is a
// fullscreen quad. The composite pass responsible for the pause menu background and the camera transitions is a mesh
// draw with thousands of indices (see Notes/Rendering/Render Targets.md), so it never matched and its viewport stayed
// at 1080p. The mismatch between the viewport and its render target is itself the signal, so no draw call filtering is
// needed: the size and format checks below already pick out the passes that need correcting.
bool ResizePostProcessRasterizerState(ID3D11DeviceContext* pContext)
{
    if (!PlayerSettingsRm.RES.UseCustomRes) { return false; }

    UINT numViewports = 0;
    pContext->RSGetViewports(&numViewports, nullptr);
    if (numViewports != 1) { return false; }

    D3D11_VIEWPORT vp = {};
    pContext->RSGetViewports(&numViewports, &vp);

    // The viewport check comes first and on its own, because it rejects the overwhelming majority of draws. Querying
    // the scissor rect for all ~750 draws in a frame as well was pure overhead on every one that was never a
    // candidate. Anything the post processing chain touches sets both to the same hardcoded size, so a viewport that
    // does not match means the scissor will not either.
    if (!IsHardcodedPostProcessSize(static_cast<UINT>(vp.Width), static_cast<UINT>(vp.Height))) { return false; }

    UINT numRects = 0;
    pContext->RSGetScissorRects(&numRects, nullptr);
    D3D11_RECT rect = {};
    const bool hasScissor = (numRects == 1);
    if (hasScissor) { pContext->RSGetScissorRects(&numRects, &rect); }

    constexpr bool viewportMatches = true;
    const bool scissorMatches = hasScissor && IsHardcodedPostProcessSize(static_cast<UINT>(rect.right), static_cast<UINT>(rect.bottom));

    D3D11_TEXTURE2D_DESC texDesc = {};
    DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
    if (!GetBoundRenderTargetDesc(pContext, texDesc, viewFormat)) { return false; }
    if (!IsPostProcessFormat(viewFormat)) { return false; }

    bool changed = false;

    if (viewportMatches && static_cast<float>(texDesc.Width) != vp.Width) {
        // Logged once per distinct transition rather than per draw. This runs on every draw in the frame, and writing
        // a few hundred lines a second to a file sink and a console window was costing real frame time.
        if (ShouldLogResize(static_cast<UINT>(vp.Width), static_cast<UINT>(vp.Height), texDesc.Width, texDesc.Height)) {
            spdlog::info("Resizing {} viewport from {}x{} to {}x{}.", DXGIFormatToString(viewFormat),
                         static_cast<UINT>(vp.Width), static_cast<UINT>(vp.Height), texDesc.Width, texDesc.Height);
        }
        vp.Width  = static_cast<FLOAT>(texDesc.Width);
        vp.Height = static_cast<FLOAT>(texDesc.Height);
        pContext->RSSetViewports(1, &vp);
        changed = true;
    }

    if (scissorMatches && static_cast<LONG>(texDesc.Width) != rect.right) {
        if (ShouldLogResize(static_cast<UINT>(rect.right), static_cast<UINT>(rect.bottom), texDesc.Width, texDesc.Height)) {
            spdlog::info("Resizing {} scissor rect from {}x{} to {}x{}.", DXGIFormatToString(viewFormat),
                         rect.right, rect.bottom, texDesc.Width, texDesc.Height);
        }
        rect.right  = static_cast<LONG>(texDesc.Width);
        rect.bottom = static_cast<LONG>(texDesc.Height);
        pContext->RSSetScissorRects(1, &rect);
        changed = true;
    }

    return changed;
}

namespace EnigmaFix {
    // A hook that contains the needed logic to resize the framebuffer whenever the game resolution changes.
    HRESULT __stdcall RenderManager::hkResizeBuffers(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
    {
        // pDevice, pContext and mainRenderTargetView are only ever assigned inside hkPresent's one time init block, but
        // this hook is on a different vtable entry and the game resizes the swap chain during its startup resolution
        // change -- before the first Present has happened. Everything below therefore has to tolerate being called
        // with none of it set up yet, which is what was crashing: a call through a null device vtable slot.
        if (rm_Instance.oResizeBuffers == nullptr) {
            spdlog::critical("ResizeBuffers: The original function was never bound, so the resize cannot be forwarded.");
            return DXGI_ERROR_INVALID_CALL;
        }

        if (pContext != nullptr && mainRenderTargetView != nullptr) {
            pContext->OMSetRenderTargets(0, 0, 0);
            mainRenderTargetView->Release();
            mainRenderTargetView = nullptr;  // Otherwise the release below runs on a dangling pointer.
        }

        const HRESULT hr = rm_Instance.oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

        // Nothing to rebuild against until Present has handed us a device. The next Present creates the view anyway.
        if (pDevice == nullptr || pContext == nullptr) {
            spdlog::info("ResizeBuffers: Ran before the device was captured, so the render target view will be rebuilt on the next Present.");
            return hr;
        }

        ID3D11Texture2D* pBuffer = nullptr;
        // Throwing out of a hook unwinds through the game's own frames, which is undefined behaviour across the COM
        // boundary and takes the process down just as surely as the crash did. These report and return instead.
        if (const HRESULT bufferHr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBuffer));
            FAILED(bufferHr) || pBuffer == nullptr) {
            spdlog::error("ResizeBuffers: Failed to get the swap chain back buffer (0x{:08X}).", static_cast<unsigned>(bufferHr));
            return hr;
        }

        if (mainRenderTargetView != nullptr) {
            mainRenderTargetView->Release();
            mainRenderTargetView = nullptr;
        }
        if (const HRESULT viewHr = pDevice->CreateRenderTargetView(pBuffer, nullptr, &mainRenderTargetView); FAILED(viewHr)) {
            spdlog::error("ResizeBuffers: Failed to create the render target view (0x{:08X}).", static_cast<unsigned>(viewHr));
            mainRenderTargetView = nullptr;
        }
        pBuffer->Release(); // Release the reference acquired by GetBuffer

        if (mainRenderTargetView != nullptr) {
            pContext->OMSetRenderTargets(1, &mainRenderTargetView, nullptr);

            // Set up the viewport.
            D3D11_VIEWPORT vp;
            vp.Width    = static_cast<float>(Width);
            vp.Height   = static_cast<float>(Height);
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            vp.TopLeftX = 0.0f;
            vp.TopLeftY = 0.0f;
            pContext->RSSetViewports(1, &vp);
        }
        return hr;
    }

    // A hook that contains the needed logic for adding the imgui interface, and forcing flip model presentation.
    HRESULT __stdcall RenderManager::hkPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags) // Here's what happens when the swapchain is ready to be presented.
    {
        if (rm_Instance.oPresent == nullptr) { return E_FAIL; }  // Never bound; nothing safe to forward to.

        // One step per presented frame. It has to advance here rather than per Map, because PerViewCB is written twice
        // in a frame (once early, once again before the forward passes) and both writes have to carry the same offset
        // or the geometry would be jittered inconsistently between the GBuffer and everything drawn after it.
        jitterFrameIndex.fetch_add(1, std::memory_order_relaxed);

        if (!InitHook) { // Checks if the hook hasn't been initialized, and if not, does the needed deeds to hook ImGui.
            HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&pDevice));
            if (SUCCEEDED(hr))
            {
                pDevice->GetImmediateContext(&pContext);
                DXGI_SWAP_CHAIN_DESC sd;
                pSwapChain->GetDesc(&sd);
                window = sd.OutputWindow;
                PlayerSettingsRm.INS.dpiScale = Util::GetDPIScaleForWindow(window) * 100.0f;
                spdlog::info("RenderManager: Current DPI Scale is {}%.", PlayerSettingsRm.INS.dpiScale);
                ID3D11Texture2D* pBackBuffer;
                hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<LPVOID*>(&pBackBuffer));
                if (SUCCEEDED(hr)) {
                    pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
                    pBackBuffer->Release();
                }
                else { spdlog::error("Present: Failed to get the swap chain back buffer."); }
                oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
                InitImGui();
                InitHook = true;
            }
            else { spdlog::error("Present: Failed to get the swap chain device."); }
        }

        // TODO: Implement developer console and find a way to pipe SpdLog and standard logging to it.
        // RefreshCachedRenderSize() is deliberately not called. Forcing that struct to the real resolution made the
        // final output letterbox with black bars without fixing the crop, so it clearly feeds more than the composite's
        // texture coordinates. Kept in the file because the value it corrects is still the best lead we have.

        PlayerSettingsRm.ShowUI = PlayerSettingsRm.ShowEFUI || PlayerSettingsRm.ShowDevConsole; // Checks if either EFUI or the dev console are enabled, and if so, enable the showUI flag.

        if (PlayerSettingsRm.ShowUI) { // Checks if the showUI flag is enabled, and if so, draws the ImGui interface.
            // TODO: Figure out why the IMGUI UI crashes on Proton.
            // Creates a new ImGui frame.
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            NewFrame();
            // Start up the ImGui UI for EnigmaFix.
            UIManagerRenMan.Start(pDevice);
            // Ends drawing the ImGui frame.
            End();
            // Render the ImGui frame. We don't need to call EndFrame because that's already being done with Render.
            Render();
            // Finally draws the ImGui overlay on screen. Or at least tries to.
            if (mainRenderTargetView) {
                pContext->OMSetRenderTargets(1, &mainRenderTargetView, nullptr);
                ImGui_ImplDX11_RenderDrawData(GetDrawData());
            }
            else { spdlog::error("Render Target View is NULL."); }
        }

        UINT syncInterval = PlayerSettingsRm.SYNC.VSync ? PlayerSettingsRm.SYNC.SyncInterval : 0; // Converts the vSync checks to a quick variable to clean up space.
        // Flags has to be a combination of DXGI_PRESENT_*. This previously passed a swap effect ORed with a swap chain
        // creation flag, which works out to DXGI_PRESENT_RESTART plus an undefined bit, every single frame. Neither the
        // swap effect nor the tearing flag can be changed at present time: both are fixed when the game creates the
        // swap chain. The game's own flags are forwarded instead, so only the sync interval is overridden.
        return rm_Instance.oPresent(pSwapChain, syncInterval, Flags);
    }

    // A hook that contains the needed logic for changing the Shadow, SSR, SSAO, and Post Processing resolution.
    HRESULT __stdcall RenderManager::hkCreateTexture2D(ID3D11Device* pDevice, D3D11_TEXTURE2D_DESC* pDesc, D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
    {
        if (rm_Instance.oCreateTexture2D == nullptr) { return E_FAIL; }  // Never bound; nothing safe to forward to.

        // Update our rendering settings related settings before we modify anything.
        int ShadowRes = PlayerSettingsRm.RS.ShadowRes;
        int ScreenSpaceEffectsScale = PlayerSettingsRm.RS.ScreenSpaceEffectsDivider;
        int SSRScale = PlayerSettingsRm.RS.SSRScaleDivider;
        int iW = *InternalHorizontalRes;
        int iH = *InternalVerticalRes;

        // Checks to see if a render target is the current texture resource first before doing anything with it.
        // This tests the bits rather than comparing the whole field: a render target that also carries another bind
        // flag (an unordered access view, for instance) is still a render target, and an exact comparison silently
        // skipped every one of those.
        constexpr UINT renderTargetBindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if ((pDesc->BindFlags & renderTargetBindFlags) == renderTargetBindFlags) {
            // We are simply using this to update our current internal resolution for the rest of the logic.
            // Checks for the specific texture format for CopyDeferredColor_Hist, and checks to see if it has twelve mipmaps, if so, we got our suspect render target. 11 only works with resolutions below 1440p, while 12 only works with anything higher than 1080p.
            if (LooksLikeInternalResolutionTarget(pDesc)) {
                if (pDesc->Width != *InternalHorizontalRes || pDesc->Height != *InternalVerticalRes) {
                    iW = pDesc->Width;
                    iH = pDesc->Height;
                    *InternalHorizontalRes = iW;
                    *InternalVerticalRes = iH;
                    spdlog::info("Internal Rendering Resolution Changed To: {}x{}", iW, iH);
                    // Updates our internal rendering aspect ratio, which we can reference with our aspect ratio fixes.
                    PlayerSettingsRm.RES.InternalAspectRatio = static_cast<float>(*InternalHorizontalRes) / static_cast<float>(*InternalVerticalRes);
                }
            }
            // Do our shadow, ambient occlusion, and SSR patches first.
            // TODO: Figure out a way of checking this based on the game, as DERQ2 and the other Mizuchi games probably are using a modified version of the engine, so it probably does this a little differently.
            // TODO: Find a way of decoupling the SSAO scale drawcalls from the SSR ones. If only there was a way to get a name for the draw call...
            if (PlayerSettingsRm.RS.ScreenSpaceEffectsDivider != 2) {
                int HorizontalPPRes = *InternalHorizontalRes / ScreenSpaceEffectsScale;
                int VerticalPPRes = *InternalVerticalRes / ScreenSpaceEffectsScale;
                switch (pDesc->Format) {
                case DXGI_FORMAT_R16G16_FLOAT: { // ImageSpaceAO and ImageSpaceCrossBilateralH (SSAO and Horizontal SSAO Blurring) Render Targets
                    // Checks for render targets half the size of the current in-game resolution.
                    spdlog::info("Found ImageSpaceAO and ImageSpaceCrossBilateralH Render Targets. Changing resolution from {}x{} to {}x{}.", pDesc->Width, pDesc->Height, HorizontalPPRes, VerticalPPRes);
                    resizeRt(pDesc, (*InternalHorizontalRes / 2), (*InternalVerticalRes / 2), HorizontalPPRes, VerticalPPRes);
                    break;
                }
                case DXGI_FORMAT_R8_UNORM: { // ImageSpaceCrossBilateralV and ImageSpaceReflectionOutput2
                    spdlog::info("Found ImageSpaceCrossBilateralV and ImageSpaceReflectionOutput2 Render Targets. Changing resolution from {}x{} to {}x{}.", pDesc->Width, pDesc->Height, HorizontalPPRes, VerticalPPRes);
                    resizeRt(pDesc, (*InternalHorizontalRes / 2), (*InternalVerticalRes / 2), HorizontalPPRes, VerticalPPRes);
                    break;
                }
                case DXGI_FORMAT_R16G16B16A16_UNORM: { // DownsampleGBuffer0, which is used as an input for ImageSpaceReflection
                    spdlog::info("Found DownsampleGBuffer0 Render Target. Changing resolution from {}x{} to {}x{}.", pDesc->Width, pDesc->Height, HorizontalPPRes, VerticalPPRes);
                    resizeRt(pDesc, (*InternalHorizontalRes / 2), (*InternalVerticalRes / 2), HorizontalPPRes, VerticalPPRes);
                    break;
                }
                case DXGI_FORMAT_R32_FLOAT: { // CompositeDepthForRLR and ImageSpaceHiZ, which is used as an input for ImageSpaceReflection
                    spdlog::info("Found CompositeDepthForRLR and ImageSpaceHiZ Render Targets. Changing resolution from {}x{} to {}x{}.", pDesc->Width, pDesc->Height, HorizontalPPRes, VerticalPPRes);
                    resizeRt(pDesc, (*InternalHorizontalRes / 2), (*InternalVerticalRes / 2), HorizontalPPRes, VerticalPPRes);
                    break;
                }
                case DXGI_FORMAT_R11G11B10_FLOAT: { // ImageSpaceReflectionOutput1
                    spdlog::info("Found ImageSpaceReflectionOutput1 Render Target. Changing resolution from {}x{} to {}x{}.", pDesc->Width, pDesc->Height, HorizontalPPRes, VerticalPPRes);
                    resizeRt(pDesc, (*InternalHorizontalRes / 2), (*InternalVerticalRes / 2), HorizontalPPRes, VerticalPPRes);
                    break;
                }
                // NOTE: ImageSpaceCompositeRLR has a $u_DeferredColorMap input which seemingly has a really strange output, and this is used during MizuchiCopyBack, AddSubsurfaceScatteringDiffuse and some other things? The first one is important, and it leads back to a 1024x1024 render target (B8G8R8A8_UNORM), done before an ExecuteCommandList and ImageSpaceShadowFilter2.
                // If we go to the rasterizer part of this, it has a 0.5 X and 0.5 Y on the viewport, which might be fine, but more interestingly, if we look at the vertex shader for the draw call, in a constant buffer, there's a gWorld parameter which has a float4 with a 1920.00 X value on row 1, and a 1080.00 Y value on row 2.
                // This is probably the cause for the pause menu and image transition bugs.
                
                // Another thing is that the call after that one now has a proper render target size, but the viewport hasn't changed from 1920x1080 for some reason.
                default: { break; }
                }
            }

            // TODO: Need to decouple this from the resolution setting. Ideally, the post-processing should be changed based on the internal rendering resolution rather than the user decided resolution.
            // As we do plan on patching the in-game resolution option separately from the reported internal resolution, and the reported internal resolution should change as a result.
            // TODO: Figure out how to grab the yebismizuchi2 set of calls using the ID3DUserDefinedAnnotation system, so we can more accurately adjust these.
            // TODO: We need to find a way to get this so it can work with resolutions lower than 1920x1080 too, because it will still glitch out with resolutions lower than that.
            if (PlayerSettingsRm.RES.UseCustomRes) {
                // The resizes below are all relative to the detected internal resolution, which only becomes correct
                // once the 11/12 mip render target above has been seen. If a post processing target is created before
                // that happens, the internal resolution is still its 1920x1080 default and every resize here is a
                // silent no-op, so say so rather than leaving it to be worked out from a black screen.
                static bool warnedAboutDetectionOrder = false;
                if (!warnedAboutDetectionOrder && *InternalHorizontalRes == 1920 && *InternalVerticalRes == 1080
                    && (pDesc->Width != 1920 || pDesc->Height != 1080)) {
                    spdlog::warn("Internal resolution is still at its 1920x1080 default while creating a {}x{} target. "
                                 "If the game is not actually running at 1080p, resolution detection has not run yet "
                                 "and these resizes will do nothing.", pDesc->Width, pDesc->Height);
                    warnedAboutDetectionOrder = true;
                }
                if (pDesc->MipLevels == 1) {
                // if (*InternalHorizontalRes != 1920 || *InternalVerticalRes != 1080) { // Unsure if the InternalHorizontalRes > 1080 will cause a problem.
                    switch (pDesc->Format) {
                    case DXGI_FORMAT_R16G16B16A16_TYPELESS: {
                        resizeRt(pDesc, 1920, 1080, *InternalHorizontalRes, *InternalVerticalRes);
                        resizeRt(pDesc, 960, 540, (*InternalHorizontalRes / 2), (*InternalVerticalRes / 2));
                        resizeRt(pDesc, 480, 270, (*InternalHorizontalRes / 4), (*InternalVerticalRes / 4));
                        resizeRt(pDesc, 240, 135, (*InternalHorizontalRes / 8), (*InternalVerticalRes / 8));
                        resizeRt(pDesc, 192, 108, (*InternalHorizontalRes / 10), (*InternalVerticalRes / 10));
                        break;
                    }
                    case DXGI_FORMAT_R11G11B10_FLOAT: {
                        resizeRt(pDesc, 384, 216, (*InternalHorizontalRes / 5), (*InternalVerticalRes / 5));
                        resizeRt(pDesc, 320, 180, (*InternalHorizontalRes / 6), (*InternalVerticalRes / 6));
                        resizeRt(pDesc, 192, 108, (*InternalHorizontalRes / 10), (*InternalVerticalRes / 10));
                        resizeRt(pDesc, 96, 54, (*InternalHorizontalRes / 20), (*InternalVerticalRes / 20));
                        resizeRt(pDesc, 48, 28, (*InternalHorizontalRes / 40), (*InternalVerticalRes / 40));
                        resizeRt(pDesc, 48, 27, (*InternalHorizontalRes / 40), (*InternalVerticalRes / 40));
                        resizeRt(pDesc, 24, 14, (*InternalHorizontalRes / 80), (*InternalVerticalRes / 80));
                        resizeRt(pDesc, 12, 7, (*InternalHorizontalRes / 160), (*InternalVerticalRes / 160));
                        resizeRt(pDesc, 12, 8, (*InternalHorizontalRes / 160), (*InternalVerticalRes / 160));
                        resizeRt(pDesc, 6, 4, (*InternalHorizontalRes / 320), (*InternalVerticalRes / 320));
                        break;
                    }
                    case DXGI_FORMAT_R24G8_TYPELESS: {
                        resizeRt(pDesc, 1920, 1080, *InternalHorizontalRes, *InternalVerticalRes);
                        break;
                    }
                    case DXGI_FORMAT_B8G8R8A8_UNORM: {
                        // The pause menu background effect that occurs after the "mizuchi-copyback" tagged drawcall.
                        if (resizeRt(pDesc, 1920, 1080, *InternalHorizontalRes, *InternalVerticalRes)) {
                            spdlog::info("Resized Pause Menu Background Render Target (After 'mizuchi-copyback') from 1920x1080 to {}x{}.", pDesc->Width, pDesc->Height);
                        }
                        break;
                    }
                    case DXGI_FORMAT_R8G8B8A8_UNORM: { // The pause menu background effect that occurs after "yebismizuchi" tagged drawcalls.
                        if (resizeRt(pDesc, 1920, 1080, *InternalHorizontalRes, *InternalVerticalRes)) {
                            spdlog::info("Resized Pause Menu Background Render Target (After 'yebismizuchi') from 1920x1080 to {}x{}.", pDesc->Width, pDesc->Height);
                        }
                        break;
                    }
                    default: { break; }
                    }
                }
            }
        }
        constexpr UINT depthStencilBindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
        if ((pDesc->BindFlags & depthStencilBindFlags) == depthStencilBindFlags) {
            if (PlayerSettingsRm.RS.ShadowRes != 2048) {
                switch (pDesc->Format) { // For some weird reason, this switch statement won't detect SSAO or Screen Space Reflections unless I prioritize them.
                case DXGI_FORMAT_R32_TYPELESS: { // This checks for the R32_TYPELESS format which is used for shadows
                    spdlog::info("Found Shadow Render Target. Changing resolution from {} to {}.", pDesc->Width, ShadowRes);
                    resizeRt(pDesc, 2048, 2048, ShadowRes, ShadowRes);
                    break;
                }
                default: { break; }
                }
            }
        }
        return rm_Instance.oCreateTexture2D(pDevice, pDesc, pInitialData, ppTexture2D);
    }

    // A hook that contains the needed logic for running checks on Viewports and Scissor Rects.
    HRESULT __stdcall RenderManager::hkDrawIndexed(ID3D11DeviceContext* pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
    {
        // Runs on every draw. The composite pass that needs correcting is a mesh draw with thousands of indices, so the
        // old "3, 4 or 6 indices" filter skipped the one call that mattered. The size and format checks inside do the
        // selecting instead, and the cheap viewport check runs before any render target lookup.
        if (rm_Instance.oDrawIndexed == nullptr) { return E_FAIL; }  // Never bound; nothing safe to forward to.

        ResizePostProcessRasterizerState(pContext);

        // The YEBIS constant buffer patch does stay filtered. It targets the fullscreen passes specifically, and it
        // reads the constant buffer back from the GPU, which is far too expensive to do on every draw.
        if ((IndexCount == 3 || IndexCount == 4 || IndexCount == 6) && StartIndexLocation == 0 && BaseVertexLocation == 0) {
            // cbPatchYebis is deliberately not called. It scaled the YEBIS UV transform matrices by
            // internalRes/1920 x internalRes/1080, on the assumption that they were authored for 1080p and needed
            // widening. They are not: the engine already builds them for the real resolution, so the extra scale made
            // the composite sample only 1/1.79 = 55.8% by 1/1.33 = 75.0% of its source and blow that up to full
            // screen. Those are exactly the crop fractions measured from captures, and the scale is 1.0 at 1920x1080,
            // which is why the crop only ever appeared above 1080p and looked aspect-independent.
            //
            // Verified by bisecting a 3440x1440 capture: the scene going in (6862) and the chain's own intermediate
            // (8682) are both framed correctly, and only the composite that this patched (EID 6988 -> 6855) came out
            // cropped.
            // Disabled again, this time with the mechanism understood rather than guessed at.
            //
            // It was never a "wrong constant, right magnitude" counterweight. It is the correct correction applied to
            // exactly one pass. Only one vertex shader in the whole chain has am44_TransformMatrix (cb0[156], which is
            // 2496 bytes, hence the size guard above): the second pass, which computes
            //     o0 = dot(float4(uv, persp, 1), am44_TransformMatrix[0])
            // Scaling that diagonal by internalRes/1920 x internalRes/1080 un-crops that pass exactly, which is why
            // disabling it used to make things worse. It cannot reach the other ~33 passes, whose vertex shaders are
            // plain movs with no term to scale, so it only ever fixed one link in a chain that crops at every link.
            //
            // hkMap/hkUnmap now correct the texture coordinates in the vertex buffer, which covers every pass including
            // that one. Leaving this enabled as well would scale the second pass twice.
            //
            // If this is ever revived, check the matrix offset first: reflection reports eight matrices in
            // am44_TransformMatrix, and 8 * 64 = 512 puts the array at 2496 - 512 = 1984, not the 2048 hardcoded below.
            // If that is right then it was writing matrices [1]..[7] and skipping [0], the only one feeding TEXCOORD0.
            // cbPatchYebis(pContext);
            // The composite immediately after the "yebismizuchi2" marker samples the finished post processing result
            // through a 160 byte $Globals whose gWorld still describes a 1920x1080 quad, which is what crops the image
            // down to a corner. This was written for exactly that draw and had never been called.
            cbPatchMizuchiCopyback(pContext);
        }
        return rm_Instance.oDrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
    }

    // Records where the game is about to write, so hkUnmap can look at it. Nothing is modified here: the game has not
    // written anything yet at this point.
    HRESULT __stdcall RenderManager::hkMap(ID3D11DeviceContext* pContext, ID3D11Resource* pResource, UINT Subresource,
                                           D3D11_MAP MapType, UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource)
    {
        if (rm_Instance.oMap == nullptr) { return E_FAIL; }

        const HRESULT hr = rm_Instance.oMap(pContext, pResource, Subresource, MapType, MapFlags, pMappedResource);
        if (FAILED(hr)) { return hr; }
        if (Subresource != 0) { return hr; }
        if (pMappedResource == nullptr || pMappedResource->pData == nullptr) { return hr; }

        if (ShouldCorrectYebisQuads() && IsYebisQuadBuffer(pResource)) {
            std::lock_guard<std::mutex> lock(quadMapMutex);
            pendingQuadMaps[pResource] = { pMappedResource->pData, kYebisQuadBufferSize, MappedBufferKind::YebisQuad };
        }
        else if (ShouldApplyTemporalJitter() && IsConstantBufferOfSize(pResource, kPerViewCBSize)) {
            std::lock_guard<std::mutex> lock(quadMapMutex);
            pendingQuadMaps[pResource] = { pMappedResource->pData, kPerViewCBSize, MappedBufferKind::PerViewConstants };
        }
        else if (ShouldApplyTemporalJitter() && IsConstantBufferOfSize(pResource, kTemporalAACBSize)) {
            std::lock_guard<std::mutex> lock(quadMapMutex);
            pendingQuadMaps[pResource] = { pMappedResource->pData, kTemporalAACBSize,
                                           MappedBufferKind::TemporalAAConstants };
        }
        return hr;
    }

    // Corrects the fullscreen quad's texture coordinates in the window between the game finishing its write and the
    // driver being told about it. The pointer is still valid until the original Unmap runs, which is why the fix goes
    // here rather than anywhere else.
    void __stdcall RenderManager::hkUnmap(ID3D11DeviceContext* pContext, ID3D11Resource* pResource, UINT Subresource)
    {
        if (rm_Instance.oUnmap == nullptr) { return; }  // Never bound; nothing safe to forward to.

        if (Subresource == 0) {
            PendingQuadMap pending = {};
            bool           found   = false;
            {
                std::lock_guard<std::mutex> lock(quadMapMutex);
                const auto iter = pendingQuadMaps.find(pResource);
                if (iter != pendingQuadMaps.cend()) {
                    pending = iter->second;
                    found   = true;
                    pendingQuadMaps.erase(iter);
                }
            }
            if (found && pending.Data != nullptr) {
                switch (pending.Kind) {
                case MappedBufferKind::YebisQuad:       CorrectYebisQuadBuffer(pending.Data, pending.ByteWidth); break;
                case MappedBufferKind::PerViewConstants: ApplyTemporalJitter(pending.Data, pending.ByteWidth);   break;
                case MappedBufferKind::TemporalAAConstants:
                    ApplyJitteredResolveWeights(pending.Data, pending.ByteWidth);
                    break;
                }
            }
        }

        rm_Instance.oUnmap(pContext, pResource, Subresource);
    }

    // Substitutes our own temporal AA resolve while the engine is building its pipeline. Falls through to the original
    // bytecode on any failure, so a compiler problem costs the improvement rather than the frame.
    HRESULT __stdcall RenderManager::hkCreatePixelShader(ID3D11Device* pDevice, const void* pShaderBytecode,
                                                         SIZE_T BytecodeLength, ID3D11ClassLinkage* pClassLinkage,
                                                         ID3D11PixelShader** ppPixelShader)
    {
        if (rm_Instance.oCreatePixelShader == nullptr) { return E_FAIL; }

        if (ShouldReplaceTemporalAAResolve() && IsTemporalAAResolve(pShaderBytecode, BytecodeLength)) {
            if (ID3DBlob* replacement = CompiledResolveBlob()) {
                const HRESULT hr = rm_Instance.oCreatePixelShader(pDevice, replacement->GetBufferPointer(),
                                                                  replacement->GetBufferSize(), pClassLinkage,
                                                                  ppPixelShader);
                if (SUCCEEDED(hr)) {
                    // Counted rather than logged once, because the engine builds several resolve variants and knowing
                    // how many were swapped is what tells us whether one slipped through.
                    static int replaced = 0;
                    ++replaced;
                    spdlog::info("Temporal AA: replaced resolve shader variant {} (original was {} bytes).", replaced,
                                 static_cast<uint64_t>(BytecodeLength));
                    return hr;
                }
                spdlog::error("Temporal AA: the replacement resolve was rejected by the device (0x{:08X}); keeping the "
                              "engine's shader.", static_cast<uint32_t>(hr));
            }
        }

        return rm_Instance.oCreatePixelShader(pDevice, pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
    }

    // Another hook that contains the needed logic for running checks on Viewports and Scissor Rects.
    HRESULT __stdcall RenderManager::hkDraw(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertexLocation)
    {
        if (rm_Instance.oDraw == nullptr) { return E_FAIL; }  // Never bound; nothing safe to forward to.

        ResizePostProcessRasterizerState(pContext);
        return rm_Instance.oDraw(pContext, VertexCount, StartVertexLocation);
    }

    DWORD __stdcall RenderManager::InitD3D11Hook(LPVOID lpReserved) {
        // Starts Hooking
        bool InitHook = false;
        do {
            SetProcessDPIAware(); // Fix High DPI Scaling.
            if (init(RenderType::D3D11) == Status::Success) {
                // Binds the function we will be using to get imgui to draw on-screen.
                kiero::bind( 8, reinterpret_cast<void**>(&oPresent),reinterpret_cast<void*>(this->hkPresent));
                // Binds the function we will be using to resize the framebuffer when needed.
                kiero::bind(13, reinterpret_cast<void**>(&oResizeBuffers),reinterpret_cast<void*>(this->hkResizeBuffers));
                switch (PlayerSettingsRm.GameMode) { // Only Initialize rendering hooks for DERQ for now, since we don't want to cause issues with the other games until support rolls out for those.
                    case PlayerSettings::DERQ: {
                        // Binds the function we will be using to modify the render target size for shadows and other things.
                        kiero::bind(23, reinterpret_cast<void**>(&oCreateTexture2D), reinterpret_cast<void*>(this->hkCreateTexture2D));
                        // Binds another function we will be using for resizing viewports.
                        kiero::bind(73, reinterpret_cast<void**>(&oDrawIndexed), reinterpret_cast<void*>(this->hkDrawIndexed));
                        // Binds the function we will be using for resizing viewports.
                        kiero::bind(74, reinterpret_cast<void**>(&oDraw), reinterpret_cast<void*>(this->hkDraw));
                        // Binds the pair we use to correct the YEBIS fullscreen quad's texture coordinates, which the
                        // engine builds from a render size stuck at 1920x1080. Not bound for DERQ2 yet: it is the same
                        // engine and very likely has the same bug, but that has not been measured.
                        kiero::bind(75, reinterpret_cast<void**>(&oMap), reinterpret_cast<void*>(this->hkMap));
                        kiero::bind(76, reinterpret_cast<void**>(&oUnmap), reinterpret_cast<void*>(this->hkUnmap));
                        // Swaps the temporal AA resolve as the engine builds its shaders.
                        kiero::bind(33, reinterpret_cast<void**>(&oCreatePixelShader), reinterpret_cast<void*>(this->hkCreatePixelShader));
                        break;
                    }
                    case PlayerSettings::DERQ2: {
                        // Binds the function we will be using to modify the render target size for shadows and other things.
                        kiero::bind(23, reinterpret_cast<void**>(&oCreateTexture2D), reinterpret_cast<void*>(this->hkCreateTexture2D));
                        // Binds another function we will be using for resizing viewports.
                        kiero::bind(73, reinterpret_cast<void**>(&oDrawIndexed),reinterpret_cast<void*>(this->hkDrawIndexed));
                        // Binds the function we will be using for resizing viewports.
                        kiero::bind(74, reinterpret_cast<void**>(&oDraw), reinterpret_cast<void*>(this->hkDraw));
                        break;
                    }
                    case PlayerSettings::Varnir: {
                        break;
                    }
                    case PlayerSettings::VIIR: {
                        break;
                    }
                    case PlayerSettings::NVS: {
                        break;
                    }
                    case PlayerSettings::MS2: {
                        break;
                    }
                    case PlayerSettings::MSF: {
                        break;
                    }
                    default: {
                        break;
                    }
                }
                InitHook = true;
            }
        }
        while (!InitHook);
        return TRUE;
    }
}