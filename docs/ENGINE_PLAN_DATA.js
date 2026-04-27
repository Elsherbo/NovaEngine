// ============================================================
// FILE:    docs/ENGINE_PLAN_DATA.js
// PURPOSE: Single source of truth for all NOVA ENGINE plan content.
//          Edit THIS file to update statuses, add systems, or change notes.
//          generate.js reads this file and builds the .docx — never touch
//          generate.js just to update content.
//
// STATUS VALUES (use exactly these strings):
//   "✅ DONE"        — Fully implemented, tested, stable
//   "🔄 IN PROGRESS" — Work started, partially complete
//   "⬜ TODO"        — Planned, not yet started
//   "❌ MISSING"     — Required but not yet designed/planned
// ============================================================

"use strict";

// ── Meta ──────────────────────────────────────────────────────
const META = {
    title:     "NOVA ENGINE",
    subtitle:  "Game Engine Development Master Plan",
    tagline:   "Quake 2 Foundation  →  Source Engine Capabilities",
    revision:  "1.2",
    codename:  "NOVA",
    startDate: "TBD",
};

// ── Phase Roadmap Summary ─────────────────────────────────────
// Shown in the overview table in Section 2.
const PHASE_SUMMARY = [
    { phase: "Phase 1", name: "Foundation",     timeline: "0–6 months",   deliverables: "Math, memory, platform, OpenGL renderer, BSP loader + PVS, windowing, lightmaps", status: "✅ DONE" },
    { phase: "Phase 2", name: "Quake 2 Core",   timeline: "6–12 months",  deliverables: "Entity system, AABB physics, delta networking, audio, game DLL",                  status: "🔄 IN PROGRESS" },
    { phase: "Phase 3", name: "Source Bridge",  timeline: "12–24 months", deliverables: "Normal maps, HDR, displacement geo, Jolt physics, Lua scripting",                  status: "⬜ TODO" },
    { phase: "Phase 4", name: "Modern Engine",  timeline: "24+ months",   deliverables: "Vulkan backend, PBR, GPU-driven rendering, editor (Hammer-like)",                   status: "⬜ TODO" },
];

// ── Directory Structure ───────────────────────────────────────
// Shown in Section 3.
const DIRECTORY_STRUCTURE = [
    { path: "engine/",                    type: "Root",   purpose: "Engine source root — all engine code lives here" },
    { path: "engine/core/",               type: "Module", purpose: "Math, memory allocators, string utils, containers, logging" },
    { path: "engine/core/math/",          type: "Sub",    purpose: "Vec2/3/4, Mat4, Quaternion, Plane, AABB, Ray structs" },
    { path: "engine/core/memory/",        type: "Sub",    purpose: "Zone allocator, arena allocator, pool allocator" },
    { path: "engine/core/containers/",    type: "Sub",    purpose: "Array, HashMap, StringView — no STL in hot paths" },
    { path: "engine/platform/",           type: "Module", purpose: "IPlatform interface + SDL2 implementation" },
    { path: "engine/platform/sdl2/",      type: "Sub",    purpose: "SDL2Platform: window, input, file I/O, threading" },
    { path: "engine/renderer/",           type: "Module", purpose: "IRenderBackend interface + all rendering code" },
    { path: "engine/renderer/gl/",        type: "Sub",    purpose: "OpenGLBackend: shaders, buffers, textures, draw calls" },
    { path: "engine/renderer/vulkan/",    type: "Sub",    purpose: "VulkanBackend (Phase 4) — not yet started" },
    { path: "engine/renderer/bsp/",       type: "Sub",    purpose: "BSP tree traversal, PVS lookup, surface rendering" },
    { path: "engine/physics/",            type: "Module", purpose: "IPhysicsWorld interface + BSP/AABB implementation" },
    { path: "engine/physics/aabb/",       type: "Sub",    purpose: "Phase 1-2: sweep tests, BSP clip, player movement" },
    { path: "engine/physics/jolt/",       type: "Sub",    purpose: "Phase 3+: JoltPhysics rigid body implementation" },
    { path: "engine/network/",            type: "Module", purpose: "Client/server architecture, delta compression, snapshots" },
    { path: "engine/audio/",             type: "Module", purpose: "IAudioSystem interface + OpenAL implementation" },
    { path: "engine/entities/",           type: "Module", purpose: "Entity list, component storage, spawn/think/touch callbacks" },
    { path: "engine/scripting/",          type: "Module", purpose: "IScriptEngine interface (Phase 3: Lua binding)" },
    { path: "game/",                      type: "Module", purpose: "Game DLL — implements IGameModule, isolated from engine" },
    { path: "game/src/",                  type: "Sub",    purpose: "Game entities, weapons, movement rules, game rules" },
    { path: "tools/",                     type: "Module", purpose: "Offline tools: BSP compiler, lightmap baker, VIS compiler" },
    { path: "tools/qbsp/",               type: "Sub",    purpose: "Compiles .map files to .bsp (brush geometry + BSP tree)" },
    { path: "tools/light/",              type: "Sub",    purpose: "Bakes lightmaps onto BSP surfaces (radiosity)" },
    { path: "tools/vis/",                type: "Sub",    purpose: "Computes PVS (Potentially Visible Set) data" },
    { path: "assets/",                    type: "Data",   purpose: "Maps, textures, models, sounds — not compiled into engine" },
    { path: "tests/",                     type: "Tests",  purpose: "Unit tests per module — one test file per system" },
    { path: "CMakeLists.txt",             type: "Build",  purpose: "Root CMake file — builds all modules and links them" },
    { path: "docs/ENGINE_PLAN.docx",      type: "Docs",   purpose: "Master plan document — always keep updated" },
];

