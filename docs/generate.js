const {
    Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
    HeadingLevel, AlignmentType, BorderStyle, WidthType, ShadingType,
    LevelFormat, PageNumber, PageBreak, TabStopType, TabStopPosition,
    Header, Footer, ImageRun
} = require('docx');
const fs = require('fs');

// ── Color Palette ──────────────────────────────────────────────
const C = {
    black: "1A1A2E",
    darkNavy: "16213E",
    navy: "0F3460",
    accent: "E94560",
    accentSoft: "533483",
    headerBg: "0F3460",
    headerFg: "FFFFFF",
    subheaderBg: "1A1A2E",
    subheaderFg: "E0E0E0",
    rowAlt: "F0F4FF",
    rowNorm: "FFFFFF",
    done: "1E7E34",
    doneBg: "D4EDDA",
    inprog: "856404",
    inprogBg: "FFF3CD",
    todo: "721C24",
    todoBg: "F8D7DA",
    missing: "495057",
    missingBg: "E2E3E5",
    border: "CCCCCC",
    lightBorder: "E0E4EF",
    textMain: "1A1A2E",
    textSub: "444466",
    codeBg: "F4F6FB",
    codeFg: "2D2D5E",
};

// ── Helpers ────────────────────────────────────────────────────
const border = (color = C.border) => ({ style: BorderStyle.SINGLE, size: 1, color });
const borders = (color = C.border) => ({ top: border(color), bottom: border(color), left: border(color), right: border(color) });
const noBorder = () => ({ style: BorderStyle.NONE, size: 0, color: "FFFFFF" });
const noBorders = () => ({ top: noBorder(), bottom: noBorder(), left: noBorder(), right: noBorder() });

function cell(text, opts = {}) {
    const {
        bold = false, color = C.textMain, bg = C.rowNorm, width = 3120,
        size = 20, italic = false, align = AlignmentType.LEFT, borders: b = borders()
    } = opts;
    return new TableCell({
        borders: b,
        width: { size: width, type: WidthType.DXA },
        shading: { fill: bg, type: ShadingType.CLEAR },
        margins: { top: 80, bottom: 80, left: 140, right: 140 },
        children: [new Paragraph({
            alignment: align,
            children: [new TextRun({ text, bold, color, size, italics: italic, font: "Arial" })]
        })]
    });
}

function headerCell(text, width = 3120) {
    return cell(text, { bold: true, color: C.headerFg, bg: C.headerBg, width, size: 20 });
}

function statusCell(status) {
    const map = {
        "✅ DONE": { color: C.done, bg: C.doneBg },
        "🔄 IN PROGRESS": { color: C.inprog, bg: C.inprogBg },
        "⬜ TODO": { color: C.todo, bg: C.todoBg },
        "❌ MISSING": { color: C.missing, bg: C.missingBg },
    };
    const s = map[status] || { color: C.textMain, bg: C.rowNorm };
    return cell(status, { bold: true, color: s.color, bg: s.bg, width: 1800, size: 18, align: AlignmentType.CENTER });
}

function h1(text) {
    return new Paragraph({
        heading: HeadingLevel.HEADING_1,
        spacing: { before: 360, after: 180 },
        children: [new TextRun({ text, bold: true, font: "Arial", size: 36, color: C.navy })]
    });
}

function h2(text) {
    return new Paragraph({
        heading: HeadingLevel.HEADING_2,
        spacing: { before: 280, after: 140 },
        children: [new TextRun({ text, bold: true, font: "Arial", size: 28, color: C.accentSoft })]
    });
}

function h3(text) {
    return new Paragraph({
        heading: HeadingLevel.HEADING_3,
        spacing: { before: 200, after: 100 },
        children: [new TextRun({ text, bold: true, font: "Arial", size: 24, color: C.darkNavy })]
    });
}

function para(text, opts = {}) {
    const { bold = false, color = C.textMain, size = 22, spacing = { before: 60, after: 60 } } = opts;
    return new Paragraph({
        spacing,
        children: [new TextRun({ text, bold, color, size, font: "Arial" })]
    });
}

function bullet(text, opts = {}) {
    const { bold = false, color = C.textMain, size = 21 } = opts;
    return new Paragraph({
        numbering: { reference: "bullets", level: 0 },
        spacing: { before: 40, after: 40 },
        children: [new TextRun({ text, bold, color, size, font: "Arial" })]
    });
}

function subBullet(text) {
    return new Paragraph({
        numbering: { reference: "subbullets", level: 0 },
        spacing: { before: 30, after: 30 },
        children: [new TextRun({ text, color: C.textSub, size: 20, font: "Arial" })]
    });
}

function divider() {
    return new Paragraph({
        spacing: { before: 120, after: 120 },
        border: { bottom: { style: BorderStyle.SINGLE, size: 4, color: C.lightBorder, space: 1 } },
        children: []
    });
}

function codeBlock(lines) {
    return lines.map(line => new Paragraph({
        spacing: { before: 20, after: 20 },
        shading: { fill: C.codeBg, type: ShadingType.CLEAR },
        indent: { left: 360 },
        children: [new TextRun({ text: line, font: "Courier New", size: 18, color: C.codeFg })]
    }));
}

function pageBreak() {
    return new Paragraph({ children: [new PageBreak()] });
}

// ── Status Legend Table ────────────────────────────────────────
function legendTable() {
    const items = [
        { s: "✅ DONE", d: "Fully implemented, tested, and stable" },
        { s: "🔄 IN PROGRESS", d: "Work has started, partially complete" },
        { s: "⬜ TODO", d: "Planned but not yet started" },
        { s: "❌ MISSING", d: "Required but not yet designed/planned" },
    ];
    return new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: [2200, 7160],
        rows: [
            new TableRow({ children: [headerCell("Status", 2200), headerCell("Meaning", 7160)] }),
            ...items.map((it, i) => new TableRow({
                children: [
                    statusCell(it.s),
                    cell(it.d, { width: 7160, bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 20 })
                ]
            }))
        ]
    });
}

