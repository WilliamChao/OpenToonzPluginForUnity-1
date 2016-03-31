#include "pch.h"
#include "Foundation.h"
#include "GraphicsDevice.h"
using namespace utj;

typedef PixelFormat twPixelFormat;
#include "TextureWriter.h"

const char *utjModuleName = "TextureWriter" utjDLLExt;

twCLinkage twExport void* twMalloc(int size)
{
    return malloc(size);
}

twCLinkage twExport void twFree(void *ptr)
{
    free(ptr);
}


twCLinkage twExport bool twWriteTexture(
    void *dst_tex, int dst_width, int dst_height, twPixelFormat dst_fmt,
    const void *src, int src_num, twPixelFormat src_fmt)
{
    auto *dev = GetGraphicsDevice();
    if (!dst_tex || !src || !dev) { return false; }

    // convert data if data format is not match
    std::vector<char> tmp_pixels;
    if (dst_fmt != src_fmt) {
        tmp_pixels.resize(src_num * GetPixelSize(dst_fmt));
        ConvertPixelFormat(&tmp_pixels[0], dst_fmt, src, src_fmt, src_num);
        src = &tmp_pixels[0];
    }

    return dev->writeTexture(dst_tex, dst_width, dst_height, dst_fmt, src, src_num * GetPixelSize(dst_fmt));
}


twCLinkage twExport bool twReadTexture(
    void *dst, int dst_num, twPixelFormat dst_fmt,
    void *src_tex, int src_width, int src_height, twPixelFormat src_fmt)
{
    auto *dev = GetGraphicsDevice();
    if (!dst || !src_tex || !dev) { return false; }


    void *orig_dst = dst;
    std::vector<char> tmp_pixels;
    if (dst_fmt != src_fmt) {
        tmp_pixels.resize(dst_num * GetPixelSize(src_fmt));
        dst = &tmp_pixels[0];
    }

    if (!dev->readTexture(dst, dst_num * GetPixelSize(src_fmt), src_tex, src_width, src_height, src_fmt)) {
        return false;
    }

    // convert data if data format is not match
    if (!tmp_pixels.empty()) {
        ConvertPixelFormat(orig_dst, dst_fmt, &tmp_pixels[0], src_fmt, (int)tmp_pixels.size());
    }

    return true;
}



// deferred call impl

typedef std::function<void()> fcDeferredCall;
namespace {
    std::mutex g_deferred_calls_mutex;
    std::vector<fcDeferredCall> g_deferred_calls;
}

twCLinkage twExport void twGuardBegin()
{
    g_deferred_calls_mutex.lock();
}

twCLinkage twExport void twGuardEnd()
{
    g_deferred_calls_mutex.unlock();
}

twCLinkage twExport int twAddDeferredCall(const fcDeferredCall& dc, int id)
{
    if (id <= 0) {
        // search empty object and return its position if found
        for (int i = 1; i < (int)g_deferred_calls.size(); ++i) {
            if (!g_deferred_calls[i]) {
                g_deferred_calls[i] = dc;
                return i;
            }
        }

        // 0th is "null" object
        if (g_deferred_calls.empty()) { g_deferred_calls.emplace_back(fcDeferredCall()); }

        // allocate new one
        g_deferred_calls.emplace_back(dc);
        return (int)g_deferred_calls.size() - 1;
    }
    else if (id < (int)g_deferred_calls.size()) {
        g_deferred_calls[id] = dc;
        return id;
    }
    else {
        utjDebugLog("fcAddDeferredCall(): should not be here");
        return 0;
    }
}

twCLinkage twExport void twEraseDeferredCall(int id)
{
    if (id <= 0 || id >= (int)g_deferred_calls.size()) { return; }

    g_deferred_calls[id] = fcDeferredCall();
}

// **called from rendering thread**
twCLinkage twExport void twCallDeferredCall(int id)
{
    std::unique_lock<std::mutex> l(g_deferred_calls_mutex);
    if (id <= 0 || id >= (int)g_deferred_calls.size()) { return; }

    auto& dc = g_deferred_calls[id];
    if (dc) { dc(); }
}

void RenderEventCallback(int id)
{
    twCallDeferredCall(id);
}


twCLinkage twExport int twWriteTextureDeferred(
    void *dst_tex, int dst_width, int dst_height, twPixelFormat dst_fmt,
    const void *src, int src_num, twPixelFormat src_fmt, int id)
{
    return twAddDeferredCall([=]() {
        twWriteTexture(dst_tex, dst_width, dst_height, dst_fmt, src, src_num, src_fmt);
    }, id);
}

twCLinkage twExport int twReadTextureDeferred(
    void *dst, int dst_num, twPixelFormat dst_fmt,
    void *src_tex, int src_width, int src_height, twPixelFormat src_fmt, int id)
{
    return twAddDeferredCall([=]() {
        twReadTexture(dst, dst_num, dst_fmt, src_tex, src_width, src_height, src_fmt);
    }, id);
}