// ── Phases ────────────────────────────────────────────────────
// Each phase has: number, name, timeline, goal, systems[], interfaces[], criteria[], notes[]
// Add/edit entries in the systems[] arrays to update the document.

const PHASES = [

    // ══════════════════════════════════════════════════════════
    //  PHASE 1 — FOUNDATION
    // ══════════════════════════════════════════════════════════
    {
        number:   1,
        name:     "Foundation",
        timeline: "Months 0–6",
        status:   "✅ DONE",

        goal: "Phase 1 produces a window with a BSP-rendered level visible on screen. No game logic, no networking, no audio. The sole goal is: open a window, load a .bsp file, render it with lightmaps using the OpenGL backend, and handle keyboard/mouse input. Everything built here must be abstracted behind interfaces.",

        systems: [
            { name: "Vec2 / Vec3 / Vec4",    file: "core/math/vec.h",                   status: "✅ DONE",        notes: "float-based with 16-byte alignment pad; dot, cross, normalize, lengthSq" },
            { name: "Mat4",                  file: "core/math/mat4.h",                   status: "✅ DONE",        notes: "Column-major; perspective, ortho, lookAt constructors present" },
            { name: "Quaternion",            file: "core/math/quat.h",                   status: "✅ DONE",        notes: "fromEuler, rotate, identity; used by Camera for yaw/pitch" },
            { name: "Plane / AABB / Ray",    file: "core/math/shapes.h",                 status: "⬜ TODO",        notes: "AABB and Ray stubs not yet implemented; needed for Phase 2 collision" },
            { name: "MemoryArena",           file: "core/memory/arena.cpp",               status: "✅ DONE",        notes: "Linear allocator with reset(); used for scratch allocations" },
            { name: "ZoneAllocator",         file: "core/memory/zone.cpp",                status: "✅ DONE",        notes: "Quake-style tagged blocks implemented" },
            { name: "PoolAllocator",         file: "core/memory/pool.cpp",                status: "✅ DONE",        notes: "Fixed-size block pool allocator implemented" },
            { name: "Logger",                file: "core/log.h / log.cpp",                status: "✅ DONE",        notes: "DEBUG/INFO/WARN/ERROR levels; file + stdout output; timestamped" },
            { name: "IPlatform interface",   file: "platform/iplatform.h",                status: "✅ DONE",        notes: "Window, input (512-key array + 3 mouse buttons), file I/O, time, threads" },
            { name: "SDL2Platform",          file: "platform/sdl2/sdl2_platform.cpp",     status: "✅ DONE",        notes: "FIXED: multi-key input now uses SDL_GetKeyboardState() snapshot — all keys held simultaneously work correctly" },
            { name: "InputState",            file: "platform/iplatform.h",                status: "✅ DONE",        notes: "keys[512], mouseButtons[3], mouseDeltaX/Y, mouseWheel — polled per frame" },
            { name: "IRenderBackend",        file: "renderer/irender_backend.h",          status: "✅ DONE",        notes: "Full interface: buffers, textures, samplers, shaders, draw calls, framebuffers" },
            { name: "OpenGLBackend",         file: "renderer/gl/gl_backend.cpp",          status: "✅ DONE",        notes: "GL 4.5 DSA; global VAO; 44-byte vertex layout; UBO binding; sampler objects" },
            { name: "ShaderProgram",         file: "renderer/gl/gl_backend.cpp",          status: "✅ DONE",        notes: "GLSL compile/link with error reporting; handles VS/FS/GS stages" },
            { name: "GpuBuffer",             file: "renderer/gl/gl_backend.cpp",          status: "✅ DONE",        notes: "VBO/IBO/UBO; glNamedBufferSubData for dynamic updates (DSA, no rebind)" },
            { name: "GpuTexture",            file: "renderer/gl/gl_backend.cpp",          status: "✅ DONE",        notes: "2D and cubemap; correct internal/base format derivation; sampler objects" },
            { name: "BSP Loader",            file: "renderer/bsp/bsp_loader.cpp",         status: "✅ DONE",        notes: "v38/v46 lump parsing, Q2→GL coord transform, 19/31 lump tables, crash-safe file-size guard, per-face extents, lightmap atlas pack, diffuse texture load (WAL/TGA/PNG). Full PVS lump read + cluster metadata." },
            { name: "BSP Renderer",          file: "renderer/bsp/bsp_loader.cpp",         status: "✅ DONE",        notes: "Chunk-based draw with frustum AABB cull + PVS cluster gate. Per-batch diffuse bind (tex slot 1) + single lightmap atlas (slot 0). render() takes cameraPos for leaf walk. Verified: 427 clusters / 54 bytes-per-row on spirit2dm2. 60-80% cull expected on typical Q2 indoor maps." },
            { name: "Lightmap System",       file: "renderer/bsp/bsp_loader.cpp",         status: "✅ DONE",        notes: "Single 4096×4096 RGB8 atlas shelf-packed from face extents. Per-face atlasX/Y/hasAtlas set at upload time. Lightmap UVs clamped to face region to prevent bilinear bleeding. Overbright ×2 decode in shader. Atlas mips capped at 4 levels." },
            { name: "BSP PVS Culling",       file: "renderer/bsp/bsp_loader.cpp",         status: "✅ DONE",        notes: "BSPMap::findLeaf() walks node tree in GL space. decompressPVS() RLE-decodes Q2 vis lump per cluster. Per-frame cluster cache skips re-decompress when camera stays in same cluster. DrawBatch carries clusterIndex; PVS gate in render() per batch. F3 key prints camLeaf/cluster to stdout." },
            { name: "Camera",                file: "core/camera.h / camera.cpp",          status: "✅ DONE",        notes: "Quaternion yaw/pitch; WASD movement (horizontal-plane locked); mouse look; getPosition() used for PVS leaf walk each frame" },
            { name: "Engine Main Loop",      file: "core/engine.cpp",                     status: "✅ DONE",        notes: "Fixed-timestep loop; FPS counter in title; BSP load + physics spawn; UBO update per frame; F1 mouse grab toggle; F2 debug-view cycle (lit/lm-gray/lm-boost/lm-uv); F3 PVS stats. EntityList + m_playerEntity wired; think(dt) called per frame; player origin synced from camera." },
        ],

        interfaces: [
            { iface: "IPlatform",      impls: "SDL2Platform",   desc: "Window creation, event polling, file system access, high-resolution timer, thread creation" },
            { iface: "IRenderBackend", impls: "OpenGLBackend",  desc: "Create/destroy GPU resources (buffers, textures, shaders), submit draw calls, present frame. NEVER call OpenGL directly outside this class." },
            { iface: "IFileSystem",    impls: "DefaultFileSystem", desc: "Open/read/write files; supports pack files (.pak) and loose files; abstracted for modding later" },
        ],

        criteria: [
            "✅ A 1280x720 window opens without errors",
            "✅ A Quake 2 .bsp file loads from disk and renders with correct lightmaps",
            "✅ Camera moves with WASD + mouse look at 60+ FPS",
            "✅ BSP PVS culling active — findLeaf() + decompressPVS() verified with F3 stats",
            "✅ All Phase 1 systems show status DONE in the table above",
            "✅ No raw OpenGL calls exist outside renderer/gl/ directory",
        ],
    },

    // ══════════════════════════════════════════════════════════
    //  PHASE 2 — QUAKE 2 CORE
    // ══════════════════════════════════════════════════════════
    {
        number:   2,
        name:     "Quake 2 Core",
        timeline: "Months 6–12",
        status:   "🔄 IN PROGRESS",

        goal: "Phase 2 transforms the renderer demo into a playable game prototype. By the end of Phase 2, a simple arena FPS can run: a player moves through a level, shoots projectiles, and the game logic lives in a separate DLL. Networking supports a basic client-server game.",

        systems: [
            { name: "IGameModule interface",  file: "engine/entities/igame_module.h",       status: "⬜ TODO",        notes: "Engine<->game contract: init, shutdown, think, onEntitySpawn, onCollision. Stub header exists; no implementation yet." },
            { name: "GameDLL loader",         file: "engine/entities/game_dll_loader.h/.cpp", status: "⬜ TODO",      notes: "dlopen/LoadLibrary; hot-reload on file change for fast iteration. Stub exists." },
            { name: "EntityID / EntityHandle",file: "engine/entities/entity_id.h",          status: "✅ DONE",        notes: "Generational index: 16-bit generation + 15-bit index packed into 32 bits. make(), index(), generation(), isValid(), isInvalid(). EntityRef for safe frame-scoped access. All tests pass." },
            { name: "EntityList",             file: "engine/entities/entity_list.h/.cpp",   status: "🔄 IN PROGRESS", notes: "Flat static arrays, kMaxEntities=1024, zero heap allocation. O(1) create/destroy via free-list stack. create() sets STATE_ALIVE; destroy() sets STATE_FREE. iterateActive, iterateByClassname, findByClassname, findInAABB, think(dt). tests/entities/test_entity.cpp and tests/physics/test_ground.cpp pass. EntityFactory + MapLoader integration still TODO." },
            { name: "Entity (base struct)",   file: "engine/entities/entity.h",             status: "🔄 IN PROGRESS", notes: "Full struct: origin, velocity, mins/maxs, angles, classname[32], EntityState (FREE/ALIVE/DEAD), think/touch/use/pain/die raw fn ptrs, nextThink, health, flags, handles. STATE_ALIVE set on create(); STATE_FREE set on destroy(). Test passing." },
            { name: "EntityFactory",          file: "entities/entity_factory.cpp",           status: "⬜ TODO",        notes: "Registry of classname -> spawn function; loaded from game DLL. Not yet started." },
            { name: "IPhysicsWorld interface",file: "engine/physics/iphysics_world.h",       status: "✅ DONE",        notes: "Pure virtual: setWorld, step, setOrigin/Velocity, getOrigin/Velocity, trace, traceEntity, moveSlide, isOnGround, getGroundEntity, getGroundElevation, setGravity/getGravity. TraceResult struct with fraction, endPos, normal, startSolid." },
            { name: "AABBPhysics",            file: "engine/physics/aabb_physics.h/.cpp",   status: "🔄 IN PROGRESS", notes: "setEntityStorage(Vec3* origin, Vec3* velocity, count) wires raw storage for physics. setPlayerBounds, testSolid (spawn escape), isOnGround, getGroundElevation, setGravity. Swept AABB vs BSP. moveSlide with 2-plane crease fix. test_ground.cpp passes. Full PM_StepSlideMove and water movement still TODO." },
            { name: "BSP Collision",          file: "physics/aabb/bsp_trace.cpp",           status: "⬜ TODO",        notes: "CM_BoxTrace against BSP planes; returns trace_t (fraction, normal, ent). Exists inside AABBPhysics but not broken out as standalone module yet." },
            { name: "PlayerMove",             file: "physics/aabb/player_move.cpp",          status: "⬜ TODO",        notes: "Full ground/air/water movement; wish velocity; friction; gravity; step-up. Currently handled partially inside AABBPhysics::moveSlide." },
            { name: "INetworkSystem interface",file: "network/inetwork.h",                  status: "⬜ TODO",        notes: "connect, disconnect, sendPacket, receivePackets — pure virtual" },
            { name: "Server",                 file: "network/server.cpp",                   status: "⬜ TODO",        notes: "Authoritative sim; builds delta snapshots; broadcasts to clients" },
            { name: "Client",                 file: "network/client.cpp",                   status: "⬜ TODO",        notes: "Sends input; receives snapshots; client-side prediction + interpolation" },
            { name: "DeltaCompressor",        file: "network/delta.cpp",                    status: "⬜ TODO",        notes: "Diff two entity snapshots; encode changed fields only (bit flags)" },
            { name: "PacketBuffer",           file: "network/packet.cpp",                   status: "⬜ TODO",        notes: "Reliable + unreliable channels; sequence numbers; ack tracking" },
            { name: "IAudioSystem interface", file: "audio/iaudio.h",                       status: "⬜ TODO",        notes: "loadSound, playSound, play3D, stopAll — pure virtual" },
            { name: "OpenALAudio",            file: "audio/openal/openal_audio.cpp",        status: "⬜ TODO",        notes: "3D positional audio; WAV/OGG playback; distance attenuation" },
            { name: "ConsoleVar (cvar)",      file: "engine/cvar.cpp",                      status: "⬜ TODO",        notes: "Runtime variables (sv_gravity, cl_fov etc.); serialized to config.cfg" },
            { name: "Console",               file: "engine/console.cpp",                   status: "⬜ TODO",        notes: "In-game drop-down console; command parsing; cvar get/set" },
            { name: "MapLoader (.bsp spawn)", file: "entities/map_loader.cpp",              status: "⬜ TODO",        notes: "Parse entity lump from .bsp; call EntityFactory to spawn all entities" },
            { name: "Model Renderer (MD2)",   file: "renderer/models/md2.cpp",              status: "⬜ TODO",        notes: "Load Quake 2 .md2 vertex-animated models; lerp between frames" },
            { name: "Sprite Renderer",        file: "renderer/sprite.cpp",                  status: "⬜ TODO",        notes: "Billboard sprites for particles, explosions, pickups" },
            { name: "Particle System (basic)",file: "renderer/particles/particles.cpp",     status: "⬜ TODO",        notes: "CPU-simulated particles: spawn, update, fade, billboard render" },
            { name: "HUD / 2D Renderer",      file: "renderer/hud/hud.cpp",                 status: "⬜ TODO",        notes: "Orthographic quads for health bar, ammo counter, crosshair" },
        ],

        // Optional: code blocks shown as-is (array of lines)
        codeBlocks: [
            {
                title: "Networking Architecture — Server/Client Data Flow Per Tick",
                lines: [
                    "SERVER TICK (every 50ms / 20Hz):",
                    "  1. Receive input packets from all clients",
                    "  2. Apply inputs to each client's player entity",
                    "  3. Run IGameModule::think(dt) — game logic, AI, projectiles",
                    "  4. Run IPhysicsWorld::step(dt) — move all entities, resolve collisions",
                    "  5. For each client: build snapshot (all entity states)",
                    "  6. Delta compress snapshot against client's last ack'd snapshot",
                    "  7. Send compressed delta packet to client",
                    "",
                    "CLIENT TICK (every 16ms / 60Hz):",
                    "  1. Read local input (keyboard/mouse)",
                    "  2. Store input in history buffer (for reconciliation)",
                    "  3. Predict local player movement immediately (client-side prediction)",
                    "  4. Send input packet to server",
                    "  5. Receive delta snapshot from server",
                    "  6. Apply delta to entity state buffer",
                    "  7. Reconcile: re-apply unack'd inputs over server state",
                    "  8. Interpolate remote entities between two buffered snapshots",
                    "  9. Render interpolated world state",
                ],
            },
        ],

        criteria: [
            "Player can load a map, spawn, move (walk/jump/swim), and shoot a projectile",
            "Game logic (damage, pickups, doors) lives entirely in game.dll with zero engine changes",
            "Two clients can connect to a local server and see each other moving",
            "Console opens with tilde (~), cvars are readable and settable",
            "Audio plays 3D sounds attached to entities",
            "All Phase 2 systems show DONE in the table above",
        ],
    },

    // ══════════════════════════════════════════════════════════
    //  PHASE 3 — SOURCE ENGINE BRIDGE
    // ══════════════════════════════════════════════════════════
    {
        number:   3,
        name:     "Source Engine Bridge",
        timeline: "Months 12–24",
        status:   "⬜ TODO",

        goal: "Phase 3 upgrades visual and gameplay capabilities to Source-engine parity. Crucially, none of Phase 1 or Phase 2 code is discarded — it is extended through the existing interfaces. The renderer gains normal maps, HDR, and shadow mapping. Physics swaps to Jolt. Lua scripting is embedded.",

        systems: [
            { name: "Normal Map Support",        file: "renderer/gl/gl_normalmap.cpp",           status: "⬜ TODO", notes: "Tangent-space normal maps; TBN matrix per vertex; add to BSP surfaces" },
            { name: "Specular / Roughness Maps", file: "renderer/materials/material.cpp",        status: "⬜ TODO", notes: "PBR-lite: albedo + normal + roughness + metallic (4 textures per surface)" },
            { name: "HDR Framebuffer",           file: "renderer/gl/gl_hdr.cpp",                 status: "⬜ TODO", notes: "Render to float16 FBO; tone-map (Reinhard/ACES) to LDR for display" },
            { name: "Shadow Mapping",            file: "renderer/shadows/shadowmap.cpp",         status: "⬜ TODO", notes: "PCF shadow maps for sun/spot lights; cascade for large outdoor maps" },
            { name: "Bloom Post-Process",        file: "renderer/post/bloom.cpp",                status: "⬜ TODO", notes: "Downsample bright areas; Gaussian blur; additive blend over scene" },
            { name: "Displacement Geometry",     file: "renderer/bsp/displacement.cpp",          status: "⬜ TODO", notes: "Subdivide BSP faces into displacement grid; edit height in map editor" },
            { name: "Displacement Compiler",     file: "tools/qbsp/displacement.cpp",            status: "⬜ TODO", notes: "Compile displacement data into .bsp; store LOD levels" },
            { name: "IPhysicsWorld (Jolt)",      file: "physics/jolt/jolt_world.cpp",            status: "⬜ TODO", notes: "Swap AABBPhysics -> JoltPhysics behind IPhysicsWorld; keep interface" },
            { name: "Rigid Body Component",      file: "physics/jolt/rigid_body.cpp",            status: "⬜ TODO", notes: "Physics-driven entities: barrels, crates, ragdolls" },
            { name: "IScriptEngine interface",   file: "scripting/iscript_engine.h",             status: "⬜ TODO", notes: "loadScript, callFunction, exposeObject — pure virtual" },
            { name: "LuaScriptEngine",           file: "scripting/lua/lua_engine.cpp",           status: "⬜ TODO", notes: "Embed Lua 5.4; bind Entity, World, Console, Events via sol2 or manual" },
            { name: "Script Entity Bindings",    file: "scripting/lua/bindings.cpp",             status: "⬜ TODO", notes: "Expose entity.origin, entity.think, world.trace() etc. to Lua" },
            { name: "Decal System",              file: "renderer/decals/decal.cpp",              status: "⬜ TODO", notes: "Projected decals on BSP surfaces: bullet holes, scorch marks, blood" },
            { name: "Dynamic Lights",            file: "renderer/lights/dyn_light.cpp",          status: "⬜ TODO", notes: "Up to 256 point/spot lights per frame; frustum-culled; affect models" },
            { name: "Env Cubemap / Reflections", file: "renderer/env/cubemap.cpp",               status: "⬜ TODO", notes: "Per-zone env_cubemap; sample in material shader for reflections" },
            { name: "Enhanced Particle System",  file: "renderer/particles/gpu_particles.cpp",   status: "⬜ TODO", notes: "GPU-simulated particles using transform feedback or compute shader" },
            { name: "Skeletal Animation (SMD)",  file: "renderer/models/skeletal.cpp",           status: "⬜ TODO", notes: "Bone-weighted skinning; load Valve SMD or GLTF; blend between clips" },
            { name: "Animation State Machine",   file: "renderer/models/anim_state.cpp",         status: "⬜ TODO", notes: "States + transitions + blend weights; driven by game logic or Lua" },
            { name: "Sound Occlusion",           file: "audio/occlusion.cpp",                    status: "⬜ TODO", notes: "Raycast through BSP to attenuate occluded sounds (low-pass filter)" },
            { name: "Reverb Zones",              file: "audio/reverb.cpp",                       status: "⬜ TODO", notes: "Map-placed reverb volumes (cave, tunnel, outdoor) via OpenAL EFX" },
        ],

        codeBlocks: [
            {
                title: "Renderer Upgrade Path",
                lines: [
                    "Phase 1 Render Pipeline:",
                    "  BSP surfaces → lightmap shader → display",
                    "",
                    "Phase 2 Additions:",
                    "  + MD2 vertex-animated models",
                    "  + Billboard sprites / particles",
                    "  + HUD overlay (2D ortho pass)",
                    "",
                    "Phase 3 Additions:",
                    "  + Normal/specular maps in surface shader",
                    "  + Shadow map pre-pass (depth only, per light)",
                    "  + HDR framebuffer (scene renders to float16)",
                    "  + Post-process stack: bloom → tone-map → gamma → display",
                    "  + Dynamic lights injected into surface shader",
                    "  + Displacement mesh rendering",
                ],
            },
        ],

        criteria: [
            "A level with normal maps, dynamic lights, and shadow maps renders correctly",
            "A physics prop (crate) can be pushed around using Jolt rigid body",
            "A Lua script can spawn an entity, set its think function, and react to player proximity",
            "Particle effects (smoke, sparks) emit from entities at runtime",
            "Skeletal model plays and blends idle/walk/attack animations",
        ],
    },

    // ══════════════════════════════════════════════════════════
    //  PHASE 4 — MODERN ENGINE
    // ══════════════════════════════════════════════════════════
    {
        number:   4,
        name:     "Modern Engine",
        timeline: "Months 24+",
        status:   "⬜ TODO",

        goal: "Phase 4 is the long-term evolution. The engine becomes Vulkan-based, PBR-native, and includes a level editor. This phase has no hard deadline — systems are added incrementally. Phase 3 must be fully complete before Phase 4 begins.",

        systems: [
            { name: "VulkanBackend",              file: "renderer/vulkan/vk_backend.cpp",    status: "⬜ TODO", notes: "Swap OpenGLBackend -> VulkanBackend behind IRenderBackend; keep API" },
            { name: "Vulkan Resource Manager",    file: "renderer/vulkan/vk_resources.cpp",  status: "⬜ TODO", notes: "Descriptor sets, render passes, pipeline cache, memory allocator" },
            { name: "GPU-Driven Rendering",       file: "renderer/vulkan/gpu_culling.cpp",   status: "⬜ TODO", notes: "Indirect draw calls; GPU frustum + occlusion cull; no CPU per-object" },
            { name: "PBR Material System",        file: "renderer/materials/pbr.cpp",        status: "⬜ TODO", notes: "Full PBR: albedo, normal, roughness, metallic, AO, emissive maps" },
            { name: "Screen-Space Reflections",   file: "renderer/post/ssr.cpp",             status: "⬜ TODO", notes: "Raymarched SSR in screen space; fallback to cubemaps" },
            { name: "Ambient Occlusion (SSAO)",   file: "renderer/post/ssao.cpp",            status: "⬜ TODO", notes: "HBAO+ style; 16 samples; blur; modulate diffuse lighting" },
            { name: "Global Illumination",        file: "renderer/gi/gi.cpp",                status: "⬜ TODO", notes: "Lightprobes / irradiance volumes; baked or dynamic (Lumen-style later)" },
            { name: "Level Editor (Nova Edit)",   file: "editor/",                           status: "⬜ TODO", notes: "Hammer-like editor: brush drawing, entity placement, compile pipeline" },
            { name: "Editor Renderer",            file: "editor/editor_renderer.cpp",        status: "⬜ TODO", notes: "Grid, wireframe overlay, selection highlight, gizmos (translate/rotate)" },
            { name: "Temporal Anti-Aliasing",     file: "renderer/post/taa.cpp",             status: "⬜ TODO", notes: "Accumulate samples across frames; jitter projection; motion vectors" },
            { name: "Streaming / LOD System",     file: "engine/streaming/lod.cpp",          status: "⬜ TODO", notes: "LOD chain per model; distance-based swap; async streaming from disk" },
            { name: "Async Asset Pipeline",       file: "engine/assets/asset_manager.cpp",   status: "⬜ TODO", notes: "Background thread loads textures/models; main thread gets handles" },
            { name: "Profiler / RenderDoc Hook",  file: "engine/profiler/profiler.cpp",      status: "⬜ TODO", notes: "CPU/GPU timers per pass; overlay UI; integrate RenderDoc capture API" },
        ],

        criteria: [
            "VulkanBackend passes all tests that OpenGLBackend previously passed",
            "A full PBR scene renders with SSR, SSAO, and TAA at 60 FPS on mid-range GPU",
            "Level editor opens a .bsp, allows brush editing, and recompiles in-editor",
            "Async asset loading shows no frame hitches when moving through a large level",
        ],
    },
];

