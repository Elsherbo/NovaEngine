
========================================================================
  NOVA ENGINE — Game Engine Development Master Plan
========================================================================
  Quake 2 Foundation  →  Source Engine Capabilities
  Revision 1.3  |  Start: TBD  |  Codename: NOVA
========================================================================

  HOW TO USE THIS DOCUMENT
  ─────────────────────────
  • Edit ENGINE_PLAN_DATA.js to change ANY content (statuses, notes, etc.)
  • Run: node generate-md.js   →  regenerates this file
  • Run: node generate.js      →  regenerates NOVA_ENGINE_PLAN.docx
  • Run: node changelog.js     →  adds a changelog entry interactively
  • Paste this file into an AI chat for full project context.


────────────────────────────────────────────────────────────────────────
1. STATUS LEGEND
────────────────────────────────────────────────────────────────────────

  DONE        = Fully implemented, tested, stable.
  IN PROGRESS = Work started, partially complete.
  TODO        = Planned, not yet started.
  MISSING     = Required but not yet designed/planned.


────────────────────────────────────────────────────────────────────────
2. PHASE ROADMAP
────────────────────────────────────────────────────────────────────────

+---------+------------------+----------------+----------------------------------------------------+---------------+
| Phase   | Name             | Timeline       | Key Deliverables                                   | Status        |
+=========+==================+================+====================================================+===============+
| Phase 1 | Foundation       | 0–6 months     | Math, memory, platform, OpenGL renderer, BSP load… | DONE          |
| Phase 2 | Quake 2 Core     | 6–12 months    | Entity system, AABB physics, delta networking, au… | IN PROGRESS   |
| Phase 3 | Source Bridge    | 12–24 months   | Normal maps, HDR, displacement geo, Jolt physics,… | TODO          |
| Phase 4 | Modern Engine    | 24+ months     | Vulkan backend, PBR, GPU-driven rendering, editor… | TODO          |
+---------+------------------+----------------+----------------------------------------------------+---------------+

────────────────────────────────────────────────────────────────────────
3. DIRECTORY STRUCTURE
────────────────────────────────────────────────────────────────────────

+--------------------------------+----------+------------------------------------------+
| Path                           | Type     | Purpose                                  |
+================================+==========+==========================================+
| engine/                        | Root     | Engine source root — all engine code     |
|                                |          | lives here                               |
+--------------------------------+----------+------------------------------------------+
| engine/core/                   | Module   | Math, memory allocators, string utils,   |
|                                |          | containers, logging                      |
+--------------------------------+----------+------------------------------------------+
| engine/core/math/              | Sub      | Vec2/3/4, Mat4, Quaternion, Plane, AABB, |
|                                |          | Ray structs                              |
+--------------------------------+----------+------------------------------------------+
| engine/core/memory/            | Sub      | Zone allocator, arena allocator, pool    |
|                                |          | allocator                                |
+--------------------------------+----------+------------------------------------------+
| engine/core/containers/        | Sub      | Array, HashMap, StringView — no STL in   |
|                                |          | hot paths                                |
+--------------------------------+----------+------------------------------------------+
| engine/platform/               | Module   | IPlatform interface + SDL2               |
|                                |          | implementation                           |
+--------------------------------+----------+------------------------------------------+
| engine/platform/sdl2/          | Sub      | SDL2Platform: window, input, file I/O,   |
|                                |          | threading                                |
+--------------------------------+----------+------------------------------------------+
| engine/renderer/               | Module   | IRenderBackend interface + all rendering |
|                                |          | code                                     |
+--------------------------------+----------+------------------------------------------+
| engine/renderer/gl/            | Sub      | OpenGLBackend: shaders, buffers,         |
|                                |          | textures, draw calls                     |
+--------------------------------+----------+------------------------------------------+
| engine/renderer/vulkan/        | Sub      | VulkanBackend (Phase 4) — not yet        |
|                                |          | started                                  |
+--------------------------------+----------+------------------------------------------+
| engine/renderer/bsp/           | Sub      | BSP tree traversal, PVS lookup, surface  |
|                                |          | rendering                                |
+--------------------------------+----------+------------------------------------------+
| engine/physics/                | Module   | IPhysicsWorld interface + BSP/AABB       |
|                                |          | implementation                           |
+--------------------------------+----------+------------------------------------------+
| engine/physics/aabb/           | Sub      | Phase 1-2: sweep tests, BSP clip, player |
|                                |          | movement                                 |
+--------------------------------+----------+------------------------------------------+
| engine/physics/jolt/           | Sub      | Phase 3+: JoltPhysics rigid body         |
|                                |          | implementation                           |
+--------------------------------+----------+------------------------------------------+
| engine/network/                | Module   | Client/server architecture, delta        |
|                                |          | compression, snapshots                   |
+--------------------------------+----------+------------------------------------------+
| engine/audio/                  | Module   | IAudioSystem interface + OpenAL          |
|                                |          | implementation                           |
+--------------------------------+----------+------------------------------------------+
| engine/entities/               | Module   | Entity list, component storage,          |
|                                |          | spawn/think/touch callbacks              |
+--------------------------------+----------+------------------------------------------+
| engine/scripting/              | Module   | IScriptEngine interface (Phase 3: Lua    |
|                                |          | binding)                                 |
+--------------------------------+----------+------------------------------------------+
| game/                          | Module   | Game DLL — implements IGameModule,       |
|                                |          | isolated from engine                     |
+--------------------------------+----------+------------------------------------------+
| game/src/                      | Sub      | Game entities, weapons, movement rules,  |
|                                |          | game rules                               |
+--------------------------------+----------+------------------------------------------+
| tools/                         | Module   | Offline tools: BSP compiler, lightmap    |
|                                |          | baker, VIS compiler                      |
+--------------------------------+----------+------------------------------------------+
| tools/qbsp/                    | Sub      | Compiles .map files to .bsp (brush       |
|                                |          | geometry + BSP tree)                     |
+--------------------------------+----------+------------------------------------------+
| tools/light/                   | Sub      | Bakes lightmaps onto BSP surfaces        |
|                                |          | (radiosity)                              |
+--------------------------------+----------+------------------------------------------+
| tools/vis/                     | Sub      | Computes PVS (Potentially Visible Set)   |
|                                |          | data                                     |
+--------------------------------+----------+------------------------------------------+
| assets/                        | Data     | Maps, textures, models, sounds — not     |
|                                |          | compiled into engine                     |
+--------------------------------+----------+------------------------------------------+
| tests/                         | Tests    | Unit tests per module — one test file    |
|                                |          | per system                               |
+--------------------------------+----------+------------------------------------------+
| CMakeLists.txt                 | Build    | Root CMake file — builds all modules and |
|                                |          | links them                               |
+--------------------------------+----------+------------------------------------------+
| docs/ENGINE_PLAN.docx          | Docs     | Master plan document — always keep       |
|                                |          | updated                                  |
+--------------------------------+----------+------------------------------------------+