// ── Phase Summary Table ────────────────────────────────────────
function phaseSummaryTable() {
    const rows = [
        ["Phase 1", "Foundation", "0–6 months", "Math, memory, platform, OpenGL renderer, BSP loader, windowing", "🔄 IN PROGRESS"],
        ["Phase 2", "Quake 2 Core", "6–12 months", "Entity system, AABB physics, delta networking, audio, game DLL", "⬜ TODO"],
        ["Phase 3", "Source Bridge", "12–24 months", "Normal maps, HDR, displacement geo, Jolt physics, Lua scripting", "⬜ TODO"],
        ["Phase 4", "Modern Engine", "24+ months", "Vulkan backend, PBR, GPU-driven rendering, editor (Hammer-like)", "⬜ TODO"],
    ];
    const cols = [1200, 1700, 1600, 3760, 1100];
    return new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: cols,
        rows: [
            new TableRow({ children: ["Phase", "Name", "Timeline", "Key Deliverables", "Status"].map((t, i) => headerCell(t, cols[i])) }),
            ...rows.map((r, ri) => new TableRow({
                children: r.map((t, ci) => {
                    if (ci === 4) return statusCell(t);
                    return cell(t, {
                        width: cols[ci], bg: ri % 2 === 0 ? C.rowNorm : C.rowAlt, size: 19,
                        bold: ci === 0 || ci === 1
                    });
                })
            }))
        ]
    });
}

// ── System Status Table ────────────────────────────────────────
function systemTable(systems) {
    // systems = [{ name, file, status, notes }]
    const cols = [2100, 2400, 1800, 3060];
    return new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: cols,
        rows: [
            new TableRow({ children: ["System / Class", "Source File(s)", "Status", "Notes / Next Steps"].map((t, i) => headerCell(t, cols[i])) }),
            ...systems.map((s, i) => new TableRow({
                children: [
                    cell(s.name, { width: cols[0], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, bold: true, size: 19 }),
                    cell(s.file, { width: cols[1], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, color: C.codeFg, italic: true }),
                    statusCell(s.status),
                    cell(s.notes, { width: cols[3], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, color: C.textSub }),
                ]
            }))
        ]
    });
}

// ── File Structure Table ───────────────────────────────────────
function fileTreeTable(entries) {
    const cols = [3200, 2000, 4160];
    return new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: cols,
        rows: [
            new TableRow({ children: ["Path", "Type", "Purpose"].map((t, i) => headerCell(t, cols[i])) }),
            ...entries.map((e, i) => new TableRow({
                children: [
                    cell(e.path, { width: cols[0], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, color: C.codeFg, italic: true }),
                    cell(e.type, { width: cols[1], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, bold: true }),
                    cell(e.purpose, { width: cols[2], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18 }),
                ]
            }))
        ]
    });
}

// ── Interface Table ────────────────────────────────────────────
function interfaceTable(interfaces) {
    const cols = [2200, 2200, 4960];
    return new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: cols,
        rows: [
            new TableRow({ children: ["Interface", "Implementations", "Description"].map((t, i) => headerCell(t, cols[i])) }),
            ...interfaces.map((e, i) => new TableRow({
                children: [
                    cell(e.iface, { width: cols[0], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, bold: true, size: 19, color: C.navy }),
                    cell(e.impls, { width: cols[1], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, color: C.codeFg, italic: true }),
                    cell(e.desc, { width: cols[2], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18 }),
                ]
            }))
        ]
    });
}

// ══════════════════════════════════════════════════════════════
//  DOCUMENT CONTENT
// ══════════════════════════════════════════════════════════════
const children = [];

// ── COVER / TITLE ──────────────────────────────────────────────
children.push(
    new Paragraph({
        spacing: { before: 720, after: 200 }, alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: "NOVA ENGINE", bold: true, font: "Arial", size: 72, color: C.navy })]
    }),
    new Paragraph({
        spacing: { before: 0, after: 160 }, alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: "Game Engine Development Master Plan", font: "Arial", size: 32, color: C.accentSoft })]
    }),
    new Paragraph({
        spacing: { before: 0, after: 80 }, alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: "Quake 2 Foundation  →  Source Engine Capabilities", font: "Arial", size: 24, color: C.textSub, italics: true })]
    }),
    new Paragraph({
        spacing: { before: 200, after: 600 }, alignment: AlignmentType.CENTER,
        border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: C.accent, space: 1 } },
        children: [new TextRun({ text: `Revision 1.0  |  Start Date: TBD  |  Engine Codename: NOVA`, font: "Arial", size: 20, color: C.textSub })]
    }),
);

// ── SECTION 1: PURPOSE ────────────────────────────────────────
children.push(
    h1("1. Document Purpose & How to Use This Plan"),
    para("This document is the single source of truth for the NOVA Engine project. Any developer, AI assistant, or collaborator reading this document alongside the codebase should be able to instantly answer three questions:", { size: 22 }),
    bullet("What has been built and is working?"),
    bullet("What is currently being worked on?"),
    bullet("What is planned but not yet started, and what is the correct order to build it?"),
    para(""),
    para("This plan is organized into four phases. Each phase contains a system breakdown table that maps every major system to its source file(s), current status, and next steps. As development progresses, the status column in each table must be updated to reflect reality.", { color: C.textSub, size: 21 }),
    para(""),
    h2("1.1 Status Legend"),
    legendTable(),
    para(""),
    para("IMPORTANT: Every table in this document uses these exact four statuses. When writing code, mark a system as IN PROGRESS the moment you start it, and DONE only when it compiles, runs, and passes its basic test case.", { bold: true, color: C.navy, size: 21 }),
    divider(),
);

// ── SECTION 2: VISION ─────────────────────────────────────────
children.push(
    h1("2. Engine Vision & Architecture Philosophy"),
    h2("2.1 Goal"),
    para("Build a game engine that starts at Quake 2 (id Tech 2) complexity and grows into Source Engine capabilities — without ever rewriting core systems from scratch. Every system is designed with an abstraction layer so it can be upgraded, swapped, or extended without breaking dependents."),
    para(""),
    h2("2.2 Core Architecture Principles"),
    bullet("Abstraction First: Every major system (renderer, physics, audio, scripting) is hidden behind a pure virtual C++ interface. Nothing in the engine calls implementation details directly."),
    bullet("Isolation of Game Logic: Game code lives in a separate shared library (.dll / .so). The engine never links directly to game logic — only through IGameModule interface. This mirrors both Quake 2 and Source."),
    bullet("No Singletons for Core Systems: Systems are passed explicitly by pointer/reference. This enables multiple instances, testing, and future multiplayer scenarios."),
    bullet("Data-Oriented Where It Matters: Entity lists, render queues, and physics bodies use flat arrays for cache efficiency. Avoid deep inheritance chains for performance-critical paths."),
    bullet("Platform Layer Wraps Everything: All window creation, input, file I/O, and threading go through a platform abstraction. The engine never calls SDL2, Win32, or POSIX directly — only through IPlatform."),
    para(""),
    h2("2.3 Four-Phase Roadmap Overview"),
    phaseSummaryTable(),
    divider(),
);

