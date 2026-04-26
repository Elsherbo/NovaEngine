// ============================================================
// FILE:    engine/renderer/gl/gl_backend.cpp
// MODULE:  Renderer > OpenGL
// PHASE:   1
// STATUS:  FIXED
// PURPOSE: OpenGL 4.5 render backend using GLAD.
//
// FIX LOG:
//   1. destroyBuffer/Texture/Sampler: were passing &handle (local
//      uint64_t) as GLuint* — undefined behaviour.  Fixed to extract
//      GLuint by value first, then pass its address.
//   2. setBufferData: was always binding to GL_ARRAY_BUFFER regardless
//      of buffer type.  Fixed to use glNamedBufferSubData (DSA, GL 4.5).
//   3. createTexture: glTexImage2D base format must match the internal
//      format channel count.  Added proper base-format derivation.
// ============================================================

#include "engine/renderer/gl/gl_backend.h"
#include "engine/renderer/irender_backend.h"
#include <glad/glad.h>
#include <cstring>
#include <cstdio>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

namespace nova
{

    // ============ Helper Functions ============

    static GLenum glBufferType(BufferType type)
    {
        switch (type)
        {
        case BufferType::Vertex:  return GL_ARRAY_BUFFER;
        case BufferType::Index:   return GL_ELEMENT_ARRAY_BUFFER;
        case BufferType::Uniform: return GL_UNIFORM_BUFFER;
        case BufferType::Storage: return GL_SHADER_STORAGE_BUFFER;
        default:                  return GL_ARRAY_BUFFER;
        }
    }

    static GLenum glBufferUsage(BufferUsage usage)
    {
        switch (usage)
        {
        case BufferUsage::Static:  return GL_STATIC_DRAW;
        case BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
        case BufferUsage::Stream:  return GL_STREAM_DRAW;
        default:                   return GL_STATIC_DRAW;
        }
    }

    static GLenum glTextureType(TextureType type)
    {
        switch (type)
        {
        case TextureType::Texture2D:      return GL_TEXTURE_2D;
        case TextureType::Texture3D:      return GL_TEXTURE_3D;
        case TextureType::TextureCube:    return GL_TEXTURE_CUBE_MAP;
        case TextureType::Texture2DArray: return GL_TEXTURE_2D_ARRAY;
        default:                          return GL_TEXTURE_2D;
        }
    }

    // Returns internal format, base format, and pixel type for a TextureFormat.
    static void glTextureFormat(TextureFormat format,
                                GLenum &internal, GLenum &base, GLenum &type)
    {
        switch (format)
        {
        case TextureFormat::R8:
            internal = GL_R8;      base = GL_RED;             type = GL_UNSIGNED_BYTE;       break;
        case TextureFormat::RG8:
            internal = GL_RG8;     base = GL_RG;              type = GL_UNSIGNED_BYTE;       break;
        case TextureFormat::RGB8:
            internal = GL_RGB8;    base = GL_RGB;             type = GL_UNSIGNED_BYTE;       break;
        case TextureFormat::RGBA8:
            internal = GL_RGBA8;   base = GL_RGBA;            type = GL_UNSIGNED_BYTE;       break;
        case TextureFormat::R16:
            internal = GL_R16;     base = GL_RED;             type = GL_UNSIGNED_SHORT;      break;
        case TextureFormat::RG16:
            internal = GL_RG16;    base = GL_RG;              type = GL_UNSIGNED_SHORT;      break;
        case TextureFormat::RGB16:
            internal = GL_RGB16;   base = GL_RGB;             type = GL_UNSIGNED_SHORT;      break;
        case TextureFormat::RGBA16:
            internal = GL_RGBA16;  base = GL_RGBA;            type = GL_UNSIGNED_SHORT;      break;
        case TextureFormat::R32F:
            internal = GL_R32F;    base = GL_RED;             type = GL_FLOAT;               break;
        case TextureFormat::RG32F:
            internal = GL_RG32F;   base = GL_RG;              type = GL_FLOAT;               break;
        case TextureFormat::RGB32F:
            internal = GL_RGB32F;  base = GL_RGB;             type = GL_FLOAT;               break;
        case TextureFormat::RGBA32F:
            internal = GL_RGBA32F; base = GL_RGBA;            type = GL_FLOAT;               break;
        case TextureFormat::D24S8:
            internal = GL_DEPTH24_STENCIL8; base = GL_DEPTH_STENCIL;
            type = GL_UNSIGNED_INT_24_8;    break;
        case TextureFormat::D32F:
            internal = GL_DEPTH_COMPONENT32F; base = GL_DEPTH_COMPONENT;
            type = GL_FLOAT;                  break;
        case TextureFormat::BGRA8:
            internal = GL_RGBA8;   base = GL_BGRA;            type = GL_UNSIGNED_BYTE;       break;
        case TextureFormat::SRGB8:
            internal = GL_SRGB8;   base = GL_RGB;             type = GL_UNSIGNED_BYTE;       break;
        case TextureFormat::SRGBA8:
            internal = GL_SRGB8_ALPHA8; base = GL_RGBA;       type = GL_UNSIGNED_BYTE;       break;
        default:
            internal = GL_RGBA8;   base = GL_RGBA;            type = GL_UNSIGNED_BYTE;       break;
        }
    }