// ── Coding Conventions ────────────────────────────────────────
const CONVENTIONS = {
    naming: [
        "Classes / Structs:    PascalCase          e.g. RenderBackend, EntityList",
        "Interfaces:           IPascalCase         e.g. IRenderBackend, IPhysicsWorld",
        "Functions:            camelCase           e.g. loadShader(), stepPhysics()",
        "Member variables:     m_camelCase         e.g. m_vertexBuffer, m_entityCount",
        "Constants / Enums:    UPPER_SNAKE_CASE    e.g. MAX_ENTITIES, BSP_VERSION",
        "Files:                snake_case.cpp/.h   e.g. bsp_loader.cpp, gl_texture.h",
    ],
    includeRules: [
        "No module may include headers from a module that depends on it (no circular deps)",
        "Engine modules never include game/ headers — only game/ includes engine/",
        "renderer/ never includes physics/ or network/ — pass data through structs",
        "Use forward declarations in .h files; #include in .cpp files only",
    ],
    memoryRules: [
        "No naked new/delete in engine code — use allocators from core/memory/",
        "All per-frame allocations use MemoryArena and are reset at frame end",
        "All persistent game objects use ZoneAllocator or PoolAllocator",
        "Render resources (buffers, textures) managed exclusively by IRenderBackend",
    ],
    interfaceRules: [
        "Every swappable system must have a pure virtual interface class prefixed with I",
        "No code outside a module's own directory calls concrete implementation classes",
        "Interface methods return error codes (enum Result), not throw exceptions",
        "All interface destructors are virtual",
    ],
    fileHeaderTemplate: [
        "// ============================================================",
        "// FILE:    engine/renderer/bsp/bsp_loader.cpp",
        "// MODULE:  Renderer > BSP",
        "// PHASE:   1",
        "// STATUS:  TODO | IN_PROGRESS | DONE",
        "// PURPOSE: Loads Quake 2 .bsp files into the BSPMap struct.",
        "//          Parses all lumps: vertices, faces, edges, texinfo,",
        "//          lightmaps, entities, PVS data.",
        "// DEPENDS: core/math, core/memory, platform/IFileSystem",
        "// ============================================================",
    ],
};