// ── SECTION 3: DIRECTORY STRUCTURE ────────────────────────────
children.push(
    pageBreak(),
    h1("3. Project Directory Structure"),
    para("Every file in the engine belongs to exactly one module. The directory structure reflects the dependency order — lower-level modules appear first. No module may include headers from a module listed after it (no circular dependencies)."),
    para(""),
    fileTreeTable([
        { path: "engine/", type: "Root", purpose: "Engine source root — all engine code lives here" },
        { path: "engine/core/", type: "Module", purpose: "Math, memory allocators, string utils, containers, logging" },
        { path: "engine/core/math/", type: "Sub", purpose: "Vec2/3/4, Mat4, Quaternion, Plane, AABB, Ray structs" },
        { path: "engine/core/memory/", type: "Sub", purpose: "Zone allocator, arena allocator, pool allocator" },
        { path: "engine/core/containers/", type: "Sub", purpose: "Array, HashMap, StringView — no STL in hot paths" },
        { path: "engine/platform/", type: "Module", purpose: "IPlatform interface + SDL2 implementation" },
        { path: "engine/platform/sdl2/", type: "Sub", purpose: "SDL2Platform: window, input, file I/O, threading" },
        { path: "engine/renderer/", type: "Module", purpose: "IRenderBackend interface + all rendering code" },
        { path: "engine/renderer/gl/", type: "Sub", purpose: "OpenGLBackend: shaders, buffers, textures, draw calls" },
        { path: "engine/renderer/vulkan/", type: "Sub", purpose: "VulkanBackend (Phase 4) — not yet started" },
        { path: "engine/renderer/bsp/", type: "Sub", purpose: "BSP tree traversal, PVS lookup, surface rendering" },
        { path: "engine/physics/", type: "Module", purpose: "IPhysicsWorld interface + BSP/AABB implementation" },
        { path: "engine/physics/aabb/", type: "Sub", purpose: "Phase 1-2: sweep tests, BSP clip, player movement" },
        { path: "engine/physics/jolt/", type: "Sub", purpose: "Phase 3+: JoltPhysics rigid body implementation" },
        { path: "engine/network/", type: "Module", purpose: "Client/server architecture, delta compression, snapshots" },
        { path: "engine/audio/", type: "Module", purpose: "IAudioSystem interface + OpenAL implementation" },
        { path: "engine/entities/", type: "Module", purpose: "Entity list, component storage, spawn/think/touch callbacks" },
        { path: "engine/scripting/", type: "Module", purpose: "IScriptEngine interface (Phase 3: Lua binding)" },
        { path: "game/", type: "Module", purpose: "Game DLL — implements IGameModule, isolated from engine" },
        { path: "game/src/", type: "Sub", purpose: "Game entities, weapons, movement rules, game rules" },
        { path: "tools/", type: "Module", purpose: "Offline tools: BSP compiler, lightmap baker, VIS compiler" },
        { path: "tools/qbsp/", type: "Sub", purpose: "Compiles .map files to .bsp (brush geometry + BSP tree)" },
        { path: "tools/light/", type: "Sub", purpose: "Bakes lightmaps onto BSP surfaces (radiosity)" },
        { path: "tools/vis/", type: "Sub", purpose: "Computes PVS (Potentially Visible Set) data" },
        { path: "assets/", type: "Data", purpose: "Maps, textures, models, sounds — not compiled into engine" },
        { path: "tests/", type: "Tests", purpose: "Unit tests per module — one test file per system" },
        { path: "CMakeLists.txt", type: "Build", purpose: "Root CMake file — builds all modules and links them" },
        { path: "docs/ENGINE_PLAN.docx", type: "Docs", purpose: "THIS FILE — master plan, always keep updated" },
    ]),
    divider(),
);

