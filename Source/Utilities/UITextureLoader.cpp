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

// Sadly can't do much about the layout here without making the compiler kill itself. Will have to take a look at it later.
#include <spdlog/spdlog.h>
#include "UITextureLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../ThirdParty/STB/stb_image.h"

#define _CRT_SECURE_NO_WARNINGS
#include "UITextureLoader.h"

#include "DXHelper.h"

//ID3D11Device* pDeviceTex = nullptr;

// Simple helper function to load an image into a DX11 texture with common settings
bool LoadTextureFromMemory(const void* data, size_t data_size, ID3D11ShaderResourceView** out_srv, ID3D11Device* pDeviceTex, int* out_width, int* out_height)
{
    // Load from memory into a raw RGBA buffer
    int image_width  = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load_from_memory(static_cast<const unsigned char*>(data), static_cast<int>(data_size), &image_width, &image_height, NULL, 4);

    if (image_data == NULL) { // Fixed: Added braces
        spdlog::error("Texture: Image data is null.");
        return false;
    }

    // Create texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = image_width;
    desc.Height = image_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    ID3D11Texture2D* pTexture = nullptr;
    D3D11_SUBRESOURCE_DATA subResource = {};
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;

    HRESULT hr = pDeviceTex->CreateTexture2D(&desc, &subResource, &pTexture);
    if (FAILED(hr)) {
        spdlog::error("Texture: Failed to create texture. HRESULT: 0x{:X}", hr);
        stbi_image_free(image_data);
        return false;
    }

    // Create texture view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;

    hr = pDeviceTex->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
    if (FAILED(hr)) {
        spdlog::error("Texture: Failed to create shader resource view. HRESULT: 0x{:X}", hr);
        pTexture->Release();
        stbi_image_free(image_data);
        return false;
    }

    *out_width = image_width;
    *out_height = image_height;

    pTexture->Release();
    stbi_image_free(image_data);

    return true;
}


// Open and read a file, then forward to LoadTextureFromMemory()
bool LoadTextureFromFile(const char* file_name, ID3D11ShaderResourceView** out_srv, ID3D11Device* pDeviceTex, int* out_width, int* out_height)
{
    FILE* f = nullptr;
    if (errno_t err = fopen_s(&f, file_name, "rb"); err != 0) {
        spdlog::error("Texture: Failed to open file {}", file_name);
        return false;
    }
    if (f == nullptr) {  // FIXED: Brackets added
        spdlog::error("Texture: File '{}' is null.", file_name);
        return false;
    }

    fseek(f, 0, SEEK_END);
    const auto file_size = static_cast<size_t>(ftell(f));
    if (file_size == -1) {
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);

    void* file_data = IM_ALLOC(file_size);
    fread(file_data, 1, file_size, f);
    fclose(f); // Ensure the file is closed after reading.

    const bool ret = LoadTextureFromMemory(file_data, file_size, out_srv, pDeviceTex, out_width, out_height);
    IM_FREE(file_data);
    return ret;
}