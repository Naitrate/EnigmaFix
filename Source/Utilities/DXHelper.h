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

#ifndef DXHELPER_H
#define DXHELPER_H

// System Libraries
#include <exception>
#include <cstdio>
#include <winerror.h>

// A COM Pointer Exception handler when something goes awry with any of the D3D11 Hooks.
namespace DX {
    // Helper class for COM exceptions
    class com_exception : public std::exception {
    public:
        com_exception(HRESULT hr) : result(hr) {
        }
        const char* what() const noexcept override {
            static char s_str[64] = {};
            sprintf_s(s_str, "Failure with HRESULT of %08X",
                      static_cast<unsigned int>(result));
            return s_str;
        }
    private:
        HRESULT result;
    };
    // Helper utility converts D3D API failures into exceptions.
    inline void ThrowIfFailed(HRESULT hr) {
        if (FAILED(hr)) {
            throw com_exception(hr);
        }
    }

    template<typename T>
    struct Release
    {
        void operator()(T* obj) const
        {
            if (obj)
            {
                obj->Release();
            }
        }
    };
}
#endif //DXHELPER_H