// ── SECTION 4: PHASE 1 ────────────────────────────────────────
children.push(
    pageBreak(),
    h1("4. Phase 1 — Foundation (Months 0–6)"),
    para("Phase 1 produces a window with a BSP-rendered level visible on screen. No game logic, no networking, no audio. The sole goal is: open a window, load a .bsp file, render it with lightmaps using the OpenGL backend, and handle keyboard/mouse input. Everything built here must be abstracted behind interfaces."),
    para(""),

    h2("4.1 System Status — Phase 1"),
    systemTable([
        { name: "Vec2 / Vec3 / Vec4", file: "core/math/vec.h", status: "✅ DONE", notes: "float-based with 16-byte alignment pad; dot, cross, normalize, lengthSq" },
        { name: "Mat4", file: "core/math/mat4.h", status: "✅ DONE", notes: "Column-major; perspective, ortho, lookAt constructors present" },
        { name: "Quaternion", file: "core/math/quat.h", status: "✅ DONE", notes: "fromEuler, rotate, identity; used by Camera for yaw/pitch" },
        { name: "Plane / AABB / Ray", file: "core/math/shapes.h", status: "⬜ TODO", notes: "AABB and Ray stubs not yet implemented; needed for Phase 2 collision" },
        { name: "MemoryArena", file: "core/memory/arena.cpp", status: "✅ DONE", notes: "Linear allocator with reset(); used for scratch allocations" },
        { name: "ZoneAllocator", file: "core/memory/zone.cpp", status: "✅ DONE", notes: "Quake-style tagged blocks implemented" },
        { name: "PoolAllocator", file: "core/memory/pool.cpp", status: "✅ DONE", notes: "Fixed-size block pool allocator implemented" },
        { name: "Logger", file: "core/log.h / log.cpp", status: "✅ DONE", notes: "DEBUG/INFO/WARN/ERROR levels; file + stdout output; timestamped" },
        { name: "IPlatform interface", file: "platform/iplatform.h", status: "✅ DONE", notes: "Window, input (512-key array + 3 mouse buttons), file I/O, time, threads" },
        { name: "SDL2Platform", file: "platform/sdl2/sdl2_platform.cpp", status: "✅ DONE", notes: "FIXED: multi-key input now uses SDL_GetKeyboardState() snapshot — all keys held simultaneously work correctly" },
        { name: "InputState", file: "platform/iplatform.h", status: "✅ DONE", notes: "keys[512], mouseButtons[3], mouseDeltaX/Y, mouseWheel — polled per frame" },
        { name: "IRenderBackend", file: "renderer/irender_backend.h", status: "✅ DONE", notes: "Full interface: buffers, textures, samplers, shaders, draw calls, framebuffers" },
        { name: "OpenGLBackend", file: "renderer/gl/gl_backend.cpp", status: "✅ DONE", notes: "GL 4.5 DSA; global VAO; 44-byte vertex layout; UBO binding; sampler objects" },
        { name: "ShaderProgram", file: "renderer/gl/gl_backend.cpp", status: "✅ DONE", notes: "GLSL compile/link with error reporting; handles VS/FS/GS stages" },
        { name: "GpuBuffer", file: "renderer/gl/gl_backend.cpp", status: "✅ DONE", notes: "VBO/IBO/UBO; glNamedBufferSubData for dynamic updates (DSA, no rebind)" },
        { name: "GpuTexture", file: "renderer/gl/gl_backend.cpp", status: "✅ DONE", notes: "2D and cubemap; correct internal/base format derivation; sampler objects" },
        { name: "BSP Loader", file: "renderer/bsp/bsp_loader.cpp", status: "✅ DONE", notes: "FIXED: file size from fseek not lump offsets (prevents OOM crash). Index fan uses per-face baseVertex offset (fixes geometry crash on large maps). Supports Q2 v38 vanilla + v46 KEX." },
        { name: "BSP Renderer", file: "renderer/bsp/bsp_loader.cpp", status: "🔄 IN PROGRESS", notes: "Flat draw-all geometry works. NEXT: per-surface lightmap texture bind; PVS culling" },
        { name: "Lightmap System", file: "renderer/bsp/bsp_loader.cpp", status: "🔄 IN PROGRESS", notes: "Raw lump stored; per-face extents computed; GPU textures uploaded. NEXT: atlas packing" },
        { name: "Camera", file: "core/camera.h / camera.cpp", status: "✅ DONE", notes: "Quaternion yaw/pitch; WASD movement (horizontal-plane locked); mouse look" },
        { name: "Engine Main Loop", file: "core/engine.cpp", status: "✅ DONE", notes: "Fixed-timestep loop; FPS counter in title; BSP load + spawn; UBO update per frame" },
    ]),

    para(""),
    h2("4.2 Key Interfaces Defined in Phase 1"),
    para("These interfaces must be finalized in Phase 1. Changing them later has cascading costs. Think carefully before finalizing."),
    para(""),
    interfaceTable([
        { iface: "IPlatform", impls: "SDL2Platform", desc: "Window creation, event polling, file system access, high-resolution timer, thread creation" },
        { iface: "IRenderBackend", impls: "OpenGLBackend", desc: "Create/destroy GPU resources (buffers, textures, shaders), submit draw calls, present frame. NEVER call OpenGL directly outside this class." },
        { iface: "IFileSystem", impls: "DefaultFileSystem", desc: "Open/read/write files; supports pack files (.pak) and loose files; abstracted for modding later" },
    ]),

    para(""),
    h2("4.3 Phase 1 Acceptance Criteria"),
    para("Phase 1 is complete when ALL of the following are true:"),
    bullet("A 1280x720 window opens without errors"),
    bullet("A Quake 2 .bsp file loads from disk and renders with correct lightmaps"),
    bullet("Camera moves with WASD + mouse look at 60+ FPS on integrated GPU"),
    bullet("All Phase 1 systems show status DONE in the table above"),
    bullet("No raw OpenGL calls exist outside renderer/gl/ directory"),
    bullet("tests/core/ and tests/renderer/ all pass"),
    divider(),
);

