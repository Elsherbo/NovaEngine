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
#include "engine/entities/igame_module.h"
#include "engine/entities/map_loader.h"
#include "engine/entities/game_dll_loader.h"
#include "engine/world/iworld.h"
#include "engine/world/bsp_world.h"
#include "engine/core/asset_fs.h"
#include <glad/glad.h>
#include "engine/core/cvar.h"
#include "engine/core/console.h"
#include "engine/core/text_2d.h"
#include "engine/renderer/models/md2.h"

// Game-module PlayerController (compiled into nova_engine/nova_player,
// drives the camera from physics in the engine main loop).
#include "engine/player/player_controller.h"

// Let SDL3 handle the entry point - it will call our main()
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_scancode.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>


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

uniform mat4 uModelMatrix; // identity for BSP, model transform for MD2/OBJ
uniform int uModelMode;    // 0=BSP (world space), 1=MD2/OBJ (model space)

out vec2 vUv;
out vec2 vLmUv;
out vec3 vNormal;
out vec3 vWorldPos;
out vec4 vColor;

void main()
{
    vUv       = aTexCoord;
    vLmUv     = aLmCoord;
    vColor    = aColor;

    if (uModelMode != 0)
    {
        // Model: transform from model space to world space
        vec4 worldPos = uModelMatrix * vec4(aPosition, 1.0);
        vWorldPos = worldPos.xyz;
        // Transform normal by inverse transpose of model matrix (rotation-only for uniform scale)
        vNormal = mat3(uModelMatrix) * aNormal;
        gl_Position = uViewProj * worldPos;
    }
    else
    {
        // BSP: vertices already in world space
        vNormal   = aNormal;
        vWorldPos = aPosition;
        gl_Position = uViewProj * vec4(aPosition, 1.0);
    }
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
uniform int uModelMode; // 0=BSP (lightmap), 1=MD2/OBJ (diffuse + point lighting)

uniform vec3 uModelLightColor = vec3(1.0); // pre-sampled BSP lightmap color for models

out vec4 fragColor;

void main()
{
    if (uModelMode != 0)
    {
        // Model: diffuse texture sampled BSP lightmap color
        vec3 base = texture(uDiffuse, vUv).rgb * vColor.rgb;
        fragColor = vec4(base * uModelLightColor, 1.0);
        return;
    }

    // lmUv.x < 0 is the sentinel for "no lightmap" faces.
    if (vLmUv.x < 0.0)
    {
        if (uDebugView != 0)
            fragColor = vec4(1.0, 0.0, 1.0, 1.0);
        else
            fragColor = vec4(0.75, 0.75, 0.75, 1.0);
        return;
    }

    vec3 lm = texture(uLightmap, vLmUv).rgb;
    lm = clamp(lm * 2.0, 0.0, 1.0);

    if (uDebugView == 1)
    {
        float g = dot(lm, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(vec3(g), 1.0);
        return;
    }
    if (uDebugView == 2)
    {
        vec3 boosted = vec3(1.0) - exp(-lm * 10.0);
        fragColor = vec4(boosted, 1.0);
        return;
    }
    if (uDebugView == 3)
    {
        fragColor = vec4(fract(vLmUv.x), fract(vLmUv.y), 0.0, 1.0);
        return;
    }

    vec3 base = texture(uDiffuse, vUv).rgb * vColor.rgb;
    vec3 lit = lm;
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
        void render(float dt);
        void buildDebugScene();

        IPlatform *m_platform = nullptr;
        IRenderBackend *m_renderer = nullptr;
        Camera *m_camera = nullptr;
        PlayerController *m_playerCtrl = nullptr;   // owns all movement physics
        BSPMap *m_bsp = nullptr;
        BSPWorld *m_bspWorld = nullptr;   // IWorld facade over m_bsp
        IPhysicsWorld *m_physics = nullptr;
        AssetFS m_assets;

        // Entity system (uses the global g_entityList shared with
        // MapLoader, EntityFactory, and the GameModule DLL).
        EntityHandle m_playerEntity;
        GameDLLLoader m_gameDLL;

        // MD2 model rendering
        ModelRenderer m_modelRenderer;
        int           m_testModelIndex = -1;   // test MD2 model index
        MD2Instance   m_testModelInstance;     // persistent animation state
        int           m_currentAnimIdx = 0;    // current animation index
        float         m_animTimer = 0.0f;      // time spent in current animation
        std::vector<MD2AnimRange> m_animList;  // detected animations for cycling
        int           m_testOBJIndex = -1;      // test OBJ model index

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
        //int m_debugView = 0; // 0=lit, 1=lightmap gray, 2=lightmap boosted, 3=lm uv
        
        // r_debugview CVar replaces the old int m_debugView.
        // 0=lit  1=lightmap-gray  2=lightmap-boost  3=lightmap-uv
        CVar* m_cvDebugView = nullptr;
    
        // In-game developer console (tilde to toggle)
        Console* m_console = nullptr;


        // External storage for physics (wired to AABBPhysics via setEntityStorage).
        // PlayerController reads/writes through IPhysicsWorld; these backing
        // arrays are the concrete storage AABBPhysics points at.
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

        // ---- Text2D init (GL objects only, font loaded after assets mount) ----
        Text2D::init();
        
        // ---- AssetFS ----
        // Assets are mounted from bspDir/gameDir in step below

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

        // ---- CVar: register engine-level cvars ----
        // Player cvars are registered automatically by the inline definitions
        // in player_controller.h when that header is first included.
        // Here we register engine-side cvars.
        m_cvDebugView = CVarSystem::instance().reg(
            "r_debugview", 0.0f,
            "render debug view: 0=lit 1=lm-gray 2=lm-boost 3=lm-uv");
 
        // sv_cheats — must exist before any Cheat-flagged cvar is set
        CVarSystem::instance().reg("sv_cheats", 0.0f, "enable cheat cvars");
 
        // ---- Console ----
        m_console = new Console();
        
        Logger::instance().setHook([this](LogLevel level, const char* msg) {
            if (!m_console) return;
            m_console->addLogLine(level, msg);
        });

        m_console->setMouseGrabCallback([this](bool grab)
        {
            SDL_Window* win = static_cast<SDL_Window*>(m_platform->getNativeWindow());
            if (grab)
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
        });
        
        // Also after m_console is set up, seed it with some startup lines:
        m_console->addLine("Nova Engine ready. Type 'help' for commands.", ConColor::Output);
        m_console->addLine("Press ~ to open/close console.", ConColor::Dim);

        // ---- PlayerController ----
        m_playerCtrl = new PlayerController();

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
            if (!pak0.empty() && pak0.back() != '/')
                pak0 += "/";
            pak0 += "pak0.pak";
            if (m_assets.mountQuake2Pak(pak0))
                fprintf(stdout, "Assets: mounted Quake2 pak0 '%s'\n", pak0.c_str());
        }

        // ---- Mount assets/ directory for models, fonts, etc. ----
        // Try mounting relative to the working directory (project root).
        m_assets.mountDirectory("assets");
        fprintf(stdout, "Assets: mounted 'assets/' (models, fonts, etc.)\n");

        // ---- Entity factory ----
        // Must be called before BSP load so MapLoader::load() finds all spawn functions.
        EntityFactory::init();
        fprintf(stdout, "Engine: EntityFactory initialized (%d built-in classes)\n", EntityFactory::classCount());

        // Standard player hull — consistent with AABBPhysics setPlayerBounds.
        // Physics hull is centered at origin: {-16,-28,-16} to {16,28,16} (32×56×32).
        // Entity storage uses feet-at-origin convention (Z=0 at feet, Z=32 at head).
        const Vec3 playerMins = {-kPC_HullHalfX, -kPC_HullHalfY, 0.f};
        const Vec3 playerMaxs = { kPC_HullHalfX,  kPC_HullHalfY, kPC_HullHalfZ * 2.f};

        // ---- Game DLL ----
        // Load before BSP so game->loadMap() can be called immediately after upload.
        if (gameDir && gameDir[0] != '\0')
        {
            std::string dllPath = std::string(gameDir) + "/nova_game.dll";
            if (m_gameDLL.load(dllPath.c_str()))
            {
                log.info("Engine: game DLL loaded");
                fprintf(stdout, "Engine: game DLL ready\n");
            }
            else
            {
                log.warn("Engine: game DLL not found, running engine-only");
            }
        }

        // ---- Load console font (must be AFTER AssetFS is mounted) ----
        // AssetFS now knows where 'assets/' is, so 'pics/conchars.png' resolves
        // to 'assets/pics/conchars.png' correctly.
        {
            // Try Q2-style conchars first.
            // Accepts any square image divisible by 16: 128, 256, 512, 1024.
            // Top half = normal (silver) set, bottom half = alternate (gold) set.
            // Place your file at: assets/pics/conchars.png
            const char* q2fonts[] = {
                "pics/conchars.png",
                "pics/conchars.tga",
                "pics/conchars.jpg",
            };
            bool loaded = false;
            for (const char* fp : q2fonts)
            {
                if (Text2D::tryLoadQ2Conchars(&m_assets, fp))
                {
                    loaded = true;
                    fprintf(stdout, "Text2D: loaded Q2 conchars from '%s'%s\n",
                        fp,
                        Text2D::hasAlternateSet()
                            ? " (normal + alternate set)"
                            : " (normal set only)");
                    break;
                }
            }

            // Fall back to generic 128x128 single-set bitmap fonts
            if (!loaded)
            {
                const char* genericFonts[] = {
                    "pics/font.png",
                    "pics/font.tga",
                    "fonts/default.png",
                };
                for (const char* fp : genericFonts)
                {
                    if (Text2D::tryLoadFont(&m_assets, fp))
                    {
                        loaded = true;
                        fprintf(stdout, "Text2D: loaded generic font from '%s'\n", fp);
                        break;
                    }
                }
            }

            if (!loaded)
                fprintf(stdout, "Text2D: using built-in IBM CP437 8x8 font\n");
        }

        // ---- BSP + Physics ----
        if (bspPath && bspPath[0] != '\0')
        {
            m_bsp = new BSPMap();
            // MUST set AssetFS BEFORE load so textures can be loaded
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

                // ---- Create IWorld facade ----
                // BSPWorld wraps BSPMap behind the IWorld interface.
                // From this point on, all callers (physics, game DLL, MapLoader)
                // receive IWorld* — not BSPMap*.
                m_bspWorld = new BSPWorld(m_bsp);

                // ---- Create physics world ---
                m_physics = new AABBPhysics();
                m_physics->setWorld(m_bspWorld->collisionWorld());

                // ---- Create player entity FIRST in g_entityList so it gets index 0 ----
                // This MUST happen before MapLoader::load(), which also spawns into
                // g_entityList. The physics external storage (setEntityStorage) only
                // tracks index 0, so the player MUST be at index 0.
                m_playerEntity = g_entityList.create("player");
                static_cast<AABBPhysics*>(m_physics)->setEntityStorage(&m_cameraPosition, &m_cameraVelocity, 1);

                static_cast<AABBPhysics*>(m_physics)->setPlayerBounds(
                    Vec3{-kPC_HullHalfX, -kPC_HullHalfY, -kPC_HullHalfZ},
                    Vec3{ kPC_HullHalfX,  kPC_HullHalfY,  kPC_HullHalfZ}
                );
                m_playerCtrl->setPhysicsWorld(m_physics);

                // ---- Spawn BSP entities (engine-side, always runs) ----
                {
                    MapLoader mapLoader;
                    int spawned = mapLoader.load(m_bspWorld);
                    fprintf(stdout, "Engine: MapLoader spawned %d entities from BSP lump\n", spawned);
                }

                // ---- Notify game module (if DLL is loaded) ----
                if (IGameModule* game = m_gameDLL.get())
                    game->loadMap(m_bspWorld);

                Vec3 spawn = m_bspWorld->getSpawnOrigin();
                // Skip floor trace - use spawn origin directly

                Vec3 safeSpawn = spawn;
                {
                    const Vec3 pMins = { -kPC_HullHalfX, -kPC_HullHalfY, -kPC_HullHalfZ };
                    const Vec3 pMaxs = {  kPC_HullHalfX,  kPC_HullHalfY,  kPC_HullHalfZ };

                    auto isSpawnSolid = [&](const Vec3& pos) -> bool {
                        TraceResult tr = m_physics->trace(
                            pos, {pos.x, pos.y - 1.0f, pos.z}, pMins, pMaxs);
                        return tr.startSolid;
                    };

                    // Nudge upward until not solid
                    for (int i = 0; i < 128 && isSpawnSolid(safeSpawn); ++i)
                        safeSpawn.y += 1.0f;

                    // If still solid after going up, try moving forward (-Z in GL)
                    if (isSpawnSolid(safeSpawn)) {
                        safeSpawn = spawn;
                        for (int i = 0; i < 128 && isSpawnSolid(safeSpawn); ++i)
                            safeSpawn.z -= 1.0f;
                    }

                    fprintf(stdout, "Engine: safe spawn at (%.1f, %.1f, %.1f)\n",
                        safeSpawn.x, safeSpawn.y, safeSpawn.z);
                }

            // Step 7: set camera position
                m_cameraPosition = safeSpawn;
                m_cameraVelocity = {0.f, 0.f, 0.f};
                m_camera->setPosition(safeSpawn);
                m_playerCtrl->setPosition(safeSpawn);

                if (m_bspWorld->getSpawnAngles().y != 0.f)
                    m_camera->setYaw(m_bspWorld->getSpawnAngles().y);

                fprintf(stdout, "Engine: BSP loaded, spawn at (%.1f, %.1f, %.1f)\n",
                        safeSpawn.x, safeSpawn.y, safeSpawn.z);

                // ---- Finish player entity setup (origin, bounds, etc.) ----
                if (Entity* p = g_entityList.get(m_playerEntity))
                {
                    p->origin   = safeSpawn;
                    p->velocity = Vec3{0.f, 0.f, 0.f};
                    p->mins     = playerMins;
                    p->maxs     = playerMaxs;
                    p->state    = STATE_ALIVE;

                    m_playerCtrl->setEntity(m_playerEntity);

                    // Notify game DLL about player spawn
                    if (IGameModule* game = m_gameDLL.get())
                        game->onEntitySpawn(m_playerEntity);
                }
            }
        }

        // ---- Entity system diagnostics (Acceptance Criteria 5) ----
        fprintf(stdout, "EntityList: sizeof(Entity) = %zu bytes\n", sizeof(Entity));

        // ---- MD2 Model Loading (test) ----
        // Try loading an MD2 model from assets/models/.
        {
            const char* testModels[] = {
                "models/soldier/mach-body.md2",
                "models/cobra/cobra.md2",
                "models/eagle/eagle.md2",
                "models/Monkey/monkey.md2",
                "models/raven/raven.md2",
                "models/snake/snake.md2",
                "models/warrior/warrior.md2",
            };
            for (const char* mp : testModels)
            {
                m_testModelIndex = m_modelRenderer.loadMD2Model(m_renderer, &m_assets, mp);
                if (m_testModelIndex >= 0)
                {
                    fprintf(stdout, "Engine: loaded MD2 model '%s' (index %d)\n", mp, m_testModelIndex);
                    break;
                }
            }
            if (m_testModelIndex < 0)
                fprintf(stdout, "Engine: no MD2 model found (tried common paths)\n");

            // Set up test model animation once
            if (m_testModelIndex >= 0 && m_modelRenderer.getMD2Mesh(m_testModelIndex))
            {
                const MD2Mesh* mesh = m_modelRenderer.getMD2Mesh(m_testModelIndex);
                const auto& anims = mesh->anims();
                if (!anims.empty())
                {
                    // Copy to vector for indexed access
                    for (const auto& kv : anims)
                        m_animList.push_back(kv.second);
                    m_testModelInstance.setAnim(m_animList[0]);
                }
                else
                {
                    m_testModelInstance.setAnim(0, mesh->numFrames() - 1, 10.0f);
                }
            }

            // Try loading a test OBJ model
            const char* testOBJs[] = {
                "models/soldier/mach-body.obj",
                "models/warrior/warrior.obj",
                "models/cobra/cobra.obj",
            };
            for (const char* op : testOBJs)
            {
                m_testOBJIndex = m_modelRenderer.loadOBJModel(m_renderer, &m_assets, op);
                if (m_testOBJIndex >= 0)
                {
                    fprintf(stdout, "Engine: loaded OBJ model '%s' (index %d)\n", op, m_testOBJIndex);
                    break;
                }
            }
            if (m_testOBJIndex < 0)
                fprintf(stdout, "Engine: no OBJ model found (tried common paths)\n");
        }

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
            delete m_bspWorld;
            m_bspWorld = nullptr;
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

        Logger::instance().setHook(nullptr);
        delete m_console;
        m_console = nullptr;

        delete m_playerCtrl;
        m_playerCtrl = nullptr;

        delete m_camera;
        m_camera = nullptr;

        // ---- Unload game DLL ----
        m_gameDLL.unload();

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
            render((float)dt);

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
        // We need the previous frame's InputState for edge detection in
        // the console and for the F-key toggles.  Keep it as a static.
        static InputState prevInput{};
 
        if (!m_platform->pollInput(input))
        {
            m_running = false;
            return;
        }

        // ---- Window resize ----
        if (input.windowResized && input.newWindowW > 0 && input.newWindowH > 0)
        {
            m_renderer->setSwapChainSize(input.newWindowW, input.newWindowH);
            m_camera->setAspect((float)input.newWindowW / (float)input.newWindowH);
            // Re-bind the UBO since GL state may have been touched
            m_renderer->bindUniformBuffer(m_uboBuffer, 0);
        }
 
        // ---- Console input (must run first — swallows all other input) ----
        // NOTE: no early return here. prevInput is always updated at the
        // bottom of update() so key edge-detection stays correct every frame.
        // The isOpen() guard below skips all gameplay input while open.
        if (m_console)
            m_console->handleInput(input, prevInput);
 
        const bool consoleOpen = m_console && m_console->isOpen();
 
        // ---- Escape (only when console is closed) ----
        if (!consoleOpen && input.keys[SDL_SCANCODE_ESCAPE])
            m_running = false;
 
        if (!consoleOpen)
        {
            // ---- F1: mouse grab toggle ----
            static bool prevF1 = false;
            const bool f1Down = input.keys[SDL_SCANCODE_F1];
            if (f1Down && !prevF1)
            {
                static int cycles = 0;
                cycles++;
                SDL_Window* win = static_cast<SDL_Window*>(m_platform->getNativeWindow());
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
 
            // ---- F2: cycle r_debugview CVar ----
            static bool prevF2 = false;
            const bool f2Down = input.keys[SDL_SCANCODE_F2];
            if (f2Down && !prevF2 && m_cvDebugView)
            {
                const int next = (static_cast<int>(m_cvDebugView->value) + 1) % 4;
                CVarSystem::instance().set(m_cvDebugView, static_cast<float>(next));
                const char* modeName =
                    (next == 0) ? "lit" :
                    (next == 1) ? "lightmap-gray" :
                    (next == 2) ? "lightmap-boost" :
                                   "lightmap-uv";
                Logger::instance().info("r_debugview = %d (%s)", next, modeName);
            }
            prevF2 = f2Down;
 
            // ---- F3: PVS debug ----
            static bool prevF3 = false;
            const bool f3Down = input.keys[SDL_SCANCODE_F3];
            if (f3Down && !prevF3 && m_bspWorld)
            {
                Vec3 pos = m_camera->getPosition();
                int camCluster = m_bspWorld->clusterForPoint(pos);
                Logger::instance().info("PVS: cluster=%d  visible from self=%s",
                    camCluster,
                    m_bspWorld->isClusterVisible(camCluster, camCluster) ? "yes" : "no");
            }
            prevF3 = f3Down;
 
            // ---- Mouse look ----
            m_camera->applyMouseLook(
                static_cast<float>(input.mouseDeltaX),
                static_cast<float>(input.mouseDeltaY));
 
            // ---- PlayerController: movement physics ----
            if (m_playerCtrl)
            {
                m_playerCtrl->update(input, dt,
                                      m_camera->getForward(),
                                      m_camera->getRight());
                m_camera->setPosition(m_playerCtrl->getEyePosition());
            }
 
            // ---- Entity think ----
            g_entityList.think(dt);
 
            // ---- Game DLL think ----
            if (IGameModule* game = m_gameDLL.get())
                game->think(dt);
 
            // ---- Sync player entity origin ----
            if (Entity* p = g_entityList.get(m_playerEntity))
                p->origin = m_camera->getPosition();
 
        } // end !consoleOpen
 
        // Always update prevInput — even when console is open.
        // This ensures edge-detection (key down/up) works correctly
        // every frame for both the console and gameplay keys.
        prevInput = input;
    }



    // -----------------------------------------------------------------------
    void Engine::render(float dt)
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
            const int dv = m_cvDebugView ? static_cast<int>(m_cvDebugView->value) : 0;
            GLint debugLoc = glGetUniformLocation(static_cast<GLuint>(m_shader), "uDebugView");
            if (debugLoc >= 0)
                glUniform1i(debugLoc, dv);
        }

        if (m_bsp)
        {
            m_bsp->setViewProj(viewProj);
            m_bsp->setViewFrustum(viewProj);
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

        // ---- MD2 Model Rendering ----
        if (m_testModelIndex >= 0 && !m_animList.empty())
        {
            const MD2Mesh* mesh = m_modelRenderer.getMD2Mesh(m_testModelIndex);
            if (mesh && mesh->numSkins() > 0)
            {
                // Cycle to next animation every 3 seconds
                m_animTimer += dt;
                if (m_animTimer >= 3.0f)
                {
                    m_animTimer = 0.0f;
                    m_currentAnimIdx = (m_currentAnimIdx + 1) % (int)m_animList.size();
                    m_testModelInstance.setAnim(m_animList[m_currentAnimIdx]);
                }

                // Update position: 80 units in front of camera, same height
                m_testModelInstance.origin = m_camera->getPosition() + m_camera->getForward() * 80.0f;
                m_testModelInstance.origin.y = m_camera->getPosition().y;

                // Update animation
                m_testModelInstance.update(dt);

                // Frustum cull: skip if model is outside view
                bool md2Visible = true;
                if (m_bsp && !m_bsp->sphereInFrustum(m_testModelInstance.origin, 40.0f))
                    md2Visible = false;

                if (md2Visible)
                {
                    // Build model matrix
                    Mat4 model = Mat4::translate(m_testModelInstance.origin);
                    model = model * Mat4::rotate(m_testModelInstance.angles.y, Vec3{0, 1, 0});
                    model = model * Mat4::rotate(m_testModelInstance.angles.x, Vec3{1, 0, 0});
                    model = model * Mat4::rotate(m_testModelInstance.angles.z, Vec3{0, 0, 1});
                    model = model * Mat4::scale(Vec3{m_testModelInstance.scale, m_testModelInstance.scale, m_testModelInstance.scale});

                    // Set uniforms and render
                    GLint modelModeLoc = glGetUniformLocation(static_cast<GLuint>(m_shader), "uModelMode");
                    GLint modelMatrixLoc = glGetUniformLocation(static_cast<GLuint>(m_shader), "uModelMatrix");
                    GLint modelLightLoc = glGetUniformLocation(static_cast<GLuint>(m_shader), "uModelLightColor");
                    if (modelModeLoc >= 0) glUniform1i(modelModeLoc, 1);
                    if (modelMatrixLoc >= 0) glUniformMatrix4fv(modelMatrixLoc, 1, GL_FALSE, model.data());

                    // Source Engine style BSP lightmap sampling
                    if (m_bsp && modelLightLoc >= 0)
                    {
                        Vec3 lightColor = m_bsp->samplePointLighting(m_testModelInstance.origin);
                        glUniform3f(modelLightLoc, lightColor.x, lightColor.y, lightColor.z);
                    }
                    else if (modelLightLoc >= 0)
                    {
                        glUniform3f(modelLightLoc, 1.0f, 1.0f, 1.0f);
                    }

                    m_modelRenderer.renderEntity(m_renderer, m_testModelIndex, m_testModelInstance, m_shader);

                    if (modelModeLoc >= 0) glUniform1i(modelModeLoc, 0);
                    if (modelMatrixLoc >= 0) glUniformMatrix4fv(modelMatrixLoc, 1, GL_FALSE, Mat4::identity().data());
                }
            }
        }

        // ---- OBJ Model Rendering ----
        if (m_testOBJIndex >= 0 && m_modelRenderer.getMesh(m_testOBJIndex))
        {
            MeshInstance objInst;
            objInst.origin = m_camera->getPosition() + m_camera->getForward() * 80.0f + m_camera->getRight() * 60.0f;
            objInst.origin.y = m_camera->getPosition().y;
            objInst.scale = 1.0f;

            // Frustum cull
            bool objVisible = true;
            if (m_bsp && !m_bsp->sphereInFrustum(objInst.origin, 40.0f))
                objVisible = false;

            if (objVisible)
            {
                // Model matrix transforms (slow Y rotation + scale pulse)
                Mat4 model = Mat4::translate(objInst.origin);
                model = model * Mat4::rotate(sin(dt * 0.5f) * 3.14159f * 2.0f, Vec3{0, 1, 0});
                float pulseScale = 1.0f + sin(dt * 2.0f) * 0.1f;
                model = model * Mat4::scale(Vec3{pulseScale, pulseScale, pulseScale});

                GLint modelModeLoc = glGetUniformLocation(static_cast<GLuint>(m_shader), "uModelMode");
                GLint modelMatrixLoc = glGetUniformLocation(static_cast<GLuint>(m_shader), "uModelMatrix");
                GLint modelLightLoc = glGetUniformLocation(static_cast<GLuint>(m_shader), "uModelLightColor");
                if (modelModeLoc >= 0) glUniform1i(modelModeLoc, 1);
                if (modelMatrixLoc >= 0) glUniformMatrix4fv(modelMatrixLoc, 1, GL_FALSE, model.data());

                if (m_bsp && modelLightLoc >= 0)
                {
                    Vec3 lightColor = m_bsp->samplePointLighting(objInst.origin);
                    glUniform3f(modelLightLoc, lightColor.x, lightColor.y, lightColor.z);
                }
                else if (modelLightLoc >= 0)
                {
                    glUniform3f(modelLightLoc, 1.0f, 1.0f, 1.0f);
                }

                m_modelRenderer.renderStatic(m_renderer, m_testOBJIndex, objInst, m_shader);

                if (modelModeLoc >= 0) glUniform1i(modelModeLoc, 0);
                if (modelMatrixLoc >= 0) glUniformMatrix4fv(modelMatrixLoc, 1, GL_FALSE, Mat4::identity().data());
            }
        }


        // ---- Console overlay (renders on top of everything) ----
        if (m_console)
        m_console->render(m_renderer->getWidth(), m_renderer->getHeight());

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
    const char *compileMap = nullptr;
    
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-gameDir") == 0 && i + 1 < argc)
            gameDir = argv[i + 1];
        else if (strcmp(argv[i], "-compile") == 0 && i + 1 < argc)
            compileMap = argv[i + 1];
    }
    
    (void)argc; (void)argv; // unused in non-debug builds
    
    if (compileMap)
    {
        printf("NovaEngine: Compiling map '%s'...\n", compileMap);
        // TODO: call qbsp/vis/light here
        printf("NovaEngine: Map compilation not yet implemented.\n");
        return 0;
    }
    
    nova::Engine engine;
    if (!engine.init(bspPath, gameDir))
        return 1;

    int result = engine.run();
    engine.shutdown();
    return result;
}