────────────────────────────────────────────────────────────────────────
4. PHASE 1 — FOUNDATION (MONTHS 0–6) [DONE]
────────────────────────────────────────────────────────────────────────

  GOAL:
  Phase 1 produces a window with a BSP-rendered level visible on
  screen. No game logic, no networking, no audio. The sole goal is:
  open a window, load a .bsp file, render it with lightmaps using the
  OpenGL backend, and handle keyboard/mouse input. Everything built
  here must be abstracted behind interfaces.


  >> 4.1 System Status
  ----------------------

+--------------------------+----------------------------------+---------------+------------------------------------------+
| System / Class           | Source File(s)                   | Status        | Notes / Next Steps                       |
+==========================+==================================+===============+==========================================+
| Vec2 / Vec3 / Vec4       | core/math/vec.h                  | DONE          | float-based with 16-byte alignment pad;  |
|                          |                                  |               | dot, cross, normalize, lengthSq          |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Mat4                     | core/math/mat4.h                 | DONE          | Column-major; perspective, ortho, lookAt |
|                          |                                  |               | constructors present                     |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Quaternion               | core/math/quat.h                 | DONE          | fromEuler, rotate, identity; used by     |
|                          |                                  |               | Camera for yaw/pitch                     |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Plane / AABB / Ray       | core/math/shapes.h               | TODO          | AABB and Ray stubs not yet implemented;  |
|                          |                                  |               | needed for Phase 2 collision             |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| MemoryArena              | core/memory/arena.cpp            | DONE          | Linear allocator with reset(); used for  |
|                          |                                  |               | scratch allocations                      |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| ZoneAllocator            | core/memory/zone.cpp             | DONE          | Quake-style tagged blocks implemented    |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| PoolAllocator            | core/memory/pool.cpp             | DONE          | Fixed-size block pool allocator          |
|                          |                                  |               | implemented                              |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Logger                   | core/log.h / log.cpp             | DONE          | DEBUG/INFO/WARN/ERROR levels; file +     |
|                          |                                  |               | stdout output; timestamped               |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| IPlatform interface      | platform/iplatform.h             | DONE          | Window, input (512-key array + 3 mouse   |
|                          |                                  |               | buttons), file I/O, time, threads        |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| SDL2Platform             | platform/sdl2/sdl2_platform.cpp  | DONE          | FIXED: multi-key input now uses          |
|                          |                                  |               | SDL_GetKeyboardState() snapshot — all    |
|                          |                                  |               | keys held simultaneously work correctly  |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| InputState               | platform/iplatform.h             | DONE          | keys[512], mouseButtons[3],              |
|                          |                                  |               | mouseDeltaX/Y, mouseWheel — polled per   |
|                          |                                  |               | frame                                    |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| IRenderBackend           | renderer/irender_backend.h       | DONE          | Full interface: buffers, textures,       |
|                          |                                  |               | samplers, shaders, draw calls,           |
|                          |                                  |               | framebuffers                             |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| OpenGLBackend            | renderer/gl/gl_backend.cpp       | DONE          | GL 4.5 DSA; global VAO; 44-byte vertex   |
|                          |                                  |               | layout; UBO binding; sampler objects     |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| ShaderProgram            | renderer/gl/gl_backend.cpp       | DONE          | GLSL compile/link with error reporting;  |
|                          |                                  |               | handles VS/FS/GS stages                  |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| GpuBuffer                | renderer/gl/gl_backend.cpp       | DONE          | VBO/IBO/UBO; glNamedBufferSubData for    |
|                          |                                  |               | dynamic updates (DSA, no rebind)         |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| GpuTexture               | renderer/gl/gl_backend.cpp       | DONE          | 2D and cubemap; correct internal/base    |
|                          |                                  |               | format derivation; sampler objects       |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| BSP Loader               | renderer/bsp/bsp_loader.cpp      | DONE          | v38/v46 lump parsing, Q2→GL coord        |
|                          |                                  |               | transform, 19/31 lump tables, crash-safe |
|                          |                                  |               | file-size guard, per-face extents,       |
|                          |                                  |               | lightmap atlas pack, diffuse texture     |
|                          |                                  |               | load (WAL/TGA/PNG). Full PVS lump read + |
|                          |                                  |               | cluster metadata.                        |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| BSP Renderer             | renderer/bsp/bsp_loader.cpp      | DONE          | Chunk-based draw with frustum AABB cull  |
|                          |                                  |               | + PVS cluster gate. Per-batch diffuse    |
|                          |                                  |               | bind (tex slot 1) + single lightmap      |
|                          |                                  |               | atlas (slot 0). render() takes cameraPos |
|                          |                                  |               | for leaf walk. Verified: 427 clusters /  |
|                          |                                  |               | 54 bytes-per-row on spirit2dm2. 60-80%   |
|                          |                                  |               | cull expected on typical Q2 indoor maps. |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Lightmap System          | renderer/bsp/bsp_loader.cpp      | DONE          | Single 4096×4096 RGB8 atlas shelf-packed |
|                          |                                  |               | from face extents. Per-face              |
|                          |                                  |               | atlasX/Y/hasAtlas set at upload time.    |
|                          |                                  |               | Lightmap UVs clamped to face region to   |
|                          |                                  |               | prevent bilinear bleeding. Overbright ×2 |
|                          |                                  |               | decode in shader. Atlas mips capped at 4 |
|                          |                                  |               | levels.                                  |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| BSP PVS Culling          | renderer/bsp/bsp_loader.cpp      | DONE          | BSPMap::findLeaf() walks node tree in GL |
|                          |                                  |               | space. decompressPVS() RLE-decodes Q2    |
|                          |                                  |               | vis lump per cluster. Per-frame cluster  |
|                          |                                  |               | cache skips re-decompress when camera    |
|                          |                                  |               | stays in same cluster. DrawBatch carries |
|                          |                                  |               | clusterIndex; PVS gate in render() per   |
|                          |                                  |               | batch. F3 key prints camLeaf/cluster to  |
|                          |                                  |               | stdout.                                  |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Camera                   | core/camera.h / camera.cpp       | DONE          | Quaternion yaw/pitch; WASD movement      |
|                          |                                  |               | (horizontal-plane locked); mouse look;   |
|                          |                                  |               | getPosition() used for PVS leaf walk     |
|                          |                                  |               | each frame                               |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Engine Main Loop         | core/engine.cpp                  | DONE          | Fixed-timestep loop; FPS counter in      |
|                          |                                  |               | title. Per-frame: mouse look, entity     |
|                          |                                  |               | think (func_plat moves + carryEntities), |
|                          |                                  |               | dynamic collider rebuild,                |
|                          |                                  |               | PlayerController physics + moveSlide,    |
|                          |                                  |               | after-think carry propagation (velocity  |
|                          |                                  |               | + grounded), player entity origin sync   |
|                          |                                  |               | from camera. F1 grab toggle, F2          |
|                          |                                  |               | debug-view cycle, F3 PVS stats. Game DLL |
|                          |                                  |               | think at frame end.                      |
+--------------------------+----------------------------------+---------------+------------------------------------------+

  >> 4.2 Key Interfaces
  -----------------------