// ── SECTION 5: PHASE 2 ────────────────────────────────────────
children.push(
    pageBreak(),
    h1("5. Phase 2 — Quake 2 Core (Months 6–12)"),
    para("Phase 2 transforms the renderer demo into a playable game prototype. By the end of Phase 2, a simple arena FPS can run: a player moves through a level, shoots projectiles, and the game logic lives in a separate DLL. Networking supports a basic client-server game."),
    para(""),

    h2("5.1 System Status — Phase 2"),
    systemTable([
        { name: "IGameModule interface", file: "engine/igame_module.h", status: "⬜ TODO", notes: "Engine<->game contract: init, shutdown, think, onEntitySpawn, onCollision" },
        { name: "GameDLL loader", file: "engine/game_dll_loader.cpp", status: "⬜ TODO", notes: "dlopen/LoadLibrary; hot-reload on file change for fast iteration" },
        { name: "EntityID / EntityHandle", file: "entities/entity_id.h", status: "⬜ TODO", notes: "Generational index (16-bit gen + 16-bit index); safe stale detection" },
        { name: "EntityList", file: "entities/entity_list.cpp", status: "⬜ TODO", notes: "Flat pool of Entity structs; O(1) create/destroy; iterate active only" },
        { name: "Entity (base struct)", file: "entities/entity.h", status: "⬜ TODO", notes: "origin, angles, velocity, bbox, think/touch/use fn ptrs, classname" },
        { name: "EntityFactory", file: "entities/entity_factory.cpp", status: "⬜ TODO", notes: "Registry of classname -> spawn function; loaded from game DLL" },
        { name: "IPhysicsWorld interface", file: "physics/iphysics_world.h", status: "⬜ TODO", notes: "createBody, step, raycast, sweepAABB — pure virtual; swappable backend" },
        { name: "AABBPhysics", file: "physics/aabb/aabb_physics.cpp", status: "⬜ TODO", notes: "Quake-style player movement: PM_SlideMove, PM_StepSlideMove" },
        { name: "BSP Collision", file: "physics/aabb/bsp_trace.cpp", status: "⬜ TODO", notes: "CM_BoxTrace against BSP planes; returns trace_t (fraction, normal, ent)" },
        { name: "PlayerMove", file: "physics/aabb/player_move.cpp", status: "⬜ TODO", notes: "Ground/air/water movement; wish velocity; friction; gravity; step-up" },
        { name: "INetworkSystem interface", file: "network/inetwork.h", status: "⬜ TODO", notes: "connect, disconnect, sendPacket, receivePackets — pure virtual" },
        { name: "Server", file: "network/server.cpp", status: "⬜ TODO", notes: "Authoritative sim; builds delta snapshots; broadcasts to clients" },
        { name: "Client", file: "network/client.cpp", status: "⬜ TODO", notes: "Sends input; receives snapshots; client-side prediction + interpolation" },
        { name: "DeltaCompressor", file: "network/delta.cpp", status: "⬜ TODO", notes: "Diff two entity snapshots; encode changed fields only (bit flags)" },
        { name: "PacketBuffer", file: "network/packet.cpp", status: "⬜ TODO", notes: "Reliable + unreliable channels; sequence numbers; ack tracking" },
        { name: "IAudioSystem interface", file: "audio/iaudio.h", status: "⬜ TODO", notes: "loadSound, playSound, play3D, stopAll — pure virtual" },
        { name: "OpenALAudio", file: "audio/openal/openal_audio.cpp", status: "⬜ TODO", notes: "3D positional audio; WAV/OGG playback; distance attenuation" },
        { name: "ConsoleVar (cvar)", file: "engine/cvar.cpp", status: "⬜ TODO", notes: "Runtime variables (sv_gravity, cl_fov etc.); serialized to config.cfg" },
        { name: "Console", file: "engine/console.cpp", status: "⬜ TODO", notes: "In-game drop-down console; command parsing; cvar get/set" },
        { name: "MapLoader (.bsp spawn)", file: "entities/map_loader.cpp", status: "⬜ TODO", notes: "Parse entity lump from .bsp; call EntityFactory to spawn all entities" },
        { name: "Model Renderer (MD2)", file: "renderer/models/md2.cpp", status: "⬜ TODO", notes: "Load Quake 2 .md2 vertex-animated models; lerp between frames" },
        { name: "Sprite Renderer", file: "renderer/sprite.cpp", status: "⬜ TODO", notes: "Billboard sprites for particles, explosions, pickups" },
        { name: "Particle System (basic)", file: "renderer/particles/particles.cpp", status: "⬜ TODO", notes: "CPU-simulated particles: spawn, update, fade, billboard render" },
        { name: "HUD / 2D Renderer", file: "renderer/hud/hud.cpp", status: "⬜ TODO", notes: "Orthographic quads for health bar, ammo counter, crosshair" },
    ]),

    para(""),
    h2("5.2 Networking Architecture Diagram"),
    para("The following describes the server-client data flow per tick:"),
    para(""),
    ...codeBlock([
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
    ]),
    para(""),
    h2("5.3 Phase 2 Acceptance Criteria"),
    bullet("Player can load a map, spawn, move (walk/jump/swim), and shoot a projectile"),
    bullet("Game logic (damage, pickups, doors) lives entirely in game.dll with zero engine changes"),
    bullet("Two clients can connect to a local server and see each other moving"),
    bullet("Console opens with tilde (~), cvars are readable and settable"),
    bullet("Audio plays 3D sounds attached to entities"),
    bullet("All Phase 2 systems show DONE in the table above"),
    divider(),
);