    static GLenum glFilter(TextureFilter filter, bool mipmap = false)
    {
        switch (filter)
        {
        case TextureFilter::Nearest:
            return mipmap ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
        case TextureFilter::Linear:
            return mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
        case TextureFilter::Trilinear:
            return GL_LINEAR_MIPMAP_LINEAR;
        default:
            return GL_LINEAR;
        }
    }

    static GLenum glWrap(TextureWrap wrap)
    {
        switch (wrap)
        {
        case TextureWrap::Repeat: return GL_REPEAT;
        case TextureWrap::Clamp:  return GL_CLAMP_TO_EDGE;
        case TextureWrap::Mirror: return GL_MIRRORED_REPEAT;
        default:                  return GL_REPEAT;
        }
    }

    static GLenum glCompareFunc(int func)
    {
        switch (func)
        {
        case 0: return GL_ALWAYS;
        case 1: return GL_NEVER;
        case 2: return GL_LESS;
        case 3: return GL_LEQUAL;
        case 4: return GL_GREATER;
        case 5: return GL_GEQUAL;
        case 6: return GL_EQUAL;
        case 7: return GL_NOTEQUAL;
        default: return GL_LESS;
        }
    }

    // ============ GLBackend Implementation ============

    GLBackend::GLBackend() = default;
    GLBackend::~GLBackend() { shutdown(); }

bool GLBackend::initialize(IPlatform *platform)
    {
        m_platform = platform;
        return true;
    }

    void GLBackend::shutdown()
    {
        if (m_vao)
        {
            glDeleteVertexArrays(1, &m_vao);
            m_vao = 0;
        }
        m_platform = nullptr;
        m_rtTextures.clear();

        return;
    }