// ── Build System ──────────────────────────────────────────────
const BUILD = {
    cmakeStructure: [
        "CMakeLists.txt              # Root: sets C++20, warnings, platform flags",
        "engine/CMakeLists.txt       # Builds nova_engine static lib",
        "game/CMakeLists.txt         # Builds game shared lib (nova_game.dll/.so)",
        "tools/CMakeLists.txt        # Builds qbsp, light, vis executables",
        "tests/CMakeLists.txt        # Builds all test executables via CTest",
    ],
    dependencies: [
        { name: "SDL2",        file: "platform/sdl2/",       status: "⬜ TODO", notes: "v2.28+; window, input, OpenGL context, file I/O wrappers" },
        { name: "OpenGL 4.5",  file: "renderer/gl/",         status: "⬜ TODO", notes: "Via GLAD loader; 4.5 core profile; DSA (direct state access) for clarity" },
        { name: "GLAD",        file: "vendor/glad/",         status: "⬜ TODO", notes: "OpenGL function loader; generate for GL 4.5 + extensions needed" },
        { name: "GLM",         file: "vendor/glm/",          status: "⬜ TODO", notes: "Header-only math; used only in tool scripts — engine uses its own math" },
        { name: "OpenAL Soft", file: "audio/openal/",        status: "⬜ TODO", notes: "v1.23+; EFX extension for reverb; cross-platform" },
        { name: "stb_image",   file: "vendor/stb/",          status: "⬜ TODO", notes: "Single-header image loader for PNG/JPG/TGA texture loading" },
        { name: "Jolt Physics",file: "physics/jolt/",        status: "⬜ TODO", notes: "Phase 3+; MIT license; excellent performance; replace AABBPhysics" },
        { name: "Lua 5.4",     file: "scripting/lua/",       status: "⬜ TODO", notes: "Phase 3+; embed as static lib; bind via sol2 or manual stack API" },
        { name: "Vulkan SDK",  file: "renderer/vulkan/",     status: "⬜ TODO", notes: "Phase 4+; VMA for memory allocation; vkbootstrap for init" },
        { name: "Catch2",      file: "vendor/catch2/",       status: "⬜ TODO", notes: "Unit test framework; one test executable per module" },
    ],
};