// ── SECTION 6: PHASE 3 ────────────────────────────────────────
children.push(
    pageBreak(),
    h1("6. Phase 3 — Source Engine Bridge (Months 12–24)"),
    para("Phase 3 upgrades visual and gameplay capabilities to Source-engine parity. Crucially, none of Phase 1 or Phase 2 code is discarded — it is extended through the existing interfaces. The renderer gains normal maps, HDR, and shadow mapping. Physics swaps to Jolt. Lua scripting is embedded."),
    para(""),

    h2("6.1 System Status — Phase 3"),
    systemTable([
        { name: "Normal Map Support", file: "renderer/gl/gl_normalmap.cpp", status: "⬜ TODO", notes: "Tangent-space normal maps; TBN matrix per vertex; add to BSP surfaces" },
        { name: "Specular / Roughness Maps", file: "renderer/materials/material.cpp", status: "⬜ TODO", notes: "PBR-lite: albedo + normal + roughness + metallic (4 textures per surface)" },
        { name: "HDR Framebuffer", file: "renderer/gl/gl_hdr.cpp", status: "⬜ TODO", notes: "Render to float16 FBO; tone-map (Reinhard/ACES) to LDR for display" },
        { name: "Shadow Mapping", file: "renderer/shadows/shadowmap.cpp", status: "⬜ TODO", notes: "PCF shadow maps for sun/spot lights; cascade for large outdoor maps" },
        { name: "Bloom Post-Process", file: "renderer/post/bloom.cpp", status: "⬜ TODO", notes: "Downsample bright areas; Gaussian blur; additive blend over scene" },
        { name: "Displacement Geometry", file: "renderer/bsp/displacement.cpp", status: "⬜ TODO", notes: "Subdivide BSP faces into displacement grid; edit height in map editor" },
        { name: "Displacement Compiler", file: "tools/qbsp/displacement.cpp", status: "⬜ TODO", notes: "Compile displacement data into .bsp; store LOD levels" },
        { name: "IPhysicsWorld (Jolt)", file: "physics/jolt/jolt_world.cpp", status: "⬜ TODO", notes: "Swap AABBPhysics -> JoltPhysics behind IPhysicsWorld; keep interface" },
        { name: "Rigid Body Component", file: "physics/jolt/rigid_body.cpp", status: "⬜ TODO", notes: "Physics-driven entities: barrels, crates, ragdolls" },
        { name: "IScriptEngine interface", file: "scripting/iscript_engine.h", status: "⬜ TODO", notes: "loadScript, callFunction, exposeObject — pure virtual" },
        { name: "LuaScriptEngine", file: "scripting/lua/lua_engine.cpp", status: "⬜ TODO", notes: "Embed Lua 5.4; bind Entity, World, Console, Events via sol2 or manual" },
        { name: "Script Entity Bindings", file: "scripting/lua/bindings.cpp", status: "⬜ TODO", notes: "Expose entity.origin, entity.think, world.trace() etc. to Lua" },
        { name: "Decal System", file: "renderer/decals/decal.cpp", status: "⬜ TODO", notes: "Projected decals on BSP surfaces: bullet holes, scorch marks, blood" },
        { name: "Dynamic Lights", file: "renderer/lights/dyn_light.cpp", status: "⬜ TODO", notes: "Up to 256 point/spot lights per frame; frustum-culled; affect models" },
        { name: "Env Cubemap / Reflections", file: "renderer/env/cubemap.cpp", status: "⬜ TODO", notes: "Per-zone env_cubemap; sample in material shader for reflections" },
        { name: "Enhanced Particle System", file: "renderer/particles/gpu_particles.cpp", status: "⬜ TODO", notes: "GPU-simulated particles using transform feedback or compute shader" },
        { name: "Skeletal Animation (SMD)", file: "renderer/models/skeletal.cpp", status: "⬜ TODO", notes: "Bone-weighted skinning; load Valve SMD or GLTF; blend between clips" },
        { name: "Animation State Machine", file: "renderer/models/anim_state.cpp", status: "⬜ TODO", notes: "States + transitions + blend weights; driven by game logic or Lua" },
        { name: "Sound Occlusion", file: "audio/occlusion.cpp", status: "⬜ TODO", notes: "Raycast through BSP to attenuate occluded sounds (low-pass filter)" },
        { name: "Reverb Zones", file: "audio/reverb.cpp", status: "⬜ TODO", notes: "Map-placed reverb volumes (cave, tunnel, outdoor) via OpenAL EFX" },
    ]),

    para(""),
    h2("6.2 Renderer Upgrade Path"),
    para("The renderer evolves without rewriting. Each upgrade adds a new pass or extends the material system:"),
    para(""),
    ...codeBlock([
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
    ]),
    para(""),
    h2("6.3 Phase 3 Acceptance Criteria"),
    bullet("A level with normal maps, dynamic lights, and shadow maps renders correctly"),
    bullet("A physics prop (crate) can be pushed around using Jolt rigid body"),
    bullet("A Lua script can spawn an entity, set its think function, and react to player proximity"),
    bullet("Particle effects (smoke, sparks) emit from entities at runtime"),
    bullet("Skeletal model plays and blends idle/walk/attack animations"),
    divider(),
);

// ── SECTION 7: PHASE 4 ────────────────────────────────────────
children.push(
    pageBreak(),
    h1("7. Phase 4 — Modern Engine (Months 24+)"),
    para("Phase 4 is the long-term evolution. The engine becomes Vulkan-based, PBR-native, and includes a level editor. This phase has no hard deadline — systems are added incrementally. Phase 3 must be fully complete before Phase 4 begins."),
    para(""),

    h2("7.1 System Status — Phase 4"),
    systemTable([
        { name: "VulkanBackend", file: "renderer/vulkan/vk_backend.cpp", status: "⬜ TODO", notes: "Swap OpenGLBackend -> VulkanBackend behind IRenderBackend; keep API" },
        { name: "Vulkan Resource Manager", file: "renderer/vulkan/vk_resources.cpp", status: "⬜ TODO", notes: "Descriptor sets, render passes, pipeline cache, memory allocator" },
        { name: "GPU-Driven Rendering", file: "renderer/vulkan/gpu_culling.cpp", status: "⬜ TODO", notes: "Indirect draw calls; GPU frustum + occlusion cull; no CPU per-object" },
        { name: "PBR Material System", file: "renderer/materials/pbr.cpp", status: "⬜ TODO", notes: "Full PBR: albedo, normal, roughness, metallic, AO, emissive maps" },
        { name: "Screen-Space Reflections", file: "renderer/post/ssr.cpp", status: "⬜ TODO", notes: "Raymarched SSR in screen space; fallback to cubemaps" },
        { name: "Ambient Occlusion (SSAO)", file: "renderer/post/ssao.cpp", status: "⬜ TODO", notes: "HBAO+ style; 16 samples; blur; modulate diffuse lighting" },
        { name: "Global Illumination", file: "renderer/gi/gi.cpp", status: "⬜ TODO", notes: "Lightprobes / irradiance volumes; baked or dynamic (Lumen-style later)" },
        { name: "Level Editor (Nova Edit)", file: "editor/", status: "⬜ TODO", notes: "Hammer-like editor: brush drawing, entity placement, compile pipeline" },
        { name: "Editor Renderer", file: "editor/editor_renderer.cpp", status: "⬜ TODO", notes: "Grid, wireframe overlay, selection highlight, gizmos (translate/rotate)" },
        { name: "Temporal Anti-Aliasing", file: "renderer/post/taa.cpp", status: "⬜ TODO", notes: "Accumulate samples across frames; jitter projection; motion vectors" },
        { name: "Streaming / LOD System", file: "engine/streaming/lod.cpp", status: "⬜ TODO", notes: "LOD chain per model; distance-based swap; async streaming from disk" },
        { name: "Async Asset Pipeline", file: "engine/assets/asset_manager.cpp", status: "⬜ TODO", notes: "Background thread loads textures/models; main thread gets handles" },
        { name: "Profiler / RenderDoc Hook", file: "engine/profiler/profiler.cpp", status: "⬜ TODO", notes: "CPU/GPU timers per pass; overlay UI; integrate RenderDoc capture API" },
    ]),

    para(""),
    h2("7.2 Phase 4 Acceptance Criteria"),
    bullet("VulkanBackend passes all tests that OpenGLBackend previously passed"),
    bullet("A full PBR scene renders with SSR, SSAO, and TAA at 60 FPS on mid-range GPU"),
    bullet("Level editor opens a .bsp, allows brush editing, and recompiles in-editor"),
    bullet("Async asset loading shows no frame hitches when moving through a large level"),
    divider(),
);

