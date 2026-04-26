// ============================================================
// FILE:    engine/renderer/irender_backend.h
// MODULE:  Renderer
// PHASE:   1
// STATUS:  DONE
// PURPOSE: Pure virtual interface for GPU rendering.
//          All rendering operations go through here.
//          NEVER call rendering APIs directly outside this class.
// DEPENDS: platform/iplatform.h, core/math
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/platform/iplatform.h"
#include "engine/core/math/vec.h"
#include "engine/core/math/mat4.h"

namespace nova
{

    // ---- Resource Handles ----
    using ShaderHandle = uint64_t;
    using BufferHandle = uint64_t;
    using TextureHandle = uint64_t;
    using SamplerHandle = uint64_t;
    using RenderTargetHandle = uint64_t;
    using FramebufferHandle = uint64_t;

    constexpr ShaderHandle INVALID_SHADER = 0;
    constexpr BufferHandle INVALID_BUFFER = 0;
    constexpr TextureHandle INVALID_TEXTURE = 0;
    constexpr SamplerHandle INVALID_SAMPLER = 0;
    constexpr RenderTargetHandle INVALID_RENDERTARGET = 0;
    constexpr FramebufferHandle INVALID_FRAMEBUFFER = 0;

    // ---- Shader ----
    struct ShaderDesc
    {
        const char *vertexSource = nullptr;
        const char *fragmentSource = nullptr;
        const char *geometrySource = nullptr; // optional
        const char *computeSource = nullptr;  // optional
    };

    struct UniformInfo
    {
        const char *name = nullptr;
        int location = -1;
        int size = 0; // count for arrays
    };

    // ---- Buffer ----
    enum class BufferType
    {
        Vertex,
        Index,
        Uniform,
        Storage,
        Staging,
    };

    enum class BufferUsage
    {
        Static,  // set once, draw many
        Dynamic, // set often, draw many
        Stream,  // set every frame
    };

    enum class VertexType
    {
        Float,  // 1x float
        Float2, // 2x float
        Float3, // 3x float
        Float4, // 4x float
        Uint,   // 1x uint
        Uint2,  // 2x uint
        Uint4,  // 4x uint
        Byte4,  // 4x uint8_t (normalized)
        Byte4N, // 4x uint8_t (normalized)
    };

    struct VertexAttribute
    {
        const char *name = nullptr;
        VertexType type = VertexType::Float3;
        int count = 3;
        size_t offset = 0;
    };

    struct VertexLayout
    {
        const char *name = nullptr;
        int stride = 0;
        VertexAttribute attributes[16];
        int attributeCount = 0;
    };

    // Predefined layouts
    constexpr VertexLayout kLayoutBSP = {
        "BSP",
        44,
        {
            {"a_position", VertexType::Float3, 3, 0},
            {"a_uv", VertexType::Float2, 2, 12},
            {"a_lmUV", VertexType::Float2, 2, 20},
            {"a_normal", VertexType::Float3, 3, 28},
            {"a_color", VertexType::Byte4N, 4, 40},
        },
        5};

    struct BufferDesc
    {
        BufferType type = BufferType::Vertex;
        BufferUsage usage = BufferUsage::Static;
        size_t size = 0;
        const void *initialData = nullptr;
    };

    // ---- Texture ----
    enum class TextureType
    {
        Texture2D,
        Texture3D,
        TextureCube,
        Texture2DArray,
    };

    enum class TextureFormat
    {
        R8,
        RG8,
        RGB8,
        RGBA8,
        R16,
        RG16,
        RGB16,
        RGBA16,
        R32F,
        RG32F,
        RGB32F,
        RGBA32F,
        D24S8,
        D32F,
        BGRA8,
        SRGB8,
        SRGBA8,
    };

    enum class TextureFilter
    {
        Nearest,
        Linear,
        Trilinear,
    };

    enum class TextureWrap
    {
        Repeat,
        Clamp,
        Mirror,
    };

    struct TextureDesc
    {
        TextureType type = TextureType::Texture2D;
        int width = 1;
        int height = 1;
        int depth = 1;
        int mipLevels = 1;
        TextureFormat format = TextureFormat::RGBA8;
        TextureFilter minFilter = TextureFilter::Linear;
        TextureFilter magFilter = TextureFilter::Linear;
        TextureWrap wrapU = TextureWrap::Repeat;
        TextureWrap wrapV = TextureWrap::Repeat;
        TextureWrap wrapW = TextureWrap::Repeat;
        const void *initialData = nullptr;
    };

    // ---- Render Target ----
    enum class RenderTargetType
    {
        Color,
        ColorDepth,
        DepthOnly,
    };

    struct RenderTargetDesc
    {
        int width = 1;
        int height = 1;
        TextureFormat colorFormat = TextureFormat::RGBA8;
        bool hasDepth = true;
        TextureFormat depthFormat = TextureFormat::D24S8;
        int samples = 1; // MSAA
    };