// ── Tests ─────────────────────────────────────────────────────
const TESTS = [
    { name: "tests/core/test_math.cpp",       file: "Tests: Vec3, Mat4, Quat, AABB",                           status: "⬜ TODO",        notes: "Test dot/cross/normalize, mat4 mul, frustum planes, AABB overlap" },
    { name: "tests/core/test_memory.cpp",     file: "Tests: Arena, Zone, Pool",                                status: "⬜ TODO",        notes: "Alloc/free/reset; out-of-memory handling; alignment checks" },
    { name: "tests/renderer/test_bsp.cpp",    file: "Tests: BSP load + PVS",                                   status: "⬜ TODO",        notes: "Load test.bsp; verify leaf count, PVS bits, surface counts" },
    { name: "tests/physics/test_ground.cpp",  file: "Tests: ground, gravity, trace, entity storage",           status: "🔄 IN PROGRESS", notes: "isOnGround (no BSP), getGroundElevation, gravity default/set, player bounds, trace fraction=1 with no BSP, entity origin/velocity set+get. All passing." },
    { name: "tests/entities/test_entity.cpp", file: "Tests: EntityID, EntityHandle, EntityList, Entity struct",status: "🔄 IN PROGRESS", notes: "EntityID make/index/gen/valid; EntityHandle null/valid; EntityList create/destroy/count/get/classname; Entity state/origin/velocity/flags. All passing." },
    { name: "tests/physics/test_trace.cpp",   file: "Tests: BSP trace, AABB sweep",                            status: "⬜ TODO",        notes: "Trace against known geometry; verify fraction, normal, hit entity" },
    { name: "tests/network/test_delta.cpp",   file: "Tests: Delta compress/decompress",                        status: "⬜ TODO",        notes: "Encode two snapshots; decode; verify byte-for-byte equality" },
    { name: "tests/entities/test_elist.cpp",  file: "Tests: EntityList stress",                                status: "⬜ TODO",        notes: "Create 1000 entities; destroy random subset; verify handles and count" },
];