// ── SECTION 8: CODING CONVENTIONS ─────────────────────────────
children.push(
    pageBreak(),
    h1("8. Coding Conventions & Rules"),
    para("All contributors (human or AI) must follow these rules. Violations must be fixed before merging."),
    para(""),

    h2("8.1 Naming"),
    ...codeBlock([
        "Classes / Structs:    PascalCase          e.g. RenderBackend, EntityList",
        "Interfaces:           IPascalCase         e.g. IRenderBackend, IPhysicsWorld",
        "Functions:            camelCase           e.g. loadShader(), stepPhysics()",
        "Member variables:     m_camelCase         e.g. m_vertexBuffer, m_entityCount",
        "Constants / Enums:    UPPER_SNAKE_CASE    e.g. MAX_ENTITIES, BSP_VERSION",
        "Files:                snake_case.cpp/.h   e.g. bsp_loader.cpp, gl_texture.h",
    ]),

    para(""),
    h2("8.2 Include Rules"),
    bullet("No module may include headers from a module that depends on it (no circular deps)"),
    bullet("Engine modules never include game/ headers — only game/ includes engine/"),
    bullet("renderer/ never includes physics/ or network/ — pass data through structs"),
    bullet("Use forward declarations in .h files; #include in .cpp files only"),
    para(""),

    h2("8.3 Memory Rules"),
    bullet("No naked new/delete in engine code — use allocators from core/memory/"),
    bullet("All per-frame allocations use MemoryArena and are reset at frame end"),
    bullet("All persistent game objects use ZoneAllocator or PoolAllocator"),
    bullet("Render resources (buffers, textures) managed exclusively by IRenderBackend"),
    para(""),

    h2("8.4 Interface Rules"),
    bullet("Every swappable system must have a pure virtual interface class prefixed with I"),
    bullet("No code outside a module's own directory calls concrete implementation classes"),
    bullet("Interface methods return error codes (enum Result), not throw exceptions"),
    bullet("All interface destructors are virtual"),
    para(""),

    h2("8.5 Comment Header Format"),
    para("Every .cpp and .h file must begin with this header:"),
    ...codeBlock([
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
    ]),
    divider(),
);

// ── SECTION 9: BUILD SYSTEM ────────────────────────────────────
children.push(
    pageBreak(),
    h1("9. Build System & Dependencies"),
    para(""),
    h2("9.1 CMake Structure"),
    ...codeBlock([
        "CMakeLists.txt              # Root: sets C++20, warnings, platform flags",
        "engine/CMakeLists.txt       # Builds nova_engine static lib",
        "game/CMakeLists.txt         # Builds game shared lib (nova_game.dll/.so)",
        "tools/CMakeLists.txt        # Builds qbsp, light, vis executables",
        "tests/CMakeLists.txt        # Builds all test executables via CTest",
    ]),

    para(""),
    h2("9.2 External Dependencies"),
    systemTable([
        { name: "SDL2", file: "platform/sdl2/", status: "⬜ TODO", notes: "v2.28+; window, input, OpenGL context, file I/O wrappers" },
        { name: "OpenGL 4.5", file: "renderer/gl/", status: "⬜ TODO", notes: "Via GLAD loader; 4.5 core profile; DSA (direct state access) for clarity" },
        { name: "GLAD", file: "vendor/glad/", status: "⬜ TODO", notes: "OpenGL function loader; generate for GL 4.5 + extensions needed" },
        { name: "GLM", file: "vendor/glm/", status: "⬜ TODO", notes: "Header-only math; used only in tool scripts — engine uses its own math" },
        { name: "OpenAL Soft", file: "audio/openal/", status: "⬜ TODO", notes: "v1.23+; EFX extension for reverb; cross-platform" },
        { name: "stb_image", file: "vendor/stb/", status: "⬜ TODO", notes: "Single-header image loader for PNG/JPG/TGA texture loading" },
        { name: "Jolt Physics", file: "physics/jolt/", status: "⬜ TODO", notes: "Phase 3+; MIT license; excellent performance; replace AABBPhysics" },
        { name: "Lua 5.4", file: "scripting/lua/", status: "⬜ TODO", notes: "Phase 3+; embed as static lib; bind via sol2 or manual stack API" },
        { name: "Vulkan SDK", file: "renderer/vulkan/", status: "⬜ TODO", notes: "Phase 4+; VMA for memory allocation; vkbootstrap for init" },
        { name: "Catch2", file: "vendor/catch2/", status: "⬜ TODO", notes: "Unit test framework; one test executable per module" },
    ]),
    divider(),
);

// ── SECTION 10: TESTING ───────────────────────────────────────
children.push(
    h1("10. Testing Strategy"),
    para("Every system must have at minimum one test file. Tests live in tests/ and mirror the engine directory structure. Tests are run via CTest."),
    para(""),
    systemTable([
        { name: "tests/core/test_math.cpp", file: "Tests: Vec3, Mat4, Quat, AABB", status: "⬜ TODO", notes: "Test dot/cross/normalize, mat4 mul, frustum planes, AABB overlap" },
        { name: "tests/core/test_memory.cpp", file: "Tests: Arena, Zone, Pool", status: "⬜ TODO", notes: "Alloc/free/reset; out-of-memory handling; alignment checks" },
        { name: "tests/renderer/test_bsp.cpp", file: "Tests: BSP load + PVS", status: "⬜ TODO", notes: "Load test.bsp; verify leaf count, PVS bits, surface counts" },
        { name: "tests/physics/test_trace.cpp", file: "Tests: BSP trace, AABB sweep", status: "⬜ TODO", notes: "Trace against known geometry; verify fraction, normal, hit entity" },
        { name: "tests/network/test_delta.cpp", file: "Tests: Delta compress/decompress", status: "⬜ TODO", notes: "Encode two snapshots; decode; verify byte-for-byte equality" },
        { name: "tests/entities/test_elist.cpp", file: "Tests: EntityList create/destroy", status: "⬜ TODO", notes: "Create 1000 entities; destroy random subset; verify handles" },
    ]),
    divider(),
);