+----------------------+----------------------+----------------------------------------------------+
| Interface            | Implementations      | Description                                        |
+======================+======================+====================================================+
| IPlatform            | SDL2Platform         | Window creation, event polling, file system        |
|                      |                      | access, high-resolution timer, thread creation     |
+----------------------+----------------------+----------------------------------------------------+
| IRenderBackend       | OpenGLBackend        | Create/destroy GPU resources (buffers, textures,   |
|                      |                      | shaders), submit draw calls, present frame. NEVER  |
|                      |                      | call OpenGL directly outside this class.           |
+----------------------+----------------------+----------------------------------------------------+
| IFileSystem          | DefaultFileSystem    | Open/read/write files; supports pack files (.pak)  |
|                      |                      | and loose files; abstracted for modding later      |
+----------------------+----------------------+----------------------------------------------------+

  >> Acceptance Criteria
  ------------------------

  [x] A 1280x720 window opens without errors
  [x] A Quake 2 .bsp file loads from disk and renders with correct lightmaps
  [x] Camera moves with WASD + mouse look at 60+ FPS
  [x] BSP PVS culling active — findLeaf() + decompressPVS() verified with F3 stats
  [x] All Phase 1 systems show status DONE in the table above
  [x] No raw OpenGL calls exist outside renderer/gl/ directory


────────────────────────────────────────────────────────────────────────
5. PHASE 2 — QUAKE 2 CORE (MONTHS 6–12) [IN PROGRESS]
────────────────────────────────────────────────────────────────────────

  GOAL:
  Phase 2 transforms the renderer demo into a playable game prototype.
  By the end of Phase 2, a simple arena FPS can run: a player moves
  through a level, shoots projectiles, and the game logic lives in a
  separate DLL. Networking supports a basic client-server game.


  >> 5.1 System Status
  ----------------------

