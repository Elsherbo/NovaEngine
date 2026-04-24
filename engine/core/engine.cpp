// ============================================================
// FILE:    engine/core/engine.cpp
// MODULE:  Core > Engine
// PHASE:   1
// PURPOSE: Main engine loop: platform init, window, render loop,
//          BSP loading, camera update, present.
// DEPENDS: all engine modules
//
// FIX LOG:
//   1. [NEW] BSP load sequence fixed: uploadToGPU() now owns the
//      atlas-packing + buildGeometry() pipeline internally.
//      The old explicit bsp->buildGeometry() call was removed —
//      calling it before uploadToGPU() meant lightmap atlas UVs
//      were not yet computed, leaving all lmUVs at 0.
//   2. [NEW] SDL_SetRelativeMouseMode(SDL_TRUE) added after window
//      creation. Without this, SDL reports absolute mouse positions
//      and mouseDeltaX/Y are always 0 (camera never turns).
// ============================================================

#include "engine/core/log.h"
#include "engine/core/camera.h"
#include "engine/platform/iplatform.h"
#include "engine/renderer/irender_backend.h"
#include "engine/renderer/bsp/bsp.h"
#include "engine/renderer/gl/gl_backend.h"
#include "engine/physics/iphysics_world.h"
#include "engine/physics/aabb_physics.h"
#include "vendor/GLAD/include/glad/glad.h"

// Let SDL3 handle the entry point - it will call our main()
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_scancode.h>
#include <GL/gl.h>

#include <cstdio>
#include <cstdlib>

namespace nova
{

// =====================================================================
//  GLSL Sources
// =====================================================================
static const char *g_vsSource = R"(
#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec2 aLmCoord;
layout(location = 3) in vec3 aNormal;
layout(location = 4) in vec4 aColor;      // normalized ubyte

layout(std140, binding = 0) uniform PerFrame
{
    mat4 uViewProj;
    vec3 uCamPos;
    float pad0;
};

out vec2 vUv;
out vec2 vLmUv;
out vec3 vNormal;
out vec3 vWorldPos;
out vec4 vColor;

void main()
{
    vUv       = aTexCoord;
    vLmUv     = aLmCoord;
    vNormal   = aNormal;
    vWorldPos = aPosition;
    vColor    = aColor;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
)";

static const char *g_fsSource = R"(
#version 450 core

in vec2 vUv;
in vec2 vLmUv;
in vec3 vNormal;
in vec3 vWorldPos;
in vec4 vColor;

layout(binding = 0) uniform sampler2D uLightmap;

out vec4 fragColor;

void main()
{
    vec3 baseColor = vColor.rgb;
    vec3 lm = texture(uLightmap, vLmUv).rgb;
    if (dot(lm, lm) < 0.001) lm = vec3(1.0); // white fallback for no-lightmap faces
    float diff = max(dot(normalize(vNormal), normalize(vec3(0.5, 1.0, 0.3))), 0.3);
    fragColor = vec4(baseColor * lm * diff, 1.0);
}
)";

// =====================================================================
//  Engine
// =====================================================================
class Engine
{
public:
    bool init(const char *bspPath);
    void shutdown();
    int  run();

private:
    void update(float dt);
    void render();
    void buildDebugScene();

    IPlatform    *m_platform   = nullptr;
    IRenderBackend *m_renderer = nullptr;
    Camera       *m_camera     = nullptr;
    BSPMap       *m_bsp        = nullptr;
    IPhysicsWorld *m_physics  = nullptr;

    struct PerFrameUBO
    {
        float viewProj[16];
        float camPos[3];
        float pad0;
    } m_ubo{};

    BufferHandle   m_uboBuffer     = INVALID_BUFFER;
    ShaderHandle   m_shader        = INVALID_SHADER;
    BufferHandle   m_debugVertex   = INVALID_BUFFER;
    BufferHandle   m_debugIndex    = INVALID_BUFFER;
    TextureHandle  m_whiteTexture  = INVALID_TEXTURE;
    SamplerHandle  m_whiteSampler  = INVALID_SAMPLER;
    int m_debugIndexCount = 0;

