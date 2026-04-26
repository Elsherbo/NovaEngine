// ============================================================
// FILE:    engine/renderer/gl/gl_backend.h
// MODULE:  Renderer > OpenGL
// PHASE:   1
// STATUS:  DONE
// PURPOSE: OpenGL 4.5 render backend implementation.
//          Uses MinGW OpenGL headers + SDL2 for context.
// DEPENDS: renderer/irender_backend.h, platform/iplatform.h
// ============================================================
#pragma once

#include "engine/renderer/irender_backend.h"
#include <unordered_map>

namespace nova
{

class GLBackend : public IRenderBackend
{
public:
    GLBackend();
    ~GLBackend() override;

    bool initialize(IPlatform *platform) override;
    void shutdown() override;
    const char *getBackendName() const override { return "OpenGL 4.5"; }

    bool createSwapChain(void *nativeWindow, const WindowDesc &desc) override;
    void destroySwapChain() override;
    void present() override;
    void setSwapChainSize(int width, int height) override;
    void *getCurrentFramebuffer() const override;

    ShaderHandle createShader(const ShaderDesc &desc) override;
    void destroyShader(ShaderHandle shader) override;
    UniformInfo getUniform(ShaderHandle shader, const char *name) override;

    BufferHandle createBuffer(const BufferDesc &desc) override;
    void destroyBuffer(BufferHandle buffer) override;
    void setBufferData(BufferHandle buffer, const void *data, size_t size) override;

    TextureHandle createTexture(const TextureDesc &desc) override;
    void destroyTexture(TextureHandle texture) override;
    void setTextureData(TextureHandle texture, int mipLevel, const void *data) override;
    void generateMipmaps(TextureHandle texture) override;

    SamplerHandle createSampler(const TextureDesc &desc) override;
    void destroySampler(SamplerHandle sampler) override;

    RenderTargetHandle createRenderTarget(const RenderTargetDesc &desc) override;
    void destroyRenderTarget(RenderTargetHandle rt) override;
    TextureHandle getRenderTargetColorTexture(RenderTargetHandle rt) override;
    TextureHandle getRenderTargetDepthTexture(RenderTargetHandle rt) override;

    FramebufferHandle createFramebuffer(RenderTargetHandle color, RenderTargetHandle depth) override;
    void destroyFramebuffer(FramebufferHandle fb) override;
    void bindFramebuffer(FramebufferHandle fb) override;

    void setViewport(const Viewport &viewport) override;
    void setScissor(const Scissor &scissor) override;
    void setBlendState(const BlendState &blend) override;
    void setDepthState(const DepthState &depth) override;
    void setRasterState(const RasterState &raster) override;

    void bindShader(ShaderHandle shader) override;
    void bindVertexBuffer(BufferHandle buffer, int slot, const VertexLayout *layout) override;
    void bindIndexBuffer(BufferHandle buffer) override;
    void bindTexture(TextureHandle texture, SamplerHandle sampler, int slot) override;
    void bindUniformBuffer(BufferHandle buffer, int slot) override;

    void clearColor(float r, float g, float b, float a) override;
    void clearDepth(float depth) override;
    void draw(int firstVertex, int vertexCount) override;
    void drawIndexed(int indexCount, int baseVertex) override;
    void drawInstanced(int vertexCount, int instanceCount) override;
    void drawIndexedInstanced(int indexCount, int instanceCount, int baseVertex) override;

    int getWidth() const override { return m_width; }
    int getHeight() const override { return m_height; }

private:
    struct ShaderObject
    {
        uint32_t program = 0;
        int32_t *uniformLocations = nullptr;
        int uniformCount = 0;
    };

    struct BufferObject
    {
        uint32_t vbo = 0;
        BufferType type;
        BufferUsage usage;
        size_t size = 0;
    };

    struct TextureObject
    {
        uint32_t texture = 0;
        TextureType type;
        int width = 0;
        int height = 0;
        int depth = 0;
        int mipLevels = 0;
        TextureFormat format;
    };

    struct SamplerObject
    {
        uint32_t sampler = 0;
        TextureFilter minFilter;
        TextureFilter magFilter;
        TextureWrap wrapU, wrapV, wrapW;
    };

    struct RenderTargetObject
    {
        uint32_t fbo = 0;
        uint32_t colorTex = 0;
        uint32_t depthTex = 0;
        uint32_t colorRbo = 0;
        uint32_t depthRbo = 0;
        int width = 0;
        int height = 0;
    };

    IPlatform *m_platform = nullptr;
    void *m_sdlGlContext = nullptr;
    void *m_window = nullptr;
    int m_width = 1280;
    int m_height = 720;

    // Global VAO — required in GL 4.5 Core profile
    uint32_t m_vao = 0;

    // Current state
    ShaderHandle m_boundShader = INVALID_SHADER;
    BufferHandle m_boundVao = INVALID_BUFFER;
    BufferHandle m_boundIndexBuffer = INVALID_BUFFER;
    FramebufferHandle m_boundFramebuffer = INVALID_FRAMEBUFFER;

    // RenderTarget texture tracking (color and depth GL texture IDs)
    struct RTTextures
    {
        uint32_t colorTex = 0;
        uint32_t depthTex = 0;
    };
    // Maps TextureHandle -> texture object
    std::unordered_map<uint64_t, TextureObject> m_textures;
    // Maps RenderTargetHandle (==fbo id) -> textures
    std::unordered_map<uint32_t, RTTextures> m_rtTextures;

    // Next handle value
    uint64_t m_nextHandle = 1;
};

IRenderBackend *createRenderBackend();

} // namespace nova