+--------------------------+----------------------------------+---------------+------------------------------------------+
| System / Class           | Source File(s)                   | Status        | Notes / Next Steps                       |
+==========================+==================================+===============+==========================================+
| IGameModule interface    | engine/entities/igame_module.h   | DONE          | Engine<->game contract: init, shutdown,  |
|                          |                                  |               | think, loadMap, onEntitySpawn,           |
|                          |                                  |               | onEntityTouch, onEntityDestroy.          |
|                          |                                  |               | GameModule in game DLL implements it.    |
|                          |                                  |               | Engine calls game->init() then           |
|                          |                                  |               | game->loadMap() after BSP load,          |
|                          |                                  |               | game->think() each frame.                |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| GameDLL loader           | engine/entities/game_dll_loader… | DONE          | LoadLibrary-based. Loads nova_game.dll,  |
|                          |                                  |               | resolves GetGameModule() symbol, returns |
|                          |                                  |               | IGameModule*. Hot-reload not yet         |
|                          |                                  |               | implemented.                             |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| EngineAPI (cross-DLL)    | engine/core/engine_api.h/.cpp    | DONE          | C-compatible function table passed to    |
|                          |                                  |               | game DLL on init. Exposes setEntityAnim, |
|                          |                                  |               | registerEntityClass, createModelEntity,  |
|                          |                                  |               | set/getEntityOrigin,                     |
|                          |                                  |               | findEntityByClassname,                   |
|                          |                                  |               | get/setEntityProperty,                   |
|                          |                                  |               | iterateActiveEntities. Static stubs in   |
|                          |                                  |               | engine_api.cpp handle entity pool        |
|                          |                                  |               | access; renderer callbacks wired by      |
|                          |                                  |               | Engine at startup.                       |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| EntityID / EntityHandle  | engine/entities/entity_id.h      | DONE          | Generational index: 16-bit generation +  |
|                          |                                  |               | 15-bit index packed into 32 bits.        |
|                          |                                  |               | make(), index(), generation(),           |
|                          |                                  |               | isValid(), isInvalid(). EntityRef for    |
|                          |                                  |               | safe frame-scoped access. All tests      |
|                          |                                  |               | pass.                                    |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| EntityList               | engine/entities/entity_list.h/.… | DONE          | Flat static arrays, kMaxEntities=1024,   |
|                          |                                  |               | zero heap allocation. O(1)               |
|                          |                                  |               | create/destroy via free-list stack.      |
|                          |                                  |               | iterateActive, iterateByClassname,       |
|                          |                                  |               | findByClassname, findInAABB, think(dt).  |
|                          |                                  |               | Engine EntityList created as global,     |
|                          |                                  |               | shared (separately compiled) across      |
|                          |                                  |               | engine + game DLL.                       |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Entity (base struct)     | engine/entities/entity.h         | DONE          | 480 bytes on MSVC x64 (static_assert).   |
|                          |                                  |               | origin/velocity/mins/maxs/angles,        |
|                          |                                  |               | classname[32], model[32], EntityState    |
|                          |                                  |               | lifecycle, EntityClass* ptr for virtual  |
|                          |                                  |               | dispatch, carriedThisFrame flag, plat    |
|                          |                                  |               | state fields (platStartPos/TargetPos,    |
|                          |                                  |               | height/speed/wait/timer/state),          |
|                          |                                  |               | animRequest[32], target/targetname,      |
|                          |                                  |               | aiThinkTimer, think/touch/use/pain/die   |
|                          |                                  |               | fn ptrs, property store integration.     |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| EntityFactory            | engine/entities/entity_factory.… | DONE          | Classname→spawn function registry.       |
|                          |                                  |               | init() registers built-in classes        |
|                          |                                  |               | (light, info_player_start, etc.),        |
|                          |                                  |               | parseOrigin, parseAngles, parseInt,      |
|                          |                                  |               | parseFloat helpers. Engine calls         |
|                          |                                  |               | EntityFactory::init() before BSP load.   |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| MapLoader (.bsp spawn)   | engine/entities/map_loader.cpp   | DONE          | Parses entity lump from .bsp; calls      |
|                          |                                  |               | EntityFactory::spawn() for each entity.  |
|                          |                                  |               | Extracts origin, angles, spawnflags,     |
|                          |                                  |               | health, target/targetname, property      |
|                          |                                  |               | store keys. For brush entities           |
|                          |                                  |               | (model=*N): reads bmodel origin +        |
|                          |                                  |               | mins/maxs from BSP submodel data. Calls  |
|                          |                                  |               | finalize() to trigger                    |
|                          |                                  |               | EntityClass::onSpawn().                  |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| PropertyStore            | engine/entities/property_store.h | DONE          | Cross-DLL safe key-value store indexed   |
|                          |                                  |               | by entity handle. get/set/has methods.   |
|                          |                                  |               | Populated by MapLoader from BSP entity   |
|                          |                                  |               | lump; read by game DLL through           |
|                          |                                  |               | EngineAPI.                               |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| EntityClass system       | engine/entities/entity_class.h/… | DONE          | EntityClass base with virtual            |
|                          |                                  |               | onSpawn/onThink/onPain/onDie/onTouch/on… |
|                          |                                  |               | hooks. EntityClassRegistry maps          |
|                          |                                  |               | classname→EntityClass*. Engine-side      |
|                          |                                  |               | global g_entityClasses. Game DLL         |
|                          |                                  |               | registers classes through                |
|                          |                                  |               | EngineAPI::registerEntityClass.          |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| PlayerEntityClass        | game/src/game_entity_classes.h/… | DONE          | Minimal no-op EntityClass registered by  |
|                          |                                  |               | game DLL so player entity has valid      |
|                          |                                  |               | entityClass pointer. Player logic driven |
|                          |                                  |               | by engine-side PlayerController, not     |
|                          |                                  |               | onThink.                                 |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| IPhysicsWorld interface  | engine/physics/iphysics_world.h  | DONE          | Pure virtual: setWorld, step,            |
|                          |                                  |               | setOrigin/Velocity, getOrigin/Velocity,  |
|                          |                                  |               | trace, traceEntity, moveSlide,           |
|                          |                                  |               | isOnGround, getGroundEntity,             |
|                          |                                  |               | getGroundElevation,                      |
|                          |                                  |               | setGravity/getGravity. TraceResult       |
|                          |                                  |               | struct with fraction, endPos, normal,    |
|                          |                                  |               | startSolid.                              |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| AABBPhysics              | engine/physics/aabb_physics.h/.… | DONE          | setEntityStorage(Vec3* origin, Vec3*     |
|                          |                                  |               | velocity, count) wires raw arrays for    |
|                          |                                  |               | physics. setPlayerBounds, testSolid      |
|                          |                                  |               | (spawn escape), isOnGround,              |
|                          |                                  |               | getGroundElevation, setGravity. Swept    |
|                          |                                  |               | AABB vs BSP + dynamic colliders.         |
|                          |                                  |               | moveSlide with 2-plane crease fix,       |
|                          |                                  |               | step-up with ceiling check, startSolid   |
|                          |                                  |               | guard. All tests pass. Full water        |
|                          |                                  |               | movement still TODO.                     |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| DynamicCollider          | engine/physics/aabb_physics.h    | DONE          | AABB collider struct for moving brush    |
|                          |                                  |               | entities. setDynamicColliders() on       |
|                          |                                  |               | AABBPhysics. Rebuilt each frame from     |
|                          |                                  |               | entity positions after think(). Platform |
|                          |                                  |               | collision works through this system.     |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| PlayerController         | engine/player/player_controller… | DONE          | First-person movement controller. Wish   |
|                          |                                  |               | direction from WASD, jump with space,    |
|                          |                                  |               | sprint with shift. CVar-driven feel:     |
|                          |                                  |               | pc_jumpspeed/movespeed/groundaccel/aira… |
|                          |                                  |               | Ground detection, gravity, friction,     |
|                          |                                  |               | acceleration. Velocity/position synced   |
|                          |                                  |               | with AABBPhysics backing store each      |
|                          |                                  |               | frame. Camera at getEyePosition() =      |
|                          |                                  |               | m_position + kPC_EyeHeight.              |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| FuncPlat (moving plat)   | game/src/game_entity_classes.h   | DONE          | Cyclic vertical moving platform via      |
|                          |                                  |               | EntityClass::onThink. Reads              |
|                          |                                  |               | height/speed/wait from TrenchBroom       |
|                          |                                  |               | properties through EngineAPI.            |
|                          |                                  |               | carryEntities() lifts players standing   |
|                          |                                  |               | on platform: Q2 AABB overlap test with   |
|                          |                                  |               | tolerance [-4, +2], delta applied to     |
|                          |                                  |               | entity origin, velocity inherited.       |
|                          |                                  |               | Engine after-think propagates carry to   |
|                          |                                  |               | physics backing store.                   |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| ConsoleVar (cvar)        | engine/core/cvar.cpp             | DONE          | CVarSystem singleton. reg(name, default, |
|                          |                                  |               | desc) creates CVar. find() lookup by     |
|                          |                                  |               | name. get/set via CVar*. CVar::value is  |
|                          |                                  |               | a plain float member, zero-overhead      |
|                          |                                  |               | read. Inline registration for            |
|                          |                                  |               | PlayerController feel cvars. Engine      |
|                          |                                  |               | cvars: r_debugview, sv_cheats.           |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Console                  | engine/core/console.cpp          | DONE          | In-game drop-down console toggled with   |
|                          |                                  |               | tilde (~). Line input, command parsing,  |
|                          |                                  |               | cvar get/set. addLogLine() for engine    |
|                          |                                  |               | log hook. Mouse grab toggle via          |
|                          |                                  |               | callback. Q2-style conchars font or      |
|                          |                                  |               | built-in IBM CP437 8x8 fallback.         |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Text2D / Console font    | engine/core/text_2d.h/.cpp       | DONE          | Bitmap font rendering for console and    |
|                          |                                  |               | HUD. Supports Q2 conchars (normal +      |
|                          |                                  |               | alternate set) and generic bitmap fonts. |
|                          |                                  |               | Built-in IBM CP437 8x8 fallback if no    |
|                          |                                  |               | font file found.                         |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Model Renderer (MD2)     | engine/renderer/models/md2_load… | DONE          | Load Quake 2 .md2 vertex-animated        |
|                          |                                  |               | models; lerp between frames.             |
|                          |                                  |               | ModelRenderer class with loadMD2Model,   |
|                          |                                  |               | registerEntity, setEntityAnim,           |
|                          |                                  |               | setEntityAnimRange. OBJ static mesh      |
|                          |                                  |               | loader also implemented.                 |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Sprite Renderer          | renderer/sprite.cpp              | TODO          | Billboard sprites for particles,         |
|                          |                                  |               | explosions, pickups                      |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Particle System (basic)  | renderer/particles/particles.cpp | TODO          | CPU-simulated particles: spawn, update,  |
|                          |                                  |               | fade, billboard render                   |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| HUD / 2D Renderer        | renderer/hud/hud.cpp             | TODO          | Orthographic quads for health bar, ammo  |
|                          |                                  |               | counter, crosshair                       |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Water movement           | engine/physics/aabb_physics.cpp  | TODO          | Swim physics: buoyancy, water drag,      |
|                          |                                  |               | surface detection. Not yet implemented — |
|                          |                                  |               | player falls through water brushes.      |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| INetworkSystem interface | network/inetwork.h               | TODO          | connect, disconnect, sendPacket,         |
|                          |                                  |               | receivePackets — pure virtual            |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Server                   | network/server.cpp               | TODO          | Authoritative sim; builds delta          |
|                          |                                  |               | snapshots; broadcasts to clients         |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Client                   | network/client.cpp               | TODO          | Sends input; receives snapshots;         |
|                          |                                  |               | client-side prediction + interpolation   |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| DeltaCompressor          | network/delta.cpp                | TODO          | Diff two entity snapshots; encode        |
|                          |                                  |               | changed fields only (bit flags)          |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| PacketBuffer             | network/packet.cpp               | TODO          | Reliable + unreliable channels; sequence |
|                          |                                  |               | numbers; ack tracking                    |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| IAudioSystem interface   | audio/iaudio.h                   | TODO          | loadSound, playSound, play3D, stopAll —  |
|                          |                                  |               | pure virtual                             |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| OpenALAudio              | audio/openal/openal_audio.cpp    | TODO          | 3D positional audio; WAV/OGG playback;   |
|                          |                                  |               | distance attenuation                     |
+--------------------------+----------------------------------+---------------+------------------------------------------+

  >> Networking Architecture — Server/Client Data Flow Per Tick
  ---------------------------------------------------------------

    SERVER TICK (every 50ms / 20Hz):
      1. Receive input packets from all clients
      2. Apply inputs to each client's player entity
      3. Run IGameModule::think(dt) — game logic, AI, projectiles
      4. Run IPhysicsWorld::step(dt) — move all entities, resolve collisions
      5. For each client: build snapshot (all entity states)
      6. Delta compress snapshot against client's last ack'd snapshot
      7. Send compressed delta packet to client
    
    CLIENT TICK (every 16ms / 60Hz):
      1. Read local input (keyboard/mouse)
      2. Store input in history buffer (for reconciliation)
      3. Predict local player movement immediately (client-side prediction)
      4. Send input packet to server
      5. Receive delta snapshot from server
      6. Apply delta to entity state buffer
      7. Reconcile: re-apply unack'd inputs over server state
      8. Interpolate remote entities between two buffered snapshots
      9. Render interpolated world state


  >> Acceptance Criteria
  ------------------------

  [x] Player loads a map, spawns on info_player_start, walks and jumps (WASD + space)
  [x] Game DLL separation works — engine + game are separate binaries linked through IGameModule + EngineAPI
  [x] FuncPlat entities carry the player (moving platform with AABB carry detection + velocity inheritance)
  [x] Console opens with tilde (~), cvars are readable and settable
  [x] EntityClass system lets game DLL define entity behavior (FuncPlat, MonsterClass, PlayerEntityClass) without engine changes
  [ ] ⬜ Swimming not yet implemented — player falls through water brushes
  [ ] ⬜ Shooting / projectiles not yet implemented
  [ ] ⬜ Networking not yet implemented (Server + Client + DeltaCompressor + PacketBuffer)
  [ ] ⬜ Audio not yet implemented (IAudioSystem + OpenALAudio)
  [ ] ⬜ Sprite / Particle / HUD rendering not yet implemented


