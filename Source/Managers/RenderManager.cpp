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
// System Libraries
//#include <comip.h>
#include <memory>
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
bool UseCustomRes           = PlayerSettingsRm.RES.UseCustomRes;
int HorizontalRes           = PlayerSettingsRm.RES.Resolution.x;
int VerticalRes             = PlayerSettingsRm.RES.Resolution.y;
bool UseResolutionScale     = PlayerSettingsRm.RES.UseCustomResScale;
int ResolutionScale         = PlayerSettingsRm.RES.CustomResScale;
int* InternalHorizontalRes   = &PlayerSettingsRm.INS.InternalResolution.x;
int* InternalVerticalRes     = &PlayerSettingsRm.INS.InternalResolution.y;
int ScreenSpaceEffectsScale = PlayerSettingsRm.RS.ScreenSpaceEffectsDivider;
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

// An easily callable function to make the CreateTexture2D hook a little cleaner.
void resizeRt(D3D11_TEXTURE2D_DESC *pDesc, int Width, int Height, int DesiredWidth, int DesiredHeight)
{
    if (pDesc->Width == Width && pDesc->Height == Height) {
        pDesc->Width  = static_cast<UINT>(DesiredWidth);
        pDesc->Height = static_cast<UINT>(DesiredHeight);
    }
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
    if (*InternalHorizontalRes != 1920 || *InternalVerticalRes != 1080) return; // Only needed when the resolution isn't 1920x1080

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

bool vpResize(ID3D11DeviceContext* pContext)
{
    if (PlayerSettingsRm.RES.UseCustomRes) {
        UINT numViewports = 0;
        pContext->RSGetViewports(&numViewports, nullptr);
        if (numViewports == 1) {
            D3D11_VIEWPORT vp = {};
            pContext->RSGetViewports(&numViewports, &vp);
            // This is where we can call each viewport that needs to be resized.
            // Since the post-process pipeline is hardcoded to 1080p, I think we can get away with murder when it comes to viewport resizing.
            if ((vp.Width == 1920.0f && vp.Height == 1080.0f) ||
                (vp.Width ==  960.0f && vp.Height ==  540.0f) ||
                (vp.Width ==  640.0f && vp.Height ==  360.0f) || // Previously commented out
                (vp.Width ==  480.0f && vp.Height ==  270.0f) ||
                (vp.Width ==  384.0f && vp.Height ==  216.0f) ||
                (vp.Width ==  320.0f && vp.Height ==  180.0f) ||
                (vp.Width ==  240.0f && vp.Height ==  135.0f) ||
                (vp.Width ==  192.0f && vp.Height ==  108.0f) ||
                (vp.Width ==  160.0f && vp.Height ==   90.0f) || // Previously commented out
                (vp.Width ==   96.0f && vp.Height ==   54.0f) ||
                (vp.Width ==   80.0f && vp.Height ==   46.0f) || // Previously commented out
                (vp.Width ==   48.0f && vp.Height ==   28.0f) ||
                (vp.Width ==   48.0f && vp.Height ==   27.0f) ||
                (vp.Width ==   40.0f && vp.Height ==   24.0f) || // Previously commented out
                (vp.Width ==   24.0f && vp.Height ==   14.0f) ||
                (vp.Width ==   20.0f && vp.Height ==   12.0f) || // Previously commented out
                (vp.Width ==   12.0f && vp.Height ==    8.0f) ||
                (vp.Width ==   12.0f && vp.Height ==    7.0f) ||
                (vp.Width ==   10.0f && vp.Height ==    6.0f) || // Previously commented out
                (vp.Width ==    6.0f && vp.Height ==    4.0f)) {
                spdlog::info("Found viewport with size {}x{}.", vp.Width, vp.Height);
                ID3D11RenderTargetView *rtView = nullptr;
                pContext->OMGetRenderTargets(1, &rtView, nullptr);
                if (rtView) {
                    D3D11_RENDER_TARGET_VIEW_DESC desc;
                    rtView->GetDesc(&desc);
                    if (desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
                        desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT    ||
                        desc.Format == DXGI_FORMAT_R11G11B10_FLOAT       ||
                        desc.Format == DXGI_FORMAT_R24G8_TYPELESS        ||
                        desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM        ||
                        desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                        spdlog::info("Found viewport with type {}.", DXGIFormatToString(desc.Format));
                        ID3D11Resource *rt = nullptr;
                        rtView->GetResource(&rt);
                        if (rt != nullptr) {
                            ID3D11Texture2D *rttex = nullptr;
                            rt->QueryInterface<ID3D11Texture2D>(&rttex);
                            if (rttex != nullptr) {
                                D3D11_TEXTURE2D_DESC texdesc = {};
                                rttex->GetDesc(&texdesc);
                                if (texdesc.Width != vp.Width) {
                                    // Here we go!
                                    // Viewport is the easy part
                                    vp.Width = static_cast<FLOAT>(texdesc.Width);
                                    vp.Height = static_cast<FLOAT>(texdesc.Height);
                                    pContext->RSSetViewports(1, &vp);
                                    spdlog::info("Set viewport to size {}x{}.", vp.Width, vp.Height);
                                    //cbResize(pContext, desc, texdesc, vp); // We should run the constant buffer modification right after viewport resizing.
                                    return true;
                                }
                            }
                        }
                        else { spdlog::error("Viewport Resource returned null."); }
                        rt->Release();
                    }
                }
                rtView->Release();
            }
        }
    }
    return false;
}

bool srResize(ID3D11DeviceContext* pContext)
{
    if (PlayerSettingsRm.RES.UseCustomRes) {
        // This is where scissor rect resizing will occur.
        UINT numRects = 0;
        pContext->RSGetScissorRects(&numRects, nullptr);
        if (numRects == 1) {
            D3D11_RECT rect = {};
            pContext->RSGetScissorRects(&numRects, &rect);
            if ((rect.right ==   1920 && rect.bottom ==  1080)  ||
                (rect.right ==    960 && rect.bottom ==   540)  ||
                (rect.right ==    640 && rect.bottom ==   360)  || // Previously commented out
                (rect.right ==    480 && rect.bottom ==   270)  ||
                (rect.right ==    384 && rect.bottom ==   216)  ||
                (rect.right ==    320 && rect.bottom ==   180)  ||
                (rect.right ==    240 && rect.bottom ==   135)  ||
                (rect.right ==    192 && rect.bottom ==   108)  ||
                (rect.right ==    160 && rect.bottom ==    90)  || // Previously commented out
                (rect.right ==     96 && rect.bottom ==    54)  ||
                (rect.right ==     80 && rect.bottom ==    46)  || // Previously commented out
                (rect.right ==     48 && rect.bottom ==    28)  ||
                (rect.right ==     48 && rect.bottom ==    27)  ||
                (rect.right ==     40 && rect.bottom ==    24)  || // Previously commented out
                (rect.right ==     24 && rect.bottom ==    14)  ||
                (rect.right ==     20 && rect.bottom ==    12)  || // Previously commented out
                (rect.right ==     12 && rect.bottom ==     8)  ||
                (rect.right ==     12 && rect.bottom ==     7)  ||
                (rect.right ==     10 && rect.bottom ==     6)  || // Previously commented out
                (rect.right ==      6 && rect.bottom ==     4)) {
                spdlog::info("Found scissor rect with size {}x{}.", rect.right, rect.bottom);
                ID3D11RenderTargetView *rtView = nullptr;
                pContext->OMGetRenderTargets(1, &rtView, nullptr);
                if (rtView) {
                    D3D11_RENDER_TARGET_VIEW_DESC desc;
                    rtView->GetDesc(&desc);
                    if (desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
                        desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT    ||
                        desc.Format == DXGI_FORMAT_R11G11B10_FLOAT       ||
                        desc.Format == DXGI_FORMAT_R24G8_TYPELESS        ||
                        desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM        ||
                        desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                        spdlog::info("Found scissor rect with type {}.", DXGIFormatToString(desc.Format));
                        ID3D11Resource *rt = nullptr;
                        rtView->GetResource(&rt);
                        if (rt != nullptr) {
                            ID3D11Texture2D *rttex = nullptr;
                            rt->QueryInterface<ID3D11Texture2D>(&rttex);
                            if (rttex != nullptr) {
                                D3D11_TEXTURE2D_DESC texdesc = {};
                                rttex->GetDesc(&texdesc);
                                if (texdesc.Width != rect.right) {
                                    // Here we go!
                                    // Viewport is the easy part
                                    rect.right = static_cast<LONG>(texdesc.Width);
                                    rect.bottom = static_cast<LONG>(texdesc.Height);
                                    pContext->RSSetScissorRects(1, &rect);
                                    spdlog::info("Set scissor rect to size {}x{}.", rect.right, rect.bottom);
                                    return true;
                                }
                            }
                        }
                        else { spdlog::error("Scissor Rect Resource returned null."); }
                        rt->Release();
                    }
                }
                rtView->Release();
            }
        }
    }
    return false;
}

namespace EnigmaFix {
    // A hook that contains the needed logic to resize the framebuffer whenever the game resolution changes.
    HRESULT __stdcall RenderManager::hkResizeBuffers(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
    {
        if (mainRenderTargetView) {
            pContext->OMSetRenderTargets(0, 0, 0);
            mainRenderTargetView->Release(); // WE DON'T NEED TO SET THE RTVIEW TO NULL AFTER RELEASING IT, GPT!
        }

        // TODO: Potentially investigate why oResizeBuffers isn't working.
        // I have a theory that it might be the rm_Instance.original functions that are causing problems.
        HRESULT hr = rm_Instance.oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
        
        ID3D11Texture2D* pBuffer = nullptr;
        hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBuffer);
        if (SUCCEEDED(hr)) {
            if (mainRenderTargetView) {
                mainRenderTargetView->Release(); // WE DON'T NEED TO SET THE MRTVIEW TO NULL AFTER RELEASING IT, GPT!
            }
            DX::ThrowIfFailed(pDevice->CreateRenderTargetView(pBuffer, nullptr, &mainRenderTargetView));
            pBuffer->Release(); // Release the reference acquired by GetBuffer
        }
        else { throw std::runtime_error("Failed to create render target view."); } // Throw a standard C++ exception with an error message.

        pContext->OMSetRenderTargets(1, &mainRenderTargetView, nullptr);

        // Set up the viewport.
        D3D11_VIEWPORT vp;
        vp.Width    = static_cast<float>(Width);
        vp.Height   = static_cast<float>(Height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        pContext->RSSetViewports(1, &vp); //how to check if any boolean is equal to true
        return hr;
    }

    // A hook that contains the needed logic for adding the imgui interface, and forcing flip model presentation.
    HRESULT __stdcall RenderManager::hkPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags) // Here's what happens when the swapchain is ready to be presented.
    {
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
                else { throw std::runtime_error("Failed to get swapchain back buffer."); } // Throw a standard C++ exception with an error message.
                oWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
                InitImGui();
                InitHook = true;
            }
            else { throw std::runtime_error("Failed to get swapchain device."); }
        }

        // TODO: Implement developer console and find a way to pipe SpdLog and standard logging to it.
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
        return rm_Instance.oPresent(pSwapChain, syncInterval, static_cast<UINT>(DXGI_SWAP_EFFECT_FLIP_DISCARD) | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);
    }

    // A hook that contains the needed logic for changing the Shadow, SSR, SSAO, and Post Processing resolution.
    HRESULT __stdcall RenderManager::hkCreateTexture2D(ID3D11Device* pDevice, D3D11_TEXTURE2D_DESC* pDesc, D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D)
    {
        // Update our rendering settings related settings before we modify anything.
        int ShadowRes = PlayerSettingsRm.RS.ShadowRes;
        int ScreenSpaceEffectsScale = PlayerSettingsRm.RS.ScreenSpaceEffectsDivider;
        int SSRScale = PlayerSettingsRm.RS.SSRScaleDivider;
        int iW = *InternalHorizontalRes;
        int iH = *InternalVerticalRes;

        // Checks to see if a render target is the current texture resource first before doing anything with it.
        if (pDesc->BindFlags == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) {
            // We are simply using this to update our current internal resolution for the rest of the logic.
            // Checks for the specific texture format for CopyDeferredColor_Hist, and checks to see if it has twelve mipmaps, if so, we got our suspect render target. 11 only works with resolutions below 1440p, while 12 only works with anything higher than 1080p.
            if (pDesc->Format == DXGI_FORMAT_R16G16B16A16_FLOAT && (pDesc->MipLevels == 11 || pDesc->MipLevels == 12)) { // Checks to see if it has twelve mipmaps, if so, we got our suspect render target. 11 only works with resolutions below 1440p, while 12 only works with anything higher than 1080p.
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
                default: { break; }
                }
            }

            // TODO: Need to decouple this from the resolution setting. Ideally, the post-processing should be changed based on the internal rendering resolution rather than the user decided resolution.
            // As we do plan on patching the in-game resolution option separately from the reported internal resolution, and the reported internal resolution should change as a result.
            // TODO: Figure out how to grab the yebismizuchi2 set of calls using the ID3DUserDefinedAnnotation system, so we can more accurately adjust these.
            // TODO: We need to find a way to get this so it can work with resolutions lower than 1920x1080 too, because it will still glitch out with resolutions lower than that.
            if (PlayerSettingsRm.RES.UseCustomRes) {
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
                        spdlog::info("Found Pause Menu Background Render Target (After 'mizuchi-copyback'). Changing resolution from {}x{} to {}x{}.", pDesc->Width, pDesc->Height, iW, iH);
                        resizeRt(pDesc, 1920, 1080, *InternalHorizontalRes, *InternalVerticalRes);
                        break;
                    }
                                                   // TODO: Fix this. It's not running.
                    case DXGI_FORMAT_R8G8B8A8_UNORM: { // The pause menu background effect that occurs after "yebismizuchi" tagged drawcalls.
                        spdlog::info("Found Pause Menu Background Render Target (After 'yebismizuchi'). Changing resolution from from {}x{} to {}x{}.", pDesc->Width, pDesc->Height, iW, iH);
                        resizeRt(pDesc, 1920, 1080, *InternalHorizontalRes, *InternalVerticalRes);
                        break;
                    }
                    default: { break; }
                    }
                }
            // }
        }
        if (pDesc->BindFlags == (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL)) {
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
    HRESULT __stdcall RenderManager::hkDrawIndexed(ID3D11DeviceContext *pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
    {
        bool preDraw = false;

        if ((IndexCount == 3 || IndexCount == 4) && StartIndexLocation == 0 && BaseVertexLocation == 0) { // NOTE: For some reason, some render targets have three indexes.
            preDraw = vpResize(pContext) && srResize(pContext);
            cbPatchYebis(pContext);
        }
        // Checks if both the viewports and scissor rects haven't been resized, and then return oDrawIndexed. I may need to find a better way of doing this to account for one not passing.
        if (!preDraw) {
            return rm_Instance.oDrawIndexed(pContext, IndexCount, StartIndexLocation, BaseVertexLocation);
        }
        return E_FAIL; // Default Return Statement, so Intellisense stops complaining.
    }

    // Another hook that contains the needed logic for running checks on Viewports and Scissor Rects.
    HRESULT __stdcall RenderManager::hkDraw(ID3D11DeviceContext *pContext, UINT VertexCount, UINT StartVertexLocation)
    {
        bool preDraw = false;
        if ((VertexCount == 3 || VertexCount == 4) && StartVertexLocation == 0) { // NOTE: For some reason, some render targets have three vertices. Some have four.
            preDraw = vpResize(pContext) && srResize(pContext);
        }
        if (!preDraw) {
            return rm_Instance.oDraw(pContext, VertexCount, StartVertexLocation);
        }
        return E_FAIL; // Default Return Statement, so Intellisense stops complaining.
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
                        // Binds the function we will be using for resizing viewports.
                        kiero::bind(74, reinterpret_cast<void**>(&oDraw), reinterpret_cast<void*>(this->hkDraw));
                        // Binds another function we will be using for resizing viewports.
                        kiero::bind(73, reinterpret_cast<void**>(&oDrawIndexed),reinterpret_cast<void*>(this->hkDrawIndexed));
                        break;
                    }
                    case PlayerSettings::DERQ2: {
                        // Binds the function we will be using to modify the render target size for shadows and other things.
                        kiero::bind(23, reinterpret_cast<void**>(&oCreateTexture2D), reinterpret_cast<void*>(this->hkCreateTexture2D));
                        // Binds the function we will be using for resizing viewports.
                        kiero::bind(74, reinterpret_cast<void**>(&oDraw), reinterpret_cast<void*>(this->hkDraw));
                        // Binds another function we will be using for resizing viewports.
                        kiero::bind(73, reinterpret_cast<void**>(&oDrawIndexed),reinterpret_cast<void*>(this->hkDrawIndexed));
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