// ── Quick Reference ───────────────────────────────────────────
// "What to do next" steps — update as work progresses.
const QUICK_REFERENCE = {
    freshStart: [
        "Step 1: Set up CMake project structure and empty module directories",
        "Step 2: Implement core/math — Vec3, Mat4, Plane, AABB, Ray",
        "Step 3: Implement core/memory — Arena, Zone, Pool allocators",
        "Step 4: Implement Logger",
        "Step 5: Define IPlatform interface; implement SDL2Platform (window + input only)",
        "Step 6: Define IRenderBackend interface",
        "Step 7: Implement OpenGLBackend (clear screen, triangle on screen)",
        "Step 8: Implement BSP Loader",
        "Step 9: Implement BSP Renderer + Lightmap System",
        "Step 10: Implement Camera — you now have Phase 1",
    ],
    phase2InProgress: [
        "Step 1 (DONE): EntityID / EntityHandle / EntityList / Entity struct / IPhysicsWorld / AABBPhysics foundation",
        "Step 2 (NEXT): Implement EntityFactory — classname registry loaded from game DLL",
        "Step 3: Implement MapLoader — parse BSP entity lump, call EntityFactory to spawn all entities",
        "Step 4: Implement full PM_StepSlideMove (step-up, water movement) in AABBPhysics",
        "Step 5: Define IGameModule; build GameDLL loader (dlopen/LoadLibrary + hot-reload)",
        "Step 6: Implement Server + Client + DeltaCompressor + PacketBuffer",
        "Step 7: Implement OpenALAudio behind IAudioSystem",
        "Step 8: Implement cvar system + Console",
        "Step 9: Implement MD2 model renderer",
        "Step 10: Implement HUD / 2D renderer — Phase 2 complete",
    ],
    startingPhase3: [
        "Step 1: Upgrade surface shader with normal map + specular support",
        "Step 2: Add HDR framebuffer + tone mapping post-process",
        "Step 3: Add shadow map pre-pass for sun light",
        "Step 4: Implement displacement geometry in BSP renderer",
        "Step 5: Swap AABBPhysics for JoltPhysics behind IPhysicsWorld",
        "Step 6: Embed Lua 5.4; define IScriptEngine; bind entity API",
        "Step 7: Add skeletal animation pipeline",
    ],
};