// ── SECTION 11: QUICK REFERENCE ───────────────────────────────
children.push(
    pageBreak(),
    h1("11. Quick Reference — What To Do Next"),
    para("This section answers the question: 'I just opened this project — where do I start?' Follow this order strictly. Do not skip ahead."),
    para(""),
    h2("If ALL systems are TODO (fresh start):"),
    bullet("Step 1: Set up CMake project structure and empty module directories"),
    bullet("Step 2: Implement core/math — Vec3, Mat4, Plane, AABB, Ray"),
    bullet("Step 3: Implement core/memory — Arena, Zone, Pool allocators"),
    bullet("Step 4: Implement Logger"),
    bullet("Step 5: Define IPlatform interface; implement SDL2Platform (window + input only)"),
    bullet("Step 6: Define IRenderBackend interface"),
    bullet("Step 7: Implement OpenGLBackend (clear screen, triangle on screen)"),
    bullet("Step 8: Implement BSP Loader"),
    bullet("Step 9: Implement BSP Renderer + Lightmap System"),
    bullet("Step 10: Implement Camera — you now have Phase 1"),
    para(""),
    h2("If Phase 1 is DONE, starting Phase 2:"),
    bullet("Step 1: Define IGameModule; build GameDLL loader"),
    bullet("Step 2: Implement EntityList + Entity struct + EntityFactory"),
    bullet("Step 3: Define IPhysicsWorld; implement AABBPhysics + BSP trace"),
    bullet("Step 4: Implement PlayerMove (Quake-style movement)"),
    bullet("Step 5: Implement MapLoader (spawn entities from .bsp entity lump)"),
    bullet("Step 6: Implement Server + Client + DeltaCompressor + PacketBuffer"),
    bullet("Step 7: Implement OpenALAudio behind IAudioSystem"),
    bullet("Step 8: Implement cvar system + Console"),
    para(""),
    h2("If Phase 2 is DONE, starting Phase 3:"),
    bullet("Step 1: Upgrade surface shader with normal map + specular support"),
    bullet("Step 2: Add HDR framebuffer + tone mapping post-process"),
    bullet("Step 3: Add shadow map pre-pass for sun light"),
    bullet("Step 4: Implement displacement geometry in BSP renderer"),
    bullet("Step 5: Swap AABBPhysics for JoltPhysics behind IPhysicsWorld"),
    bullet("Step 6: Embed Lua 5.4; define IScriptEngine; bind entity API"),
    bullet("Step 7: Add skeletal animation pipeline"),
    divider(),
);

// ── SECTION 12: CHANGE LOG ────────────────────────────────────
children.push(
    h1("12. Revision History"),
    new Table({
        width: { size: 9360, type: WidthType.DXA },
        columnWidths: [1200, 1600, 2200, 4360],
        rows: [
            new TableRow({ children: ["Rev", "Date", "Author", "Changes"].map((t, i) => headerCell(t, [1200, 1600, 2200, 4360][i])) }),
            new TableRow({
                children: [
                    cell("1.0", { width: 1200, bg: C.rowNorm, bold: true }),
                    cell("TBD", { width: 1600, bg: C.rowNorm }),
                    cell("Project Lead", { width: 2200, bg: C.rowNorm }),
                    cell("Initial document creation — all four phases defined, all systems marked TODO", { width: 4360, bg: C.rowNorm }),
                ]
            }),
        ]
    }),
);

// ══════════════════════════════════════════════════════════════
//  ASSEMBLE DOCUMENT
// ══════════════════════════════════════════════════════════════
const doc = new Document({
    numbering: {
        config: [
            {
                reference: "bullets",
                levels: [{
                    level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT,
                    style: { paragraph: { indent: { left: 720, hanging: 360 } } }
                }]
            },
            {
                reference: "subbullets",
                levels: [{
                    level: 0, format: LevelFormat.BULLET, text: "◦", alignment: AlignmentType.LEFT,
                    style: { paragraph: { indent: { left: 1080, hanging: 360 } } }
                }]
            },
        ]
    },
    styles: {
        default: { document: { run: { font: "Arial", size: 22, color: C.textMain } } },
        paragraphStyles: [
            {
                id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
                run: { size: 36, bold: true, font: "Arial", color: C.navy },
                paragraph: { spacing: { before: 360, after: 180 }, outlineLevel: 0 }
            },
            {
                id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
                run: { size: 28, bold: true, font: "Arial", color: C.accentSoft },
                paragraph: { spacing: { before: 280, after: 140 }, outlineLevel: 1 }
            },
            {
                id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
                run: { size: 24, bold: true, font: "Arial", color: C.darkNavy },
                paragraph: { spacing: { before: 200, after: 100 }, outlineLevel: 2 }
            },
        ]
    },
    sections: [{
        properties: {
            page: {
                size: { width: 12240, height: 15840 },
                margin: { top: 1080, right: 1080, bottom: 1080, left: 1080 }
            }
        },
        headers: {
            default: new Header({
                children: [new Paragraph({
                    border: { bottom: { style: BorderStyle.SINGLE, size: 4, color: C.lightBorder, space: 1 } },
                    children: [
                        new TextRun({ text: "NOVA ENGINE — Development Master Plan", font: "Arial", size: 18, color: C.textSub, bold: true }),
                        new TextRun({ text: "   |   Confidential Draft", font: "Arial", size: 18, color: C.textSub }),
                    ]
                })]
            })
        },
        footers: {
            default: new Footer({
                children: [new Paragraph({
                    border: { top: { style: BorderStyle.SINGLE, size: 4, color: C.lightBorder, space: 1 } },
                    tabStops: [{ type: TabStopType.RIGHT, position: TabStopPosition.MAX }],
                    children: [
                        new TextRun({ text: "© NOVA Engine Project", font: "Arial", size: 18, color: C.textSub }),
                        new TextRun({ text: "\tPage ", font: "Arial", size: 18, color: C.textSub }),
                        new TextRun({
                            children: ["PAGE"],
                            font: "Arial",
                            size: 18,
                            color: C.textSub,
                        }),
                    ]
                })]
            })
        },
        children,
    }]
});

Packer.toBuffer(doc)
    .then(buf => {
        fs.writeFileSync("./NOVA_ENGINE_PLAN.docx", buf);
        console.log("✅ NOVA_ENGINE_PLAN.docx generated successfully");
    })
    .catch(err => {
        console.error("❌ Error generating document:", err);
    });