────────────────────────────────────────────────────────────────────────
6. PHASE 3 — SOURCE ENGINE BRIDGE (MONTHS 12–24) [TODO]
────────────────────────────────────────────────────────────────────────

  GOAL:
  Phase 3 upgrades visual and gameplay capabilities to Source-engine
  parity. Crucially, none of Phase 1 or Phase 2 code is discarded — it
  is extended through the existing interfaces. The renderer gains
  normal maps, HDR, and shadow mapping. Physics swaps to Jolt. Lua
  scripting is embedded.


  >> 6.1 System Status
  ----------------------

+--------------------------+----------------------------------+---------------+------------------------------------------+
| System / Class           | Source File(s)                   | Status        | Notes / Next Steps                       |
+==========================+==================================+===============+==========================================+
| Normal Map Support       | renderer/gl/gl_normalmap.cpp     | TODO          | Tangent-space normal maps; TBN matrix    |
|                          |                                  |               | per vertex; add to BSP surfaces          |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Specular / Roughness     | renderer/materials/material.cpp  | TODO          | PBR-lite: albedo + normal + roughness +  |
| Maps                     |                                  |               | metallic (4 textures per surface)        |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| HDR Framebuffer          | renderer/gl/gl_hdr.cpp           | TODO          | Render to float16 FBO; tone-map          |
|                          |                                  |               | (Reinhard/ACES) to LDR for display       |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Shadow Mapping           | renderer/shadows/shadowmap.cpp   | TODO          | PCF shadow maps for sun/spot lights;     |
|                          |                                  |               | cascade for large outdoor maps           |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Bloom Post-Process       | renderer/post/bloom.cpp          | TODO          | Downsample bright areas; Gaussian blur;  |
|                          |                                  |               | additive blend over scene                |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Displacement Geometry    | renderer/bsp/displacement.cpp    | TODO          | Subdivide BSP faces into displacement    |
|                          |                                  |               | grid; edit height in map editor          |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Displacement Compiler    | tools/qbsp/displacement.cpp      | TODO          | Compile displacement data into .bsp;     |
|                          |                                  |               | store LOD levels                         |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| IPhysicsWorld (Jolt)     | physics/jolt/jolt_world.cpp      | TODO          | Swap AABBPhysics -> JoltPhysics behind   |
|                          |                                  |               | IPhysicsWorld; keep interface            |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Rigid Body Component     | physics/jolt/rigid_body.cpp      | TODO          | Physics-driven entities: barrels,        |
|                          |                                  |               | crates, ragdolls                         |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| IScriptEngine interface  | scripting/iscript_engine.h       | TODO          | loadScript, callFunction, exposeObject — |
|                          |                                  |               | pure virtual                             |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| LuaScriptEngine          | scripting/lua/lua_engine.cpp     | TODO          | Embed Lua 5.4; bind Entity, World,       |
|                          |                                  |               | Console, Events via sol2 or manual       |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Script Entity Bindings   | scripting/lua/bindings.cpp       | TODO          | Expose entity.origin, entity.think,      |
|                          |                                  |               | world.trace() etc. to Lua                |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Decal System             | renderer/decals/decal.cpp        | TODO          | Projected decals on BSP surfaces: bullet |
|                          |                                  |               | holes, scorch marks, blood               |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Dynamic Lights           | renderer/lights/dyn_light.cpp    | TODO          | Up to 256 point/spot lights per frame;   |
|                          |                                  |               | frustum-culled; affect models            |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Env Cubemap /            | renderer/env/cubemap.cpp         | TODO          | Per-zone env_cubemap; sample in material |
| Reflections              |                                  |               | shader for reflections                   |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Enhanced Particle System | renderer/particles/gpu_particle… | TODO          | GPU-simulated particles using transform  |
|                          |                                  |               | feedback or compute shader               |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Skeletal Animation (SMD) | renderer/models/skeletal.cpp     | TODO          | Bone-weighted skinning; load Valve SMD   |
|                          |                                  |               | or GLTF; blend between clips             |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Animation State Machine  | renderer/models/anim_state.cpp   | TODO          | States + transitions + blend weights;    |
|                          |                                  |               | driven by game logic or Lua              |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Sound Occlusion          | audio/occlusion.cpp              | TODO          | Raycast through BSP to attenuate         |
|                          |                                  |               | occluded sounds (low-pass filter)        |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Reverb Zones             | audio/reverb.cpp                 | TODO          | Map-placed reverb volumes (cave, tunnel, |
|                          |                                  |               | outdoor) via OpenAL EFX                  |
+--------------------------+----------------------------------+---------------+------------------------------------------+

  >> Renderer Upgrade Path
  --------------------------

    Phase 1 Render Pipeline:
      BSP surfaces → lightmap shader → display
    
    Phase 2 Additions:
      + MD2 vertex-animated models
      + Billboard sprites / particles
      + HUD overlay (2D ortho pass)
    
    Phase 3 Additions:
      + Normal/specular maps in surface shader
      + Shadow map pre-pass (depth only, per light)
      + HDR framebuffer (scene renders to float16)
      + Post-process stack: bloom → tone-map → gamma → display
      + Dynamic lights injected into surface shader
      + Displacement mesh rendering


  >> Acceptance Criteria
  ------------------------

  [ ] A level with normal maps, dynamic lights, and shadow maps renders correctly
  [ ] A physics prop (crate) can be pushed around using Jolt rigid body
  [ ] A Lua script can spawn an entity, set its think function, and react to player proximity
  [ ] Particle effects (smoke, sparks) emit from entities at runtime
  [ ] Skeletal model plays and blends idle/walk/attack animations


