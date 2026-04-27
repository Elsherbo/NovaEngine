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
//   2. [NEW] SDL_SetRelativeMouseMode replaces SDL_CaptureMouse.
//      SDL_CaptureMouse only captures events outside the window —
//      it does NOT deliver relative deltas. SDL3 relative mouse mode
//      is SDL_SetWindowRelativeMouseMode(win, true). Without this
//      mouseDeltaX/Y are absolute coords, camera never turns.
//   3. [FIX] m_camera->setPhysicsWorld(m_physics) was missing from
//      the BSP init block, so the camera always ran in noclip mode.
//   4. [FIX] setEntityStorage() must be called before any trace or
//      setOrigin/setVelocity calls so the storage pointer is valid.
//   5. [FIX] Spawn floor trace: sweep the player hull down from
//      well above the BSP spawn origin to land cleanly on the floor
//      instead of relying on a hardcoded Y offset.
//   6. [FIX] Spawn solid escape: if the spawn point (after floor
//      trace) is still inside solid geometry (testSolid() returns
//      true), nudge the candidate upward in 8-unit steps until
//      clear.  This handles Q2 maps where info_player_start is
//      embedded in a brush.  Without this the player falls through
//      the floor or gets stuck inside walls on load.
//   7. [FIX] Fragment shader: removed extra diffuse term. Q2 lightmaps
//      already encode all scene lighting; multiplying by an additional
//      dot(normal, sunDir) term was double-lighting and crushing all
//      dark surfaces to near-black. Correct formula: baseColor * lm.
//   8. [FIX] render(): bindUniformBuffer(m_uboBuffer, 0) added every
//      frame. It was only called once in init(). After the BSP draw
//      changes GL state the UBO binding at slot 0 goes stale; the
//      next frame's shader reads garbage viewProj and produces black.
//   9. [FIX] render(): removed bare glDisable(GL_CULL_FACE). It was
//      undoing the GL_CULL_FACE setup from GLBackend::createSwapChain
//      on every frame, causing back faces to render (inside-out rooms).
//  10. [FIX] Removed manual SDL_GL_CreateContext + gladLoadGL from
//      engine.cpp init. GLBackend::createSwapChain already creates the
//      context and loads GLAD. Having two SDL_GL_CreateContext calls
//      left a dangling unused context from the engine side.
//  11. [FIX] Added EntityList + player entity. EntityList provides game-
//      logic entity storage; external Vec3 storage for physics is kept
//      for AABBPhysics compatibility. Player entity origin is synced from
//      camera each frame. EntityList::think() is called each update tick.
// ============================================================

#include "engine/core/log.h"
#include "engine/renderer/irender_backend.h"
#include "engine/core/camera.h"
#include "engine/platform/iplatform.h"
#include "engine/renderer/irender_backend.h"
#include "engine/renderer/bsp/bsp.h"
#include "engine/renderer/gl/gl_backend.h"
#include "engine/physics/iphysics_world.h"
#include "engine/physics/aabb_physics.h"
#include "engine/entities/entity.h"
#include "engine/entities/entity_list.h"
#include "engine/entities/entity_factory.h"
#include "engine/entities/map_loader.h"
#include "engine/core/asset_fs.h"
#include "vendor/GLAD/include/glad/glad.h"

// Let SDL3 handle the entry point - it will call our main()
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_scancode.h>
#include <GL/gl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
layout(binding = 1) uniform sampler2D uDiffuse;
uniform int uDebugView;

out vec4 fragColor;