    // ---- Draw State ----
    struct Viewport
    {
        int x = 0;
        int y = 0;
        int width = 1280;
        int height = 720;
        float minDepth = 0.f;
        float maxDepth = 1.f;
    };

    struct Scissor
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    struct BlendState
    {
        bool enabled = false;
        int srcColor = 0;
        int dstColor = 0;
        int srcAlpha = 0;
        int dstAlpha = 0;
    };

    struct DepthState
    {
        bool enabled = true;
        bool writeEnabled = true;
        int func = 0; // ComparisonFunc
    };

    struct RasterState
    {
        bool cullFace = true;
        bool frontFaceCCW = true;
        bool polygonOffset = false;
        float polygonOffsetFactor = 0.f;
        float polygonOffsetUnits = 0.f;
    };

    // ---- IRenderBackend ----
    class IRenderBackend
    {
    public:
        virtual ~IRenderBackend() = default;

        // ---- Initialization ----
        virtual bool initialize(IPlatform *platform) = 0;
        virtual void shutdown() = 0;
        virtual const char *getBackendName() const = 0;

        // ---- Swap Chain ----
        virtual bool createSwapChain(void *nativeWindow, const WindowDesc &desc) = 0;
        virtual void destroySwapChain() = 0;
        virtual void present() = 0;
        virtual void setSwapChainSize(int width, int height) = 0;
        virtual void *getCurrentFramebuffer() const = 0;

        // ---- Shaders ----
        virtual ShaderHandle createShader(const ShaderDesc &desc) = 0;
        virtual void destroyShader(ShaderHandle shader) = 0;
        virtual UniformInfo getUniform(ShaderHandle shader, const char *name) = 0;

        // ---- Buffers ----
        virtual BufferHandle createBuffer(const BufferDesc &desc) = 0;
        virtual void destroyBuffer(BufferHandle buffer) = 0;
        virtual void setBufferData(BufferHandle buffer, const void *data, size_t size) = 0;

        // ---- Textures ----
        virtual TextureHandle createTexture(const TextureDesc &desc) = 0;
        virtual void destroyTexture(TextureHandle texture) = 0;
        virtual void setTextureData(TextureHandle texture, int mipLevel, const void *data) = 0;
        virtual void generateMipmaps(TextureHandle texture) = 0;

        // ---- Samplers ----
        virtual SamplerHandle createSampler(const TextureDesc &desc) = 0;
        virtual void destroySampler(SamplerHandle sampler) = 0;

        // ---- Render Targets ----
        virtual RenderTargetHandle createRenderTarget(const RenderTargetDesc &desc) = 0;
        virtual void destroyRenderTarget(RenderTargetHandle rt) = 0;
        virtual TextureHandle getRenderTargetColorTexture(RenderTargetHandle rt) = 0;
        virtual TextureHandle getRenderTargetDepthTexture(RenderTargetHandle rt) = 0;

        // ---- Framebuffers (binding for rendering) ----
        virtual FramebufferHandle createFramebuffer(RenderTargetHandle color, RenderTargetHandle depth) = 0;
        virtual void destroyFramebuffer(FramebufferHandle fb) = 0;
        virtual void bindFramebuffer(FramebufferHandle fb) = 0;

        // ---- Rendering State ----
        virtual void setViewport(const Viewport &viewport) = 0;
        virtual void setScissor(const Scissor &scissor) = 0;
        virtual void setBlendState(const BlendState &blend) = 0;
        virtual void setDepthState(const DepthState &depth) = 0;
        virtual void setRasterState(const RasterState &raster) = 0;

        // ---- Binding ----
        virtual void bindShader(ShaderHandle shader) = 0;
        virtual void bindVertexBuffer(BufferHandle buffer, int slot, const VertexLayout *layout) = 0;
        virtual void bindIndexBuffer(BufferHandle buffer) = 0;
        virtual void bindTexture(TextureHandle texture, SamplerHandle sampler, int slot) = 0;
        virtual void bindUniformBuffer(BufferHandle buffer, int slot) = 0;

        // ---- Drawing ----
        virtual void clearColor(float r, float g, float b, float a) = 0;
        virtual void clearDepth(float depth) = 0;
        virtual void draw(int firstVertex, int vertexCount) = 0;
        // Second parameter is the first index within the bound index buffer.
        virtual void drawIndexed(int indexCount, int firstIndex) = 0;
        virtual void drawInstanced(int vertexCount, int instanceCount) = 0;
        virtual void drawIndexedInstanced(int indexCount, int instanceCount, int firstIndex) = 0;

        // ---- Queries ----
        virtual int getWidth() const = 0;
        virtual int getHeight() const = 0;
    };

    // Factory
    IRenderBackend *createRenderBackend();

} // namespace nova