────────────────────────────────────────────────────────────────────────
7. PHASE 4 — MODERN ENGINE (MONTHS 24+) [TODO]
────────────────────────────────────────────────────────────────────────

  GOAL:
  Phase 4 is the long-term evolution. The engine becomes Vulkan-based,
  PBR-native, and includes a level editor. This phase has no hard
  deadline — systems are added incrementally. Phase 3 must be fully
  complete before Phase 4 begins.


  >> 7.1 System Status
  ----------------------

+--------------------------+----------------------------------+---------------+------------------------------------------+
| System / Class           | Source File(s)                   | Status        | Notes / Next Steps                       |
+==========================+==================================+===============+==========================================+
| VulkanBackend            | renderer/vulkan/vk_backend.cpp   | TODO          | Swap OpenGLBackend -> VulkanBackend      |
|                          |                                  |               | behind IRenderBackend; keep API          |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Vulkan Resource Manager  | renderer/vulkan/vk_resources.cpp | TODO          | Descriptor sets, render passes, pipeline |
|                          |                                  |               | cache, memory allocator                  |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| GPU-Driven Rendering     | renderer/vulkan/gpu_culling.cpp  | TODO          | Indirect draw calls; GPU frustum +       |
|                          |                                  |               | occlusion cull; no CPU per-object        |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| PBR Material System      | renderer/materials/pbr.cpp       | TODO          | Full PBR: albedo, normal, roughness,     |
|                          |                                  |               | metallic, AO, emissive maps              |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Screen-Space Reflections | renderer/post/ssr.cpp            | TODO          | Raymarched SSR in screen space; fallback |
|                          |                                  |               | to cubemaps                              |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Ambient Occlusion (SSAO) | renderer/post/ssao.cpp           | TODO          | HBAO+ style; 16 samples; blur; modulate  |
|                          |                                  |               | diffuse lighting                         |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Global Illumination      | renderer/gi/gi.cpp               | TODO          | Lightprobes / irradiance volumes; baked  |
|                          |                                  |               | or dynamic (Lumen-style later)           |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Level Editor (Nova Edit) | editor/                          | TODO          | Hammer-like editor: brush drawing,       |
|                          |                                  |               | entity placement, compile pipeline       |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Editor Renderer          | editor/editor_renderer.cpp       | TODO          | Grid, wireframe overlay, selection       |
|                          |                                  |               | highlight, gizmos (translate/rotate)     |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Temporal Anti-Aliasing   | renderer/post/taa.cpp            | TODO          | Accumulate samples across frames; jitter |
|                          |                                  |               | projection; motion vectors               |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Streaming / LOD System   | engine/streaming/lod.cpp         | TODO          | LOD chain per model; distance-based      |
|                          |                                  |               | swap; async streaming from disk          |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Async Asset Pipeline     | engine/assets/asset_manager.cpp  | TODO          | Background thread loads textures/models; |
|                          |                                  |               | main thread gets handles                 |
+--------------------------+----------------------------------+---------------+------------------------------------------+
| Profiler / RenderDoc     | engine/profiler/profiler.cpp     | TODO          | CPU/GPU timers per pass; overlay UI;     |
| Hook                     |                                  |               | integrate RenderDoc capture API          |
+--------------------------+----------------------------------+---------------+------------------------------------------+

  >> Acceptance Criteria
  ------------------------

  [ ] VulkanBackend passes all tests that OpenGLBackend previously passed
  [ ] A full PBR scene renders with SSR, SSAO, and TAA at 60 FPS on mid-range GPU
  [ ] Level editor opens a .bsp, allows brush editing, and recompiles in-editor
  [ ] Async asset loading shows no frame hitches when moving through a large level


────────────────────────────────────────────────────────────────────────
8. CODING CONVENTIONS & RULES
────────────────────────────────────────────────────────────────────────

  >> 8.1 Naming Conventions
  ---------------------------

  Classes / Structs:    PascalCase          e.g. RenderBackend, EntityList
  Interfaces:           IPascalCase         e.g. IRenderBackend, IPhysicsWorld
  Functions:            camelCase           e.g. loadShader(), stepPhysics()
  Member variables:     m_camelCase         e.g. m_vertexBuffer, m_entityCount
  Constants / Enums:    UPPER_SNAKE_CASE    e.g. MAX_ENTITIES, BSP_VERSION
  Files:                snake_case.cpp/.h   e.g. bsp_loader.cpp, gl_texture.h

  >> 8.2 Include Rules
  ----------------------

  - No module may include headers from a module that depends on it (no circular deps)
  - Engine modules never include game/ headers — only game/ includes engine/
  - renderer/ never includes physics/ or network/ — pass data through structs
  - Use forward declarations in .h files; #include in .cpp files only

  >> 8.3 Memory Rules
  ---------------------

  - No naked new/delete in engine code — use allocators from core/memory/
  - All per-frame allocations use MemoryArena and are reset at frame end
  - All persistent game objects use ZoneAllocator or PoolAllocator
  - Render resources (buffers, textures) managed exclusively by IRenderBackend

  >> 8.4 Interface Rules
  ------------------------

  - Every swappable system must have a pure virtual interface class prefixed with I
  - No code outside a module's own directory calls concrete implementation classes
  - Interface methods return error codes (enum Result), not throw exceptions
  - All interface destructors are virtual

  >> 8.5 File Header Template
  -----------------------------

  // ============================================================
  // FILE:    engine/renderer/bsp/bsp_loader.cpp
  // MODULE:  Renderer > BSP
  // PHASE:   1
  // STATUS:  TODO | IN_PROGRESS | DONE
  // PURPOSE: Loads Quake 2 .bsp files into the BSPMap struct.
  //          Parses all lumps: vertices, faces, edges, texinfo,
  //          lightmaps, entities, PVS data.
  // DEPENDS: core/math, core/memory, platform/IFileSystem
  // ============================================================