    bool m_running = false;
    double m_lastTime = 0.0;
    int   m_fps       = 0;
    int   m_frames    = 0;
    double m_fpstimer = 0.0;
};

// -----------------------------------------------------------------------
bool Engine::init(const char *bspPath)
{
    Logger &log = Logger::instance();
    log.setLevel(LogLevel::Info);
    log.setFile(stdout);
    log.info("Nova Engine Phase 1 initializing...");

    // ---- Platform ----
    m_platform = createPlatform();
    if (!m_platform)
    {
        log.error("Engine: failed to create platform");
        return false;
    }

    WindowDesc wd{};
    wd.title = "Nova Engine";
    wd.width = 1280;
    wd.height = 720;

    if (!m_platform->createWindow(wd))
    {
        log.error("Engine: failed to create window");
        return false;
    }

    m_platform->setMouseGrab(true);
    m_platform->showCursor(false);

    SDL_Window *win = static_cast<SDL_Window *>(m_platform->getNativeWindow());

    // ---- OpenGL Context ----
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_GLContext glCtx = SDL_GL_CreateContext(win);
    if (!glCtx)
    {
        fprintf(stderr, "Engine: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        log.error("Engine: GL context creation failed");
        return false;
    }

    SDL_GL_MakeCurrent(win, glCtx);

    // Show window and enable relative mouse mode
    SDL_ShowWindow(win);
    SDL_RaiseWindow(win);
    SDL_SetWindowRelativeMouseMode(win, true);

    // ---- Renderer ----
    m_renderer = createRenderBackend();
    if (!m_renderer)
    {
        log.error("Engine: failed to create render backend");
        return false;
    }

    if (!m_renderer->initialize(m_platform))
    {
        log.error("Engine: render backend init failed");
        return false;
    }

    // ---- Swap chain ----
    if (!m_renderer->createSwapChain(win, wd))
    {
        log.error("Engine: createSwapChain failed");
        return false;
    }

    // ---- Shader ----
    ShaderDesc sd{};
    sd.vertexSource   = g_vsSource;
    sd.fragmentSource = g_fsSource;
    m_shader = m_renderer->createShader(sd);
    if (m_shader == INVALID_SHADER)
    {
        log.error("Engine: failed to compile shader");
        return false;
    }

    // ---- UBO ----
    BufferDesc uboDesc{};
    uboDesc.type  = BufferType::Uniform;
    uboDesc.usage = BufferUsage::Dynamic;
    uboDesc.size  = sizeof(PerFrameUBO);
    m_uboBuffer   = m_renderer->createBuffer(uboDesc);
    m_renderer->bindUniformBuffer(m_uboBuffer, 0);

    // ---- White lightmap texture (fallback when no BSP / no atlas) ----
    TextureDesc td{};
    td.type      = TextureType::Texture2D;
    td.width     = td.height = 1;
    td.format    = TextureFormat::RGB8;
    td.minFilter = TextureFilter::Linear;
    td.magFilter = TextureFilter::Linear;
    uint8_t white[3] = {255, 255, 255};
    td.initialData = white;
    m_whiteTexture = m_renderer->createTexture(td);
    m_whiteSampler = m_renderer->createSampler(td);

    // ---- Camera ----
    m_camera = new Camera();
    m_camera->setAspect((float)wd.width / (float)wd.height);

    // ---- BSP ----
    // FIX 1: uploadToGPU() owns the full pipeline:
    //   atlas packing → buildGeometry (with correct lm UVs) → GPU upload.
    // Do NOT call buildGeometry() separately before uploadToGPU().
    if (bspPath && bspPath[0] != '\0')
    {
        m_bsp = new BSPMap();
        if (!m_bsp->load(m_platform, bspPath))
        {
            log.warn("Engine: BSP load failed — running without map");
            delete m_bsp;
            m_bsp = nullptr;
        }
        else
        {
            m_bsp->uploadToGPU(m_renderer);   // atlas + geometry + GPU upload

            Vec3 spawn = m_bsp->getSpawnOrigin();
            m_camera->setPosition(spawn);

            if (m_bsp->getSpawnAngles().y != 0.f)
                m_camera->setYaw(m_bsp->getSpawnAngles().y);

            fprintf(stdout, "Engine: BSP loaded, spawn at (%.1f, %.1f, %.1f)\n",
                    spawn.x, spawn.y, spawn.z);

            // ---- Create physics world with BSP ----
            m_physics = new AABBPhysics();
            m_physics->setWorld(m_bsp);
            m_camera->setPhysicsWorld(m_physics);
        }
    }
    buildDebugScene();

    m_lastTime = (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
    m_running  = true;
    log.info("Engine: Phase 1 ready");
    return true;
}

// -----------------------------------------------------------------------
void Engine::buildDebugScene()
{
    struct Vertex
    {
        float pos[3];
        float uv[2];
        float lmUV[2];
        float n[3];
        uint8_t c[4];
    };
    static_assert(sizeof(Vertex) == 44, "Vertex stride must be 44 bytes");

    Vertex boxVerts[] = {
        // Floor
        {{-10, 0, -10}, {0,0}, {0,0}, {0,1,0}, {140,140,140,255}},
        {{ 10, 0, -10}, {1,0}, {0,0}, {0,1,0}, {140,140,140,255}},
        {{ 10, 0,  10}, {1,1}, {0,0}, {0,1,0}, {140,140,140,255}},
        {{-10, 0,  10}, {0,1}, {0,0}, {0,1,0}, {140,140,140,255}},
        // Back wall
        {{-10,  0, -10}, {0,0}, {0,0}, {0,0,1}, {100,100,140,255}},
        {{ 10,  0, -10}, {1,0}, {0,0}, {0,0,1}, {100,100,140,255}},
        {{ 10,  8, -10}, {1,1}, {0,0}, {0,0,1}, {100,100,140,255}},
        {{-10,  8, -10}, {0,1}, {0,0}, {0,0,1}, {100,100,140,255}},
        // Ceiling
        {{-10,  8, -10}, {0,0}, {0,0}, {0,-1,0}, {80,80,80,255}},
        {{ 10,  8, -10}, {1,0}, {0,0}, {0,-1,0}, {80,80,80,255}},
        {{ 10,  8,  10}, {1,1}, {0,0}, {0,-1,0}, {80,80,80,255}},
        {{-10,  8,  10}, {0,1}, {0,0}, {0,-1,0}, {80,80,80,255}},
    };

    uint32_t boxIndices[] = {
        0, 1, 2,  0, 2, 3,
        4, 5, 6,  4, 6, 7,
        8, 9,10,  8,10,11,
    };

    BufferDesc vbDesc{};
    vbDesc.type = BufferType::Vertex;
    vbDesc.usage = BufferUsage::Static;
    vbDesc.size = sizeof(boxVerts);
    vbDesc.initialData = boxVerts;
    m_debugVertex = m_renderer->createBuffer(vbDesc);

    BufferDesc ibDesc{};
    ibDesc.type = BufferType::Index;
    ibDesc.usage = BufferUsage::Static;
    ibDesc.size = sizeof(boxIndices);
    ibDesc.initialData = boxIndices;
    m_debugIndex = m_renderer->createBuffer(ibDesc);

    m_debugIndexCount = (int)(sizeof(boxIndices) / sizeof(boxIndices[0]));
}

// -----------------------------------------------------------------------
void Engine::shutdown()
{
    if (m_bsp) { delete m_bsp; m_bsp = nullptr; }
    delete m_camera; m_camera = nullptr;

    if (m_renderer) { m_renderer->shutdown(); delete m_renderer; m_renderer = nullptr; }
    if (m_platform) { m_platform->destroyWindow(); delete m_platform; m_platform = nullptr; }

    SDL_Quit();
    Logger::instance().info("Engine: shutdown complete");
}

// -----------------------------------------------------------------------
int Engine::run()
{
    const double targetDt = 1.0 / 60.0;
    const double maxDt    = 0.25;

    while (m_running)
    {
        double now = (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
        double dt  = now - m_lastTime;
        m_lastTime = now;
        if (dt > maxDt) dt = maxDt;

        m_fpstimer += dt;
        m_frames++;
        if (m_fpstimer >= 1.0)
        {
            m_fps = m_frames;
            m_frames = 0;
            m_fpstimer = 0.0;
            char title[128];
            snprintf(title, sizeof(title), "Nova Engine [%d FPS]", m_fps);
            m_platform->setWindowTitle(title);
        }

        update((float)dt);
        render();

        if (dt < targetDt)
        {
            double sleepMs = (targetDt - dt) * 1000.0 - 1.0;
            if (sleepMs > 0.0) SDL_Delay((Uint32)sleepMs);
        }
    }

    return 0;
}

// -----------------------------------------------------------------------
void Engine::update(float dt)
{
    InputState input{};
    if (!m_platform->pollInput(input))
    {
        m_running = false;
        return;
    }

    if (input.keys[SDL_SCANCODE_ESCAPE])
        m_running = false;

    if (input.keys[SDL_SCANCODE_F1])
    {
        static int cycles = 0;
        cycles++;
        SDL_Window *win = static_cast<SDL_Window *>(m_platform->getNativeWindow());
        if (cycles % 2 == 0)
        {
            m_platform->setMouseGrab(true);
            m_platform->showCursor(false);
            SDL_SetWindowRelativeMouseMode(win, true);
        }
        else
        {
            m_platform->setMouseGrab(false);
            m_platform->showCursor(true);
            SDL_SetWindowRelativeMouseMode(win, false);
        }
    }

    m_camera->update(input, dt);
}

// -----------------------------------------------------------------------
void Engine::render()
{
    glClearColor(0.1f, 0.1f, 0.15f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Mat4 viewProj = m_camera->getViewProjectionMatrix();
    memcpy(m_ubo.viewProj, viewProj.data(), sizeof(float) * 16);
    Vec3 cp = m_camera->getPosition();
    m_ubo.camPos[0] = cp.x; m_ubo.camPos[1] = cp.y; m_ubo.camPos[2] = cp.z;
    m_renderer->setBufferData(m_uboBuffer, &m_ubo, sizeof(m_ubo));
    m_renderer->bindShader(m_shader);
    m_renderer->bindUniformBuffer(m_uboBuffer, 0);

    if (m_bsp)
    {
        // BSP render: binds the lightmap atlas internally
        m_bsp->render(m_renderer);
    }
    else
    {
        // Debug scene: use white 1x1 texture as lightmap
        m_renderer->bindTexture(m_whiteTexture, m_whiteSampler, 0);

        if (m_debugVertex != INVALID_BUFFER)
            m_renderer->bindVertexBuffer(m_debugVertex, 0);

        if (m_debugIndex != INVALID_BUFFER && m_debugIndexCount > 0)
        {
            m_renderer->bindIndexBuffer(m_debugIndex);
            m_renderer->drawIndexed(m_debugIndexCount, 0);
        }
    }

    SDL_Window *win = static_cast<SDL_Window *>(m_platform->getNativeWindow());
    SDL_GL_SwapWindow(win);
}

} // namespace nova

// =====================================================================
//  Entry point
// =====================================================================
int main(int argc, char *argv[])
{
    const char *bspPath = (argc > 1) ? argv[1] : "";

    nova::Engine engine;
    if (!engine.init(bspPath))
        return 1;

    int result = engine.run();
    engine.shutdown();
    return result;
}