    bool GLBackend::createSwapChain(void *nativeWindow, const WindowDesc &desc)
    {
        m_window = nativeWindow;
        m_width = desc.width;
        m_height = desc.height;

        m_sdlGlContext = SDL_GL_CreateContext(static_cast<SDL_Window *>(nativeWindow));
        if (!m_sdlGlContext)
        {
            fprintf(stderr, "GLBackend: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            return false;
        }

        SDL_GL_MakeCurrent(static_cast<SDL_Window *>(nativeWindow), 
            reinterpret_cast<SDL_GLContext>(m_sdlGlContext));

        // Load GL function pointers (requires active context)
        if (!gladLoadGL())
        {
            fprintf(stderr, "GLBackend: gladLoadGL failed\n");
            return false;
        }

        // Global VAO — required by GL 4.5 Core Profile
        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        // Keep culling disabled for now: BSP faces can contain mixed winding
        // after coordinate-space conversion and fan triangulation. Forcing
        // either CW or CCW culling drops valid surfaces and causes the
        // "inside-out / sliced" look.
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glViewport(0, 0, m_width, m_height);
        return true;
    }

    void GLBackend::destroySwapChain() {}

    void GLBackend::present()
    {
        SDL_GL_SwapWindow(static_cast<SDL_Window *>(m_window));
    }

    void GLBackend::setSwapChainSize(int width, int height)
    {
        m_width = width;
        m_height = height;
        glViewport(0, 0, width, height);
    }

    void *GLBackend::getCurrentFramebuffer() const { return nullptr; }

    ShaderHandle GLBackend::createShader(const ShaderDesc &desc)
    {
        GLuint program = glCreateProgram();
        bool linked = true;
        int success = 0;
        char infoLog[512];

        auto compileStage = [&](GLenum stage, const char *src) -> bool
        {
            if (!src) return true;
            GLuint sh = glCreateShader(stage);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            glGetShaderiv(sh, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                glGetShaderInfoLog(sh, sizeof(infoLog), nullptr, infoLog);
                printf("Shader compile error (stage %u): %s\n", stage, infoLog);
                glDeleteShader(sh);
                return false;
            }
            glAttachShader(program, sh);
            glDeleteShader(sh);
            return true;
        };

        linked &= compileStage(GL_VERTEX_SHADER,   desc.vertexSource);
        linked &= compileStage(GL_FRAGMENT_SHADER, desc.fragmentSource);
        linked &= compileStage(GL_GEOMETRY_SHADER, desc.geometrySource);

        if (linked)
        {
            glLinkProgram(program);
            glGetProgramiv(program, GL_LINK_STATUS, &success);
            if (!success)
            {
                glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
                printf("Shader link error: %s\n", infoLog);
                linked = false;
            }
        }

        if (!linked)
        {
            glDeleteProgram(program);
            return INVALID_SHADER;
        }
        return static_cast<ShaderHandle>(program);
    }

    void GLBackend::destroyShader(ShaderHandle shader)
    {
        if (shader == INVALID_SHADER) return;
        glDeleteProgram(static_cast<GLuint>(shader));
    }

    UniformInfo GLBackend::getUniform(ShaderHandle shader, const char *name)
    {
        UniformInfo info{};
        if (shader == INVALID_SHADER || !name) return info;
        info.location = glGetUniformLocation(static_cast<GLuint>(shader), name);
        return info;
    }

    BufferHandle GLBackend::createBuffer(const BufferDesc &desc)
    {
        GLuint id;
        glGenBuffers(1, &id);
        GLenum target = glBufferType(desc.type);
        glBindBuffer(target, id);
        glBufferData(target, (GLsizeiptr)desc.size, desc.initialData,
                     glBufferUsage(desc.usage));
        glBindBuffer(target, 0);
        return static_cast<BufferHandle>(id);
    }

    void GLBackend::destroyBuffer(BufferHandle buffer)
    {
        if (buffer == INVALID_BUFFER) return;
        // FIX 1: extract GLuint by value — do NOT cast &buffer
        GLuint id = static_cast<GLuint>(buffer);
        glDeleteBuffers(1, &id);
    }

    // FIX 2: use DSA glNamedBufferSubData so we don't need to know the target
    void GLBackend::setBufferData(BufferHandle buffer, const void *data, size_t size)
    {
        if (buffer == INVALID_BUFFER || !data) return;
        glNamedBufferSubData(static_cast<GLuint>(buffer), 0, (GLsizeiptr)size, data);
    }

    TextureHandle GLBackend::createTexture(const TextureDesc &desc)
    {
        GLuint id;
        glGenTextures(1, &id);
        GLenum glType = glTextureType(desc.type);
        glBindTexture(glType, id);

        GLenum internalFmt, baseFmt, pixelType;
        glTextureFormat(desc.format, internalFmt, baseFmt, pixelType);

        if (glType == GL_TEXTURE_CUBE_MAP)
        {
            for (int face = 0; face < 6; ++face)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
                             internalFmt, desc.width, desc.height,
                             0, baseFmt, pixelType, desc.initialData);
        }
        else if (glType == GL_TEXTURE_2D)
        {
            // FIX 3: use derived baseFmt instead of hardcoded GL_RGBA
            glTexImage2D(GL_TEXTURE_2D, 0, internalFmt,
                         desc.width, desc.height,
                         0, baseFmt, pixelType, desc.initialData);
        }

        glTexParameteri(glType, GL_TEXTURE_MIN_FILTER, glFilter(desc.minFilter, desc.mipLevels > 1));
        glTexParameteri(glType, GL_TEXTURE_MAG_FILTER, glFilter(desc.magFilter, false));
        glTexParameteri(glType, GL_TEXTURE_WRAP_S, glWrap(desc.wrapU));
        glTexParameteri(glType, GL_TEXTURE_WRAP_T, glWrap(desc.wrapV));
        if (glType == GL_TEXTURE_3D)
            glTexParameteri(glType, GL_TEXTURE_WRAP_R, glWrap(desc.wrapW));

        if (desc.mipLevels > 1)
            glGenerateMipmap(glType);

        glBindTexture(glType, 0);

        // Store texture info for setTextureData
        TextureObject obj{};
        obj.texture = id;
        obj.type = desc.type;
        obj.width = desc.width;
        obj.height = desc.height;
        obj.depth = desc.depth;
        obj.mipLevels = desc.mipLevels;
        obj.format = desc.format;
        m_textures[static_cast<uint64_t>(id)] = obj;

        return static_cast<TextureHandle>(id);
    }

    void GLBackend::destroyTexture(TextureHandle texture)
    {
        if (texture == INVALID_TEXTURE) return;
        uint64_t key = static_cast<uint64_t>(texture);
        m_textures.erase(key);

        GLuint id = static_cast<GLuint>(texture);
        glDeleteTextures(1, &id);
    }

    void GLBackend::setTextureData(TextureHandle texture, int mipLevel, const void *data)
    {
        if (texture == INVALID_TEXTURE || !data) return;

        uint64_t key = static_cast<uint64_t>(texture);
        auto it = m_textures.find(key);
        if (it == m_textures.end()) return;

        const TextureObject &obj = it->second;
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));

        GLenum internalFmt, baseFmt, pixelType;
        glTextureFormat(obj.format, internalFmt, baseFmt, pixelType);

        GLint w = 0, h = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, mipLevel, GL_TEXTURE_WIDTH, &w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, mipLevel, GL_TEXTURE_HEIGHT, &h);
        if (w > 0 && h > 0)
            glTexSubImage2D(GL_TEXTURE_2D, mipLevel, 0, 0, w, h,
                            baseFmt, pixelType, data);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void GLBackend::generateMipmaps(TextureHandle texture)
    {
        if (texture == INVALID_TEXTURE) return;
        GLuint id = static_cast<GLuint>(texture);
        // DSA version: no explicit bind needed
        glGenerateTextureMipmap(id);
    }

    SamplerHandle GLBackend::createSampler(const TextureDesc &desc)
    {
        GLuint id;
        glGenSamplers(1, &id);
        glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, glFilter(desc.minFilter, desc.mipLevels > 1));
        glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, glFilter(desc.magFilter, false));
        glSamplerParameteri(id, GL_TEXTURE_WRAP_S, glWrap(desc.wrapU));
        glSamplerParameteri(id, GL_TEXTURE_WRAP_T, glWrap(desc.wrapV));
        glSamplerParameteri(id, GL_TEXTURE_WRAP_R, glWrap(desc.wrapW));
        return static_cast<SamplerHandle>(id);
    }

    void GLBackend::destroySampler(SamplerHandle sampler)
    {
        if (sampler == INVALID_SAMPLER) return;
        // FIX 1: extract by value
        GLuint id = static_cast<GLuint>(sampler);
        glDeleteSamplers(1, &id);
    }

    RenderTargetHandle GLBackend::createRenderTarget(const RenderTargetDesc &desc)
    {
        GLuint fbo;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        GLenum internalFmt, baseFmt, pixelType;

        // Color attachment
        GLuint colorTex = 0;
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTextureFormat(desc.colorFormat, internalFmt, baseFmt, pixelType);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, desc.width, desc.height,
                     0, baseFmt, pixelType, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Depth attachment
        GLuint depthTex = 0;
        if (desc.hasDepth)
        {
            glGenTextures(1, &depthTex);
            glBindTexture(GL_TEXTURE_2D, depthTex);
            glTextureFormat(desc.depthFormat, internalFmt, baseFmt, pixelType);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, desc.width, desc.height,
                         0, baseFmt, pixelType, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                   GL_TEXTURE_2D, depthTex, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        m_rtTextures[fbo] = {colorTex, depthTex};

        return static_cast<RenderTargetHandle>(fbo);
    }

    void GLBackend::destroyRenderTarget(RenderTargetHandle rt)
    {
        if (rt == INVALID_RENDERTARGET) return;
        GLuint fbo = static_cast<GLuint>(rt);
        auto it = m_rtTextures.find(fbo);
        if (it != m_rtTextures.end())
        {
            if (it->second.colorTex)
                glDeleteTextures(1, &it->second.colorTex);
            if (it->second.depthTex)
                glDeleteTextures(1, &it->second.depthTex);
            m_rtTextures.erase(it);
        }
        glDeleteFramebuffers(1, &fbo);
    }

    TextureHandle GLBackend::getRenderTargetColorTexture(RenderTargetHandle rt)
    {
        GLuint fbo = static_cast<GLuint>(rt);
        auto it = m_rtTextures.find(fbo);
        if (it != m_rtTextures.end())
            return static_cast<TextureHandle>(it->second.colorTex);
        return INVALID_TEXTURE;
    }

    TextureHandle GLBackend::getRenderTargetDepthTexture(RenderTargetHandle rt)
    {
        GLuint fbo = static_cast<GLuint>(rt);
        auto it = m_rtTextures.find(fbo);
        if (it != m_rtTextures.end())
            return static_cast<TextureHandle>(it->second.depthTex);
        return INVALID_TEXTURE;
    }

    FramebufferHandle GLBackend::createFramebuffer(RenderTargetHandle color, RenderTargetHandle depth)
    {
        if (color == INVALID_RENDERTARGET && depth == INVALID_RENDERTARGET)
            return INVALID_FRAMEBUFFER;

        GLuint fbo;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        if (color != INVALID_RENDERTARGET)
        {
            GLuint colorTex = static_cast<GLuint>(getRenderTargetColorTexture(color));
            if (colorTex)
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, colorTex, 0);
        }

        if (depth != INVALID_RENDERTARGET)
        {
            GLuint depthTex = static_cast<GLuint>(getRenderTargetDepthTexture(depth));
            if (depthTex)
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                       GL_TEXTURE_2D, depthTex, 0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return static_cast<FramebufferHandle>(fbo);
    }

    void GLBackend::destroyFramebuffer(FramebufferHandle fb)
    {
        if (fb == INVALID_FRAMEBUFFER) return;
        GLuint fbo = static_cast<GLuint>(fb);
        glDeleteFramebuffers(1, &fbo);
    }

    void GLBackend::bindFramebuffer(FramebufferHandle fb)
    {
        if (fb == INVALID_FRAMEBUFFER)
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        else
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fb));
        m_boundFramebuffer = fb;
    }

    void GLBackend::setViewport(const Viewport &vp)
    {
        glViewport(vp.x, vp.y, vp.width, vp.height);
    }

    void GLBackend::setScissor(const Scissor &sc)
    {
        if (sc.width == 0 || sc.height == 0)
            glDisable(GL_SCISSOR_TEST);
        else
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(sc.x, sc.y, sc.width, sc.height);
        }
    }

    void GLBackend::setBlendState(const BlendState &blend)
    {
        if (!blend.enabled)
            glDisable(GL_BLEND);
        else
        {
            glEnable(GL_BLEND);
            glBlendFunc(blend.srcColor ? GL_SRC_ALPHA : GL_ONE,
                        blend.dstColor ? GL_ONE_MINUS_SRC_ALPHA : GL_ZERO);
        }
    }

    void GLBackend::setDepthState(const DepthState &depth)
    {
        if (!depth.enabled)
            glDisable(GL_DEPTH_TEST);
        else
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(glCompareFunc(depth.func));
            glDepthMask(depth.writeEnabled ? GL_TRUE : GL_FALSE);
        }
    }

    void GLBackend::setRasterState(const RasterState &raster)
    {
        if (!raster.cullFace)
            glDisable(GL_CULL_FACE);
        else
            glEnable(GL_CULL_FACE);
        glFrontFace(raster.frontFaceCCW ? GL_CCW : GL_CW);

        if (raster.polygonOffset)
        {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(raster.polygonOffsetFactor, raster.polygonOffsetUnits);
        }
        else
        {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }

    void GLBackend::bindShader(ShaderHandle shader)
    {
        m_boundShader = shader;
        glUseProgram(static_cast<GLuint>(shader));
    }

void GLBackend::bindVertexBuffer(BufferHandle buffer, int slot, const VertexLayout *layout)
{
    // Default to BSP layout if null
    if (!layout) layout = &kLayoutBSP;

    (void)slot;
    if (buffer == INVALID_BUFFER) return;

    GLuint vbo = static_cast<GLuint>(buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    for (int i = 0; i < layout->attributeCount; ++i)
    {
        const VertexAttribute &attr = layout->attributes[i];
        GLenum glType = GL_FLOAT;
        int count = attr.count;
        bool normalized = GL_FALSE;

        switch (attr.type)
        {
        case VertexType::Float:   glType = GL_FLOAT;         break;
        case VertexType::Float2:  glType = GL_FLOAT;         break;
        case VertexType::Float3: glType = GL_FLOAT;         break;
        case VertexType::Float4: glType = GL_FLOAT;         break;
        case VertexType::Uint:   glType = GL_UNSIGNED_INT;  break;
        case VertexType::Uint2:   glType = GL_UNSIGNED_INT;  break;
        case VertexType::Uint4:   glType = GL_UNSIGNED_INT;  break;
        case VertexType::Byte4:    glType = GL_UNSIGNED_BYTE; break;
        case VertexType::Byte4N:   glType = GL_UNSIGNED_BYTE; normalized = GL_TRUE; break;
        default: continue;
        }

        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, count, glType, normalized,
                          static_cast<GLsizei>(layout->stride),
                          reinterpret_cast<const void *>(attr.offset));
    }
}

    void GLBackend::bindIndexBuffer(BufferHandle buffer)
    {
        m_boundIndexBuffer = buffer;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(buffer));
    }

    void GLBackend::bindTexture(TextureHandle texture, SamplerHandle sampler, int slot)
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        if (sampler != INVALID_SAMPLER)
            glBindSampler(slot, static_cast<GLuint>(sampler));
    }

    void GLBackend::bindUniformBuffer(BufferHandle buffer, int slot)
    {
        glBindBufferBase(GL_UNIFORM_BUFFER, slot, static_cast<GLuint>(buffer));
    }

    void GLBackend::clearColor(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void GLBackend::clearDepth(float depth)
    {
        glClearDepth(depth);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    void GLBackend::draw(int firstVertex, int vertexCount)
    {
        glDrawArrays(GL_TRIANGLES, firstVertex, vertexCount);
    }

    void GLBackend::drawIndexed(int indexCount, int firstIndex)
    {
        const intptr_t offsetBytes = (intptr_t)firstIndex * (intptr_t)sizeof(uint32_t);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, (const void*)offsetBytes);
    }

    void GLBackend::drawInstanced(int vertexCount, int instanceCount)
    {
        glDrawArraysInstanced(GL_TRIANGLES, 0, vertexCount, instanceCount);
    }

    void GLBackend::drawIndexedInstanced(int indexCount, int instanceCount, int firstIndex)
    {
        const intptr_t offsetBytes = (intptr_t)firstIndex * (intptr_t)sizeof(uint32_t);
        glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT,
                                (const void*)offsetBytes, instanceCount);
    }

    IRenderBackend *createRenderBackend()
    {
        return new GLBackend();
    }

} // namespace nova