────────────────────────────────────────────────────────────────────────
9. BUILD SYSTEM & DEPENDENCIES
────────────────────────────────────────────────────────────────────────

  >> 9.1 CMake Structure
  ------------------------

  CMakeLists.txt              # Root: sets C++20, warnings, platform flags
  engine/CMakeLists.txt       # Builds nova_engine static lib
  game/CMakeLists.txt         # Builds game shared lib (nova_game.dll/.so)
  tools/CMakeLists.txt        # Builds qbsp, light, vis executables
  tests/CMakeLists.txt        # Builds all test executables via CTest

  >> 9.2 External Dependencies
  ------------------------------

+----------------+----------------------+---------------+----------------------------------------------------+
| Library        | Directory            | Status        | Notes                                              |
+================+======================+===============+====================================================+
| SDL3           | vendor/SDL3/         | DONE          | v3.2+; window, input, OpenGL context. Relative     |
|                |                      |               | mouse mode for FPS look.                           |
+----------------+----------------------+---------------+----------------------------------------------------+
| OpenGL 4.5     | renderer/gl/         | DONE          | Via GLAD loader; 4.5 core profile; DSA (direct     |
|                |                      |               | state access) for clarity                          |
+----------------+----------------------+---------------+----------------------------------------------------+
| GLAD           | vendor/GLAD/         | DONE          | OpenGL function loader; GL 4.5 core profile        |
+----------------+----------------------+---------------+----------------------------------------------------+
| stb_image      | vendor/stb/          | DONE          | Single-header image loader for PNG/JPG/TGA/WAL     |
|                |                      |               | texture loading                                    |
+----------------+----------------------+---------------+----------------------------------------------------+
| GLM            | vendor/glm/          | TODO          | Header-only math; planned for tool scripts —       |
|                |                      |               | engine uses its own math                           |
+----------------+----------------------+---------------+----------------------------------------------------+
| OpenAL Soft    | audio/openal/        | TODO          | Phase 2+; EFX extension for reverb;                |
|                |                      |               | cross-platform. Not started.                       |
+----------------+----------------------+---------------+----------------------------------------------------+
| Jolt Physics   | physics/jolt/        | TODO          | Phase 3+; MIT license; replace AABBPhysics         |
+----------------+----------------------+---------------+----------------------------------------------------+
| Lua 5.4        | scripting/lua/       | TODO          | Phase 3+; embed as static lib                      |
+----------------+----------------------+---------------+----------------------------------------------------+
| Vulkan SDK     | renderer/vulkan/     | TODO          | Phase 4+; VMA + vkbootstrap                        |
+----------------+----------------------+---------------+----------------------------------------------------+

────────────────────────────────────────────────────────────────────────
10. TESTING STRATEGY
────────────────────────────────────────────────────────────────────────

+------------------------------------+--------------------------------+---------------+------------------------------------------+
| Test File                          | Covers                         | Status        | Notes                                    |
+====================================+================================+===============+==========================================+
| tests/core/test_math.cpp           | Tests: Vec3, Mat4, Quat        | DONE          | Dot/cross/normalize, mat4 mul, frustum   |
|                                    |                                |               | planes. 33 lines, plain assert().        |
+------------------------------------+--------------------------------+---------------+------------------------------------------+
| tests/core/test_memory.cpp         | Tests: Arena, Zone, Pool       | DONE          | Alloc/free/reset; out-of-memory          |
|                                    |                                |               | handling; alignment checks. All pass.    |
+------------------------------------+--------------------------------+---------------+------------------------------------------+
| tests/core/test_log.cpp            | Tests: Logger levels + hook    | DONE          | DEBUG/INFO/WARN/ERROR levels; log hook   |
|                                    |                                |               | callback. All pass.                      |
+------------------------------------+--------------------------------+---------------+------------------------------------------+
| tests/renderer/test_bsp.cpp        | Tests: BSP load + PVS          | DONE          | Load test.bsp; verify leaf count, model  |
|                                    |                                |               | count, PVS bits. All pass.               |
+------------------------------------+--------------------------------+---------------+------------------------------------------+
| tests/physics/test_ground.cpp      | Tests: ground, gravity, trace, | DONE          | isOnGround (no BSP), getGroundElevation, |
|                                    | entity storage                 |               | gravity default/set, player bounds,      |
|                                    |                                |               | trace fraction=1 with no BSP, entity     |
|                                    |                                |               | origin/velocity set+get. All pass.       |
+------------------------------------+--------------------------------+---------------+------------------------------------------+
| tests/physics/test_collide.cpp     | Tests: AABB collision          | DONE          | traceAABB swept vs static AABB. Slab     |
|                                    |                                |               | method verification. All pass.           |
+------------------------------------+--------------------------------+---------------+------------------------------------------+
| tests/physics/test_trace_world.cpp | Tests: AABB vs BSP + dynamics  | DONE          | BSP brush trace + dynamic collider       |
|                                    |                                |               | sweep. All pass.                         |
+------------------------------------+--------------------------------+---------------+------------------------------------------+
| tests/entities/test_entity.cpp     | Tests: EntityID, Handle, List, | DONE          | EntityID make/index/gen/valid;           |
|                                    | struct                         |               | EntityHandle null/valid; EntityList      |
|                                    |                                |               | create/destroy/count/get/classname;      |
|                                    |                                |               | Entity state/origin/velocity/flags. All  |
|                                    |                                |               | pass.                                    |
+------------------------------------+--------------------------------+---------------+------------------------------------------+
| tests/network/test_delta.cpp       | Tests: Delta                   | TODO          | Encode two snapshots; decode; verify     |
|                                    | compress/decompress            |               | byte-for-byte equality. Not started.     |
+------------------------------------+--------------------------------+---------------+------------------------------------------+
| tests/entities/test_elist.cpp      | Tests: EntityList stress       | TODO          | Create 1000 entities; destroy random     |
|                                    |                                |               | subset; verify handles and count. Not    |
|                                    |                                |               | started.                                 |
+------------------------------------+--------------------------------+---------------+------------------------------------------+