void main()
{
    // lmUv.x < 0 is the sentinel for "no lightmap" faces.
    if (vLmUv.x < 0.0)
    {
        // In lightmap-debug modes, highlight missing baked data.
        if (uDebugView != 0)
            fragColor = vec4(1.0, 0.0, 1.0, 1.0);
        else
            fragColor = vec4(0.75, 0.75, 0.75, 1.0);
        return;
    }

    // Sample the lightmap atlas and apply Q2 overbright scale.
    // Q2 stores lightmap values where 128 = "normal brightness", so
    // multiplying by 2 brings the full dynamic range into [0, 2] -> clamp [0, 1].
    // The lightmap atlas is RGB8 (linear data — Q2 bakers write linear radiance).
    // Do NOT gamma-encode here: GL_FRAMEBUFFER_SRGB handles that automatically
    // on framebuffer write, so all shader math must stay in linear space.
    vec3 lm = texture(uLightmap, vLmUv).rgb;
    lm = clamp(lm * 2.0, 0.0, 1.0);

    if (uDebugView == 1)
    {
        // Grayscale baked-light visualization.
        float g = dot(lm, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(vec3(g), 1.0);
        return;
    }
    if (uDebugView == 2)
    {
        // Exposure-boosted lightmap view (reveals very dark bakes).
        vec3 boosted = vec3(1.0) - exp(-lm * 10.0);
        fragColor = vec4(boosted, 1.0);
        return;
    }
    if (uDebugView == 3)
    {
        // UV debug: visualize atlas coordinates directly.
        fragColor = vec4(fract(vLmUv.x), fract(vLmUv.y), 0.0, 1.0);
        return;
    }

    vec3 base = texture(uDiffuse, vUv).rgb * vColor.rgb;
    vec3 lit = max(lm, vec3(0.22));
    fragColor = vec4(base * lit, 1.0);
}
)";

    // =====================================================================
    //  Engine
    // =====================================================================
    class Engine
    {
    public:
        bool init(const char *bspPath, const char *gameDir);
        void shutdown();
        int run();

    private:
        void update(float dt);
        void render();
        void buildDebugScene();

        IPlatform *m_platform = nullptr;
        IRenderBackend *m_renderer = nullptr;
        Camera *m_camera = nullptr;
        BSPMap *m_bsp = nullptr;
        IPhysicsWorld *m_physics = nullptr;
        AssetFS m_assets;

        // Entity system (Problem 6)
        EntityHandle m_playerEntity;

        struct PerFrameUBO
        {
            float viewProj[16];
            float camPos[3];
            float pad0;
        } m_ubo{};

        BufferHandle m_uboBuffer = INVALID_BUFFER;
        ShaderHandle m_shader = INVALID_SHADER;
        BufferHandle m_debugVertex = INVALID_BUFFER;
        BufferHandle m_debugIndex = INVALID_BUFFER;
        TextureHandle m_whiteTexture = INVALID_TEXTURE;
        SamplerHandle m_whiteSampler = INVALID_SAMPLER;
        int m_debugIndexCount = 0;
        int m_debugView = 0; // 0=lit, 1=lightmap gray, 2=lightmap boosted, 3=lm uv

        // External storage for physics (wired to AABBPhysics via setEntityStorage)
        // Kept alongside EntityList — AABBPhysics requires raw Vec3* pointers.
        Vec3 m_cameraPosition = {0, 0, 0};
        Vec3 m_cameraVelocity = {0, 0, 0};

        bool m_running = false;
        double m_lastTime = 0.0;
        int m_fps = 0;
        int m_frames = 0;
        double m_fpstimer = 0.0;
    };

    // -----------------------------------------------------------------------
    bool Engine::init(const char *bspPath, const char *gameDir)
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

        // ---- OpenGL context attributes ----
        // These MUST be set before GLBackend::createSwapChain which calls
        // SDL_GL_CreateContext internally.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        // ---- Renderer ----
        // NOTE: Do NOT call SDL_GL_CreateContext or gladLoadGL here.
        // GLBackend::createSwapChain() owns context creation and GLAD loading.
        // Creating a second context here leaves a dangling unused GL context.
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

        if (!m_renderer->createSwapChain(win, wd))
        {
            log.error("Engine: createSwapChain failed");
            return false;
        }

        SDL_ShowWindow(win);
        SDL_RaiseWindow(win);
        // FIX 2: SDL3 relative mouse mode — delivers per-frame deltas via
        // SDL_EVENT_MOUSE_MOTION. SDL_CaptureMouse is for out-of-window capture
        // only and does NOT produce relative motion events.
        if (!SDL_SetWindowRelativeMouseMode(win, true))
            fprintf(stderr, "Engine: SDL_SetWindowRelativeMouseMode failed: %s\n", SDL_GetError());

        // ---- Shader ----
        ShaderDesc sd{};
        sd.vertexSource = g_vsSource;
        sd.fragmentSource = g_fsSource;
        m_shader = m_renderer->createShader(sd);
        if (m_shader == INVALID_SHADER)
        {
            log.error("Engine: failed to compile shader");
            return false;
        }

        // ---- UBO ----
        BufferDesc uboDesc{};
        uboDesc.type = BufferType::Uniform;
        uboDesc.usage = BufferUsage::Dynamic;
        uboDesc.size = sizeof(PerFrameUBO);
        m_uboBuffer = m_renderer->createBuffer(uboDesc);
        m_renderer->bindUniformBuffer(m_uboBuffer, 0);

        // ---- White 1x1 lightmap fallback ----
        TextureDesc td{};
        td.type = TextureType::Texture2D;
        td.width = td.height = 1;
        td.format = TextureFormat::RGB8;
        td.minFilter = TextureFilter::Linear;
        td.magFilter = TextureFilter::Linear;
        uint8_t white[3] = {255, 255, 255};
        td.initialData = white;
        m_whiteTexture = m_renderer->createTexture(td);
        m_whiteSampler = m_renderer->createSampler(td);

        // ---- Camera ----
        m_camera = new Camera();
        m_camera->setAspect((float)wd.width / (float)wd.height);
        // Depth precision fix: a tiny near plane (0.1) with very large far plane
        // causes z-fighting on coplanar/near-coplanar BSP surfaces.
        m_camera->setNearFar(2.0f, 4096.0f);

        // ---- Assets ----
        auto dirOf = [](const std::string& p) -> std::string
        {
            const size_t s = p.find_last_of("/\\");
            return (s == std::string::npos) ? std::string{} : p.substr(0, s);
        };

        // Mount BSP-local roots first so map-pack textures override base assets.
        if (bspPath && bspPath[0] != '\0')
        {
            const std::string bspFile = bspPath;
            const std::string bspDir = dirOf(bspFile);   // .../maps
            const std::string bspRoot = dirOf(bspDir);   // .../<map-pack-root>
            if (!bspRoot.empty())
            {
                m_assets.mountDirectory(bspRoot);
                fprintf(stdout, "Assets: mounted BSP root '%s'\n", bspRoot.c_str());
            }
            if (!bspDir.empty())
            {
                m_assets.mountDirectory(bspDir);
                fprintf(stdout, "Assets: mounted BSP dir '%s'\n", bspDir.c_str());
            }
        }

        if (gameDir && gameDir[0] != '\0')
        {
            m_assets.mountDirectory(gameDir);
            fprintf(stdout, "Assets: mounted game dir '%s'\n", gameDir);
            std::string pak0 = std::string(gameDir);
            if (!pak0.empty() && pak0.back() != '\\' && pak0.back() != '/')
                pak0 += "\\";
            pak0 += "pak0.pak";
            if (m_assets.mountQuake2Pak(pak0))
                fprintf(stdout, "Assets: mounted Quake2 pak0 '%s'\n", pak0.c_str());
        }

        // ---- BSP + Physics ----
        if (bspPath && bspPath[0] != '\0')
        {
            m_bsp = new BSPMap();
            m_bsp->setAssetFS(&m_assets);
            if (!m_bsp->load(m_platform, bspPath))
            {
                log.warn("Engine: BSP load failed — running without map");
                delete m_bsp;
                m_bsp = nullptr;
            }
            else
            {
                m_bsp->uploadToGPU(m_renderer);

                // ---- Spawn BSP entities ----
                EntityFactory::init();
                int spawned = 0;
                if (m_bsp) {
                    spawned = MapLoader::load(m_bsp);
                    log.info("Engine: %d entities spawned from map", spawned);
                }

                Vec3 spawn = m_bsp->getSpawnOrigin();

                // ---- Physics setup ----
                // Order matters:
                //   1. Create physics
                //   2. Set world geometry
                //   3. Set hull bounds
                //   4. Set entity storage (MUST be before any trace/setOrigin calls)
                //   5. Wire camera entity handle and physics pointer
                //   6. Run spawn floor trace
                //   7. Write spawn position into storage + camera simultaneously

                const Vec3 playerMins = {-16.f, -36.f, -16.f};
                const Vec3 playerMaxs = {16.f, 36.f, 16.f};

                m_physics = new AABBPhysics();
                m_physics->setWorld(m_bsp);
                static_cast<AABBPhysics *>(m_physics)->setPlayerBounds(playerMins, playerMaxs);

                // Step 4: storage must be valid before ANY trace or setOrigin call
                static_cast<AABBPhysics *>(m_physics)->setEntityStorage(&m_cameraPosition, &m_cameraVelocity, 1);

                // Step 5: wire both the entity handle AND the physics pointer into camera
                EntityHandle camEntity = EntityHandle::make(0, 1);
                m_camera->setEntity(camEntity);
                m_camera->setPhysicsWorld(m_physics);

                // Step 6: sweep player hull down from well above spawn to land on floor
                Vec3 traceStart = spawn;
                traceStart.y += 256.f;
                Vec3 traceEnd = spawn;
                traceEnd.y -= 4096.f;

                TraceResult spawnTr = m_physics->trace(traceStart, traceEnd, playerMins, playerMaxs);

                Vec3 safeSpawn;
                if (spawnTr.fraction < 1.0f && spawnTr.normal.y > 0.5f)
                {
                    safeSpawn = spawnTr.endPos;
                    safeSpawn.y += 1.0f;
                    fprintf(stdout, "Engine: spawn floor found at Y=%.1f (trace fraction=%.3f)\n",
                            safeSpawn.y, spawnTr.fraction);
                }
                else
                {
                    log.warn("Engine: spawn ground trace found no floor — using fallback height");
                    safeSpawn = spawn;
                    safeSpawn.y += 64.f;
                }

                // FIX 6: Solid-escape nudge.
                {
                    AABBPhysics *aabb = static_cast<AABBPhysics *>(m_physics);
                    constexpr int kMaxNudgeSteps = 64;
                    constexpr float kNudgeStep = 8.0f;
                    for (int nudge = 0; nudge < kMaxNudgeSteps; ++nudge)
                    {
                        if (!aabb->testSolid(safeSpawn, playerMins, playerMaxs))
                            break;
                        safeSpawn.y += kNudgeStep;
                    }
                    if (aabb->testSolid(safeSpawn, playerMins, playerMaxs))
                        log.warn("Engine: could not escape solid at spawn — player may be stuck");
                    else
                        fprintf(stdout, "Engine: final safe spawn Y=%.1f\n", safeSpawn.y);
                }

                // Step 7: atomically set storage, physics origin, and camera position.
                m_cameraPosition = safeSpawn;
                m_cameraVelocity = {0.f, 0.f, 0.f};
                m_physics->setOrigin(camEntity, safeSpawn);
                m_physics->setVelocity(camEntity, {0.f, 0.f, 0.f});
                m_camera->setPosition(safeSpawn);

                if (m_bsp->getSpawnAngles().y != 0.f)
                    m_camera->setYaw(m_bsp->getSpawnAngles().y);

                fprintf(stdout, "Engine: BSP loaded, spawn at (%.1f, %.1f, %.1f)\n",
                        safeSpawn.x, safeSpawn.y, safeSpawn.z);

                // ---- Create player entity in EntityList (Problem 6c) ----
                // This is a game-logic copy of the player; AABBPhysics continues
                // to use the external m_cameraPosition/m_cameraVelocity storage.
                m_playerEntity = g_entityList.create("player");
                if (Entity* p = g_entityList.get(m_playerEntity))
                {
                    p->origin   = safeSpawn;
                    p->velocity = Vec3{0.f, 0.f, 0.f};
                    p->mins     = playerMins;
                    p->maxs     = playerMaxs;
                    p->state    = STATE_ALIVE;
                }
            }
        }

        // ---- Entity system diagnostics (Acceptance Criteria 5) ----
        fprintf(stdout, "EntityList: sizeof(Entity) = %zu bytes\n", sizeof(Entity));

        buildDebugScene();

        m_lastTime = (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
        m_running = true;
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
            {{-10, 0, -10}, {0, 0}, {0, 0}, {0, 1, 0}, {140, 140, 140, 255}},
            {{10, 0, -10}, {1, 0}, {0, 0}, {0, 1, 0}, {140, 140, 140, 255}},
            {{10, 0, 10}, {1, 1}, {0, 0}, {0, 1, 0}, {140, 140, 140, 255}},
            {{-10, 0, 10}, {0, 1}, {0, 0}, {0, 1, 0}, {140, 140, 140, 255}},
            // Back wall
            {{-10, 0, -10}, {0, 0}, {0, 0}, {0, 0, 1}, {100, 100, 140, 255}},
            {{10, 0, -10}, {1, 0}, {0, 0}, {0, 0, 1}, {100, 100, 140, 255}},
            {{10, 8, -10}, {1, 1}, {0, 0}, {0, 0, 1}, {100, 100, 140, 255}},
            {{-10, 8, -10}, {0, 1}, {0, 0}, {0, 0, 1}, {100, 100, 140, 255}},
            // Ceiling
            {{-10, 8, -10}, {0, 0}, {0, 0}, {0, -1, 0}, {80, 80, 80, 255}},
            {{10, 8, -10}, {1, 0}, {0, 0}, {0, -1, 0}, {80, 80, 80, 255}},
            {{10, 8, 10}, {1, 1}, {0, 0}, {0, -1, 0}, {80, 80, 80, 255}},
            {{-10, 8, 10}, {0, 1}, {0, 0}, {0, -1, 0}, {80, 80, 80, 255}},
        };

        uint32_t boxIndices[] = {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11,
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
        if (m_bsp)
        {
            if (m_renderer)
                m_bsp->releaseGPU(m_renderer);
            delete m_bsp;
            m_bsp = nullptr;
        }
        if (m_physics)
        {
            delete m_physics;
            m_physics = nullptr;
        }

        if (m_renderer)
        {
            if (m_shader != INVALID_SHADER)
                m_renderer->destroyShader(m_shader);
            if (m_uboBuffer != INVALID_BUFFER)
                m_renderer->destroyBuffer(m_uboBuffer);
            if (m_debugVertex != INVALID_BUFFER)
                m_renderer->destroyBuffer(m_debugVertex);
            if (m_debugIndex != INVALID_BUFFER)
                m_renderer->destroyBuffer(m_debugIndex);
            if (m_whiteTexture != INVALID_TEXTURE)
                m_renderer->destroyTexture(m_whiteTexture);
            if (m_whiteSampler != INVALID_SAMPLER)
                m_renderer->destroySampler(m_whiteSampler);
        }

        delete m_camera;
        m_camera = nullptr;

        if (m_renderer)
        {
            m_renderer->shutdown();
            delete m_renderer;
            m_renderer = nullptr;
        }
        if (m_platform)
        {
            m_platform->destroyWindow();
            delete m_platform;
            m_platform = nullptr;
        }

        SDL_Quit();
        Logger::instance().info("Engine: shutdown complete");
    }

    // -----------------------------------------------------------------------
    int Engine::run()
    {
        const double targetDt = 1.0 / 60.0;
        const double maxDt = 0.25;

        while (m_running)
        {
            double now = (double)SDL_GetPerformanceCounter() / SDL_GetPerformanceFrequency();
            double dt = now - m_lastTime;
            m_lastTime = now;
            if (dt > maxDt)
                dt = maxDt;

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
                if (sleepMs > 0.0)
                    SDL_Delay((Uint32)sleepMs);
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

        static bool prevF1 = false;
        const bool f1Down = input.keys[SDL_SCANCODE_F1];
        if (f1Down && !prevF1)
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
        prevF1 = f1Down;

        static bool prevF2 = false;
        const bool f2Down = input.keys[SDL_SCANCODE_F2];
        if (f2Down && !prevF2)
        {
            m_debugView = (m_debugView + 1) % 4;
            const char *modeName =
                (m_debugView == 0) ? "lit" :
                (m_debugView == 1) ? "lightmap-gray" :
                (m_debugView == 2) ? "lightmap-boost" :
                                     "lightmap-uv";
            fprintf(stdout, "Engine: debug view = %s (F2 to cycle)\n", modeName);
        }
        prevF2 = f2Down;

        // F3: print PVS stats
        static bool prevF3 = false;
        const bool f3Down = input.keys[SDL_SCANCODE_F3];
        if (f3Down && !prevF3 && m_bsp)
        {
            Vec3 pos = m_camera->getPosition();
            const int nodeCount  = m_bsp->nodeCount();
            const int leafCount  = m_bsp->leafCount();
            const int planeCount = m_bsp->planeCount();
            (void)nodeCount; (void)planeCount;

            int camLeaf = -1;
            {
                int ni = 0;
                while (ni >= 0)
                {
                    const nova::BSPNode* nd = m_bsp->nodes() + ni;
                    const nova::BSPPlane* pl = m_bsp->planes() + nd->plane;
                    float d = pos.x*pl->normal.x + pos.y*pl->normal.y + pos.z*pl->normal.z - pl->dist;
                    ni = nd->children[d < 0.f ? 1 : 0];
                }
                int li = ~ni;
                if (li >= 0 && li < leafCount) camLeaf = li;
            }

            int camCluster = -1;
            if (camLeaf >= 0)
                camCluster = (int)m_bsp->leaves()[camLeaf].cluster;

            fprintf(stdout, "PVS: camLeaf=%d cluster=%d\n", camLeaf, camCluster);
        }
        prevF3 = f3Down;

        m_camera->update(input, dt);

        // ---- Entity think dispatch (Problem 6f) ----
        g_entityList.think(dt);

        // ---- Sync player entity origin from camera (Problem 6e) ----
        if (Entity* p = g_entityList.get(m_playerEntity))
            p->origin = m_camera->getPosition();
    }

    // -----------------------------------------------------------------------
    void Engine::render()
    {
        m_renderer->clearColor(0.1f, 0.1f, 0.15f, 1.f);
        m_renderer->clearDepth(0.f);

        Mat4 viewProj = m_camera->getViewProjectionMatrix();
        memcpy(m_ubo.viewProj, viewProj.data(), sizeof(float) * 16);
        Vec3 cp = m_camera->getPosition();
        m_ubo.camPos[0] = cp.x;
        m_ubo.camPos[1] = cp.y;
        m_ubo.camPos[2] = cp.z;

        // FIX 8: Rebind UBO every frame — BSP render may change GL state.
        m_renderer->setBufferData(m_uboBuffer, &m_ubo, sizeof(m_ubo));
        m_renderer->bindShader(m_shader);
        m_renderer->bindUniformBuffer(m_uboBuffer, 0);
        {
            GLint debugLoc = glGetUniformLocation(static_cast<GLuint>(m_shader), "uDebugView");
            if (debugLoc >= 0)
                glUniform1i(debugLoc, m_debugView);
        }

        if (m_bsp)
        {
            m_bsp->setViewProj(viewProj);
            m_bsp->render(m_renderer, m_camera->getPosition());
        }
        else
        {
            m_renderer->bindTexture(m_whiteTexture, m_whiteSampler, 0);
            if (m_debugVertex != INVALID_BUFFER)
                m_renderer->bindVertexBuffer(m_debugVertex, 0, nullptr);
            if (m_debugIndex != INVALID_BUFFER && m_debugIndexCount > 0)
            {
                m_renderer->bindIndexBuffer(m_debugIndex);
                m_renderer->drawIndexed(m_debugIndexCount, 0);
            }
        }

        m_renderer->present();
    }

} // namespace nova

// =====================================================================
//  Entry point
// =====================================================================
int main(int argc, char *argv[])
{
    const char *bspPath = (argc > 1) ? argv[1] : "";
    const char *gameDir = "";
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-gameDir") == 0 && i + 1 < argc)
            gameDir = argv[i + 1];
    }

    nova::Engine engine;
    if (!engine.init(bspPath, gameDir))
        return 1;

    int result = engine.run();
    engine.shutdown();
    return result;
}
