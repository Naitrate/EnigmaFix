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

#ifndef ENIGMAFIX_RENDERMANAGER_H
#define ENIGMAFIX_RENDERMANAGER_H

// System Libraries
#include <d3d11.h>
#include <directxmath.h>
#include <dxgi.h>
// Variables
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef uintptr_t PTR;

namespace EnigmaFix {
    class RenderManager {
    public:
        static RenderManager& Get() {
            return rm_Instance;
        }
        // Render Hooking Functionality
        DWORD __stdcall InitD3D11Hook(LPVOID lpReserved);
    private:
        static RenderManager rm_Instance;
        RenderManager() {}  // Private constructor to prevent instantiation

        // Rendering Hook Definitions
        //// Hooks for ImGui overlay.
        typedef HRESULT(__stdcall* Present)(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
        typedef HRESULT(__stdcall* ResizeBuffers)(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
        //// CreateTexture2D Hook.
        typedef HRESULT(__stdcall* CreateTexture2D)(ID3D11Device *pDevice, D3D11_TEXTURE2D_DESC *pDesc, D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D);
        //// Draw Hook
        typedef HRESULT(__stdcall* Draw)(ID3D11DeviceContext *pContext, UINT VertexCount, UINT StartVertexLocation);
        //// Draw Indexed Hook
        typedef HRESULT(__stdcall* DrawIndexed)(ID3D11DeviceContext *pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
        //// RSSetViewports Hook
        typedef HRESULT(__stdcall* RSSetViewports)(ID3D11DeviceContext *pContext, UINT NumViewports, const D3D11_VIEWPORT* pViewports);
        //// RSSetScissorRects Hook
        typedef HRESULT(__stdcall* RSSetScissorRects)(ID3D11DeviceContext *pContext, UINT NumRects, const D3D11_RECT* pRects);
        // Render Hook Functions
        static HRESULT __stdcall hkResizeBuffers(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
        static HRESULT __stdcall hkPresent(IDXGISwapChain *pSwapChain, UINT SyncInterval, UINT Flags);
        static HRESULT __stdcall hkCreateTexture2D(ID3D11Device *pDevice, D3D11_TEXTURE2D_DESC *pDesc, D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D **ppTexture2D);
        static HRESULT __stdcall hkDrawIndexed(ID3D11DeviceContext *pContext, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation);
        static HRESULT __stdcall hkDraw(ID3D11DeviceContext *pContext, UINT VertexCount, UINT StartVertexLocation);
        static HRESULT __stdcall hkRSSetViewports(ID3D11DeviceContext *pContext, UINT NumViewports, D3D11_VIEWPORT *pViewports);
        static HRESULT __stdcall hkRSSetScissorRects(ID3D11DeviceContext *pContext, UINT NumRects, D3D11_RECT *pRects);

        // Variables
        //// D3D11Hook functions.
        Present oPresent;
        CreateTexture2D oCreateTexture2D;
        ResizeBuffers oResizeBuffers;
        Draw oDraw;
        DrawIndexed oDrawIndexed;
        RSSetViewports oRSSetViewports;
        RSSetScissorRects oRSSetScissorRects;
    };
}

#endif //ENIGMAFIX_RENDERMANAGER_H