// ── Changelog ─────────────────────────────────────────────────
// Add a new entry here every time you regenerate the document.
const CHANGELOG = [
    {
        rev:    "1.0",
        date:   "TBD",
        author: "Project Lead",
        changes: "Initial document creation — all four phases defined, all systems marked TODO",
    },
    {
        rev:    "1.1",
        date:   "2026-04-27",
        author: "Ahmed (solo dev)",
        changes: "Phase 1 COMPLETE. BSP PVS culling implemented and verified: findLeaf(), decompressPVS(), per-batch cluster gate, PVS cache. BSP Renderer, Lightmap System, and BSP PVS Culling all marked DONE. Verified on spirit2dm2.bsp: 427 clusters, 54 bytes/row, leaf walk tracking correctly across full map traversal. Phase summary updated to DONE.",
    },
    {
        rev:    "1.2",
        date:   "2026-04-27",
        author: "Ahmed (solo dev)",
        changes: "Phase 2 IN PROGRESS. Entity system foundation complete: EntityID/EntityHandle DONE; EntityList flat arrays kMaxEntities=1024 no heap allocation IN PROGRESS; Entity struct STATE_ALIVE/FREE lifecycle IN PROGRESS; IPhysicsWorld interface DONE; AABBPhysics setEntityStorage/testSolid/isOnGround/moveSlide IN PROGRESS. Engine wired: m_entityList + m_playerEntity added to Engine class, think(dt) called each frame, player origin synced from camera each tick, sizeof(Entity) printed on startup. tests/entities/test_entity.cpp and tests/physics/test_ground.cpp both pass. Phase 2 summary updated to IN PROGRESS. Quick reference updated with current Phase 2 step order.",
    },
];

// ── Exports ───────────────────────────────────────────────────
module.exports = {
    META,
    PHASE_SUMMARY,
    DIRECTORY_STRUCTURE,
    PHASES,
    CONVENTIONS,
    BUILD,
    TESTS,
    QUICK_REFERENCE,
    CHANGELOG,
};