────────────────────────────────────────────────────────────────────────
11. QUICK REFERENCE — WHAT TO DO NEXT
────────────────────────────────────────────────────────────────────────

  >> Fresh Start (all TODO)
  ---------------------------

  Step 1: Set up CMake project structure and empty module directories
  Step 2: Implement core/math — Vec3, Mat4, Plane, AABB, Ray
  Step 3: Implement core/memory — Arena, Zone, Pool allocators
  Step 4: Implement Logger
  Step 5: Define IPlatform interface; implement SDL2Platform (window + input only)
  Step 6: Define IRenderBackend interface
  Step 7: Implement OpenGLBackend (clear screen, triangle on screen)
  Step 8: Implement BSP Loader
  Step 9: Implement BSP Renderer + Lightmap System
  Step 10: Implement Camera — you now have Phase 1

  >> Phase 2 In Progress
  ------------------------

  Step 1 (DONE): Foundation — EntityID/Handle, EntityList, Entity struct, IPhysicsWorld/AABBPhysics
  Step 2 (DONE): EntityClass system — virtual onSpawn/onThink/onTouch/onUse/onDie hooks + registry
  Step 3 (DONE): EngineAPI cross-DLL function table — game DLL talks to engine through it
  Step 4 (DONE): IGameModule + GameDLL loader — engine loads game DLL, calls init/think/loadMap
  Step 5 (DONE): EntityFactory + MapLoader — spawn entities from BSP lump, property store
  Step 6 (DONE): CVar system + Console — tilde console, live cvar get/set
  Step 7 (DONE): PlayerController — WASD + jump + sprint + mouse look, ground detection
  Step 8 (DONE): FuncPlat moving platform — AABB carry detection works with engine after-think
  Step 9 (DONE): MD2 + OBJ model renderer — vertex-animated + static mesh loading
  Step 10 (NEXT): Water movement — swim physics, buoyancy, surface detection
  Step 11: Sprite renderer — billboard sprites for particles, pickups, explosions
  Step 12: HUD / 2D renderer — health bar, ammo counter, crosshair overlay
  Step 13: Server + Client + DeltaCompressor + PacketBuffer — networking
  Step 14: OpenALAudio — 3D positional sound, WAV/OGG playback
  Step 15: Particle system — CPU-simulated particles for effects

  >> Starting Phase 3
  ---------------------

  Step 1: Upgrade surface shader with normal map + specular support
  Step 2: Add HDR framebuffer + tone mapping post-process
  Step 3: Add shadow map pre-pass for sun light
  Step 4: Implement displacement geometry in BSP renderer
  Step 5: Swap AABBPhysics for JoltPhysics behind IPhysicsWorld
  Step 6: Embed Lua 5.4; define IScriptEngine; bind entity API
  Step 7: Add skeletal animation pipeline


────────────────────────────────────────────────────────────────────────
12. REVISION HISTORY
────────────────────────────────────────────────────────────────────────

+-------+--------------+--------------------+----------------------------------------------------------+
| Rev   | Date         | Author             | Changes                                                  |
+=======+==============+====================+==========================================================+
| 1.0   | TBD          | Project Lead       | Initial document creation — all four phases defined, all |
|       |              |                    | systems marked TODO                                      |
+-------+--------------+--------------------+----------------------------------------------------------+
| 1.1   | 2026-04-27   | Ahmed (solo dev)   | Phase 1 COMPLETE. BSP PVS culling implemented and        |
|       |              |                    | verified: findLeaf(), decompressPVS(), per-batch cluster |
|       |              |                    | gate, PVS cache. BSP Renderer, Lightmap System, and BSP  |
|       |              |                    | PVS Culling all marked DONE. Verified on spirit2dm2.bsp: |
|       |              |                    | 427 clusters, 54 bytes/row, leaf walk tracking correctly |
|       |              |                    | across full map traversal. Phase summary updated to      |
|       |              |                    | DONE.                                                    |
+-------+--------------+--------------------+----------------------------------------------------------+
| 1.2   | 2026-04-27   | Ahmed (solo dev)   | Phase 2 IN PROGRESS. Entity system foundation complete:  |
|       |              |                    | EntityID/EntityHandle DONE; EntityList flat arrays       |
|       |              |                    | kMaxEntities=1024 no heap allocation IN PROGRESS; Entity |
|       |              |                    | struct STATE_ALIVE/FREE lifecycle IN PROGRESS;           |
|       |              |                    | IPhysicsWorld interface DONE; AABBPhysics                |
|       |              |                    | setEntityStorage/testSolid/isOnGround/moveSlide IN       |
|       |              |                    | PROGRESS. Engine wired: m_entityList + m_playerEntity    |
|       |              |                    | added to Engine class, think(dt) called each frame,      |
|       |              |                    | player origin synced from camera each tick,              |
|       |              |                    | sizeof(Entity) printed on startup.                       |
|       |              |                    | tests/entities/test_entity.cpp and                       |
|       |              |                    | tests/physics/test_ground.cpp both pass. Phase 2 summary |
|       |              |                    | updated to IN PROGRESS. Quick reference updated with     |
|       |              |                    | current Phase 2 step order.                              |
+-------+--------------+--------------------+----------------------------------------------------------+
| 1.3   | 2026-05-09   | Ahmed (solo dev)   | Phase 2 comprehensive status update. EntityFactory,      |
|       |              |                    | MapLoader, IGameModule, GameDLL loader, ConsoleVar,      |
|       |              |                    | Console, MD2/OBJ Model Renderer, EngineAPI, EntityClass  |
|       |              |                    | system, PlayerController, FuncPlat, PropertyStore,       |
|       |              |                    | DynamicCollider, Text2D all marked DONE — was previously |
|       |              |                    | TODO. Engine Main Loop notes updated (after-think carry  |
|       |              |                    | propagation, frame ordering fix). PlayerController added |
|       |              |                    | as new system. FuncPlat carry system with AABB detection |
|       |              |                    | + velocity inheritance documented. Tests table updated:  |
|       |              |                    | all 7 existing tests marked DONE (were TODO).            |
|       |              |                    | Dependencies updated: SDL3, OpenGL 4.5, GLAD, stb_image  |
|       |              |                    | marked DONE. Quick reference Phase 2 steps rewritten to  |
|       |              |                    | match actual completion state (steps 1-9 DONE, steps     |
|       |              |                    | 10-15 TODO). Phase 2 acceptance criteria updated with    |
|       |              |                    | partial checkmarks for working systems.                  |
+-------+--------------+--------------------+----------------------------------------------------------+

========================================================================
  Generated by generate-md.js from ENGINE_PLAN_DATA.js
  To update: edit ENGINE_PLAN_DATA.js, then run: node generate-md.js
========================================================================
