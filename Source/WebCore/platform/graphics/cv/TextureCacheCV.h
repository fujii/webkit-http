/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if HAVE(CORE_VIDEO)

#include "GraphicsTypes3D.h"
#include <wtf/Ref.h>
#include <wtf/RetainPtr.h>
#include <wtf/WeakPtr.h>

typedef struct __CVBuffer* CVImageBufferRef;
typedef CVImageBufferRef CVPixelBufferRef;
typedef CVImageBufferRef CVOpenGLTextureRef;
typedef CVImageBufferRef CVOpenGLESTextureRef;
typedef struct __CVOpenGLTextureCache *CVOpenGLTextureCacheRef;
typedef struct __CVOpenGLESTextureCache *CVOpenGLESTextureCacheRef;

namespace WebCore {

class GraphicsContext3D;

class TextureCacheCV {
public:
    static std::unique_ptr<TextureCacheCV> create(GraphicsContext3D&);

#if USE(OPENGL_ES)
    using TextureCacheType = CVOpenGLESTextureCacheRef;
    using TextureType = CVOpenGLESTextureRef;
#else
    using TextureCacheType = CVOpenGLTextureCacheRef;
    using TextureType = CVOpenGLTextureRef;
#endif

    TextureCacheCV(GraphicsContext3D&, RetainPtr<TextureCacheType>&&);

    RetainPtr<TextureType> textureFromImage(CVPixelBufferRef, GC3Denum outputTarget, GC3Dint level, GC3Denum internalFormat, GC3Denum format, GC3Denum type);
    GraphicsContext3D& context() { return m_context.get(); }

private:
    Ref<GraphicsContext3D> m_context;
    RetainPtr<TextureCacheType> m_cache;
    WeakPtrFactory<TextureCacheCV> m_weakPtrFactory;
};

}

#endif // HAVE(CORE_VIDEO)
