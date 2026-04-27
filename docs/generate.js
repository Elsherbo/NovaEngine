"use strict";
// ============================================================
// FILE:    docs/generate.js
// PURPOSE: Reads ENGINE_PLAN_DATA.js and generates NOVA_ENGINE_PLAN.docx
//          DO NOT put content here. Edit ENGINE_PLAN_DATA.js instead.
// USAGE:   node generate.js
// ============================================================

const {
    Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
    HeadingLevel, AlignmentType, BorderStyle, WidthType, ShadingType,
    LevelFormat, PageNumber, PageBreak, TabStopType, TabStopPosition,
    Header, Footer,
} = require("docx");
const fs = require("fs");
const path = require("path");

const DATA = require("./ENGINE_PLAN_DATA.js");
const { META, PHASE_SUMMARY, DIRECTORY_STRUCTURE, PHASES, CONVENTIONS, BUILD, TESTS, QUICK_REFERENCE, CHANGELOG } = DATA;

// ── Color Palette ──────────────────────────────────────────────
const C = {
    black:       "1A1A2E",
    darkNavy:    "16213E",
    navy:        "0F3460",
    accent:      "E94560",
    accentSoft:  "533483",
    headerBg:    "0F3460",
    headerFg:    "FFFFFF",
    rowAlt:      "F0F4FF",
    rowNorm:     "FFFFFF",
    done:        "1E7E34",
    doneBg:      "D4EDDA",
    inprog:      "856404",
    inprogBg:    "FFF3CD",
    todo:        "721C24",
    todoBg:      "F8D7DA",
    missing:     "495057",
    missingBg:   "E2E3E5",
    border:      "CCCCCC",
    lightBorder: "E0E4EF",
    textMain:    "1A1A2E",
    textSub:     "444466",
    codeBg:      "F4F6FB",
    codeFg:      "2D2D5E",
};

// ── Helpers ────────────────────────────────────────────────────
const bdr  = (color = C.border) => ({ style: BorderStyle.SINGLE, size: 1, color });
const bdrs = (color = C.border) => ({ top: bdr(color), bottom: bdr(color), left: bdr(color), right: bdr(color) });
const noBdr  = () => ({ style: BorderStyle.NONE, size: 0, color: "FFFFFF" });
const noBdrs = () => ({ top: noBdr(), bottom: noBdr(), left: noBdr(), right: noBdr() });

function cell(text, opts = {}) {
    const { bold = false, color = C.textMain, bg = C.rowNorm, width = 3120,
            size = 20, italic = false, align = AlignmentType.LEFT, borders: b = bdrs() } = opts;
    return new TableCell({
        borders: b, width: { size: width, type: WidthType.DXA },
        shading: { fill: bg, type: ShadingType.CLEAR },
        margins: { top: 80, bottom: 80, left: 140, right: 140 },
        children: [new Paragraph({
            alignment: align,
            children: [new TextRun({ text: String(text), bold, color, size, italics: italic, font: "Arial" })]
        })]
    });
}

function headerCell(text, width = 3120) {
    return cell(text, { bold: true, color: C.headerFg, bg: C.headerBg, width, size: 20 });
}

function statusCell(status) {
    const map = {
        "✅ DONE":        { color: C.done,    bg: C.doneBg    },
        "🔄 IN PROGRESS": { color: C.inprog,  bg: C.inprogBg  },
        "⬜ TODO":        { color: C.todo,    bg: C.todoBg    },
        "❌ MISSING":     { color: C.missing, bg: C.missingBg },
    };
    const s = map[status] || { color: C.textMain, bg: C.rowNorm };
    return cell(status, { bold: true, color: s.color, bg: s.bg, width: 1800, size: 18, align: AlignmentType.CENTER });
}

function h1(text) {
    return new Paragraph({
        heading: HeadingLevel.HEADING_1, spacing: { before: 360, after: 180 },
        children: [new TextRun({ text, bold: true, font: "Arial", size: 36, color: C.navy })]
    });
}
function h2(text) {
    return new Paragraph({
        heading: HeadingLevel.HEADING_2, spacing: { before: 280, after: 140 },
        children: [new TextRun({ text, bold: true, font: "Arial", size: 28, color: C.accentSoft })]
    });
}
function h3(text) {
    return new Paragraph({
        heading: HeadingLevel.HEADING_3, spacing: { before: 200, after: 100 },
        children: [new TextRun({ text, bold: true, font: "Arial", size: 24, color: C.darkNavy })]
    });
}
function para(text, opts = {}) {
    const { bold = false, color = C.textMain, size = 22, spacing = { before: 60, after: 60 } } = opts;
    return new Paragraph({ spacing, children: [new TextRun({ text: String(text), bold, color, size, font: "Arial" })] });
}
function bullet(text, opts = {}) {
    const { bold = false, color = C.textMain, size = 21 } = opts;
    return new Paragraph({
        numbering: { reference: "bullets", level: 0 }, spacing: { before: 40, after: 40 },
        children: [new TextRun({ text: String(text), bold, color, size, font: "Arial" })]
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
        children: [new TextRun({ text: String(line), font: "Courier New", size: 18, color: C.codeFg })]
    }));
}
function pageBreak() { return new Paragraph({ children: [new PageBreak()] }); }

// ── Reusable Tables ────────────────────────────────────────────
function systemTable(systems) {
    const cols = [2100, 2400, 1800, 3060];
    return new Table({
        width: { size: 9360, type: WidthType.DXA }, columnWidths: cols,
        rows: [
            new TableRow({ children: ["System / Class", "Source File(s)", "Status", "Notes / Next Steps"].map((t, i) => headerCell(t, cols[i])) }),
            ...systems.map((s, i) => new TableRow({ children: [
                cell(s.name,   { width: cols[0], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, bold: true, size: 19 }),
                cell(s.file,   { width: cols[1], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, color: C.codeFg, italic: true }),
                statusCell(s.status),
                cell(s.notes,  { width: cols[3], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, color: C.textSub }),
            ]}))
        ]
    });
}

function fileTreeTable(entries) {
    const cols = [3200, 2000, 4160];
    return new Table({
        width: { size: 9360, type: WidthType.DXA }, columnWidths: cols,
        rows: [
            new TableRow({ children: ["Path", "Type", "Purpose"].map((t, i) => headerCell(t, cols[i])) }),
            ...entries.map((e, i) => new TableRow({ children: [
                cell(e.path,    { width: cols[0], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, color: C.codeFg, italic: true }),
                cell(e.type,    { width: cols[1], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, bold: true }),
                cell(e.purpose, { width: cols[2], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18 }),
            ]}))
        ]
    });
}

function interfaceTable(interfaces) {
    const cols = [2200, 2200, 4960];
    return new Table({
        width: { size: 9360, type: WidthType.DXA }, columnWidths: cols,
        rows: [
            new TableRow({ children: ["Interface", "Implementations", "Description"].map((t, i) => headerCell(t, cols[i])) }),
            ...interfaces.map((e, i) => new TableRow({ children: [
                cell(e.iface, { width: cols[0], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, bold: true, size: 19, color: C.navy }),
                cell(e.impls, { width: cols[1], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18, color: C.codeFg, italic: true }),
                cell(e.desc,  { width: cols[2], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18 }),
            ]}))
        ]
    });
}

function legendTable() {
    const items = [
        { s: "✅ DONE",        d: "Fully implemented, tested, and stable" },
        { s: "🔄 IN PROGRESS", d: "Work has started, partially complete" },
        { s: "⬜ TODO",        d: "Planned but not yet started" },
        { s: "❌ MISSING",     d: "Required but not yet designed/planned" },
    ];
    return new Table({
        width: { size: 9360, type: WidthType.DXA }, columnWidths: [2200, 7160],
        rows: [
            new TableRow({ children: [headerCell("Status", 2200), headerCell("Meaning", 7160)] }),
            ...items.map((it, i) => new TableRow({ children: [
                statusCell(it.s),
                cell(it.d, { width: 7160, bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 20 })
            ]}))
        ]
    });
}

function phaseSummaryTable() {
    const cols = [1200, 1700, 1600, 3760, 1100];
    const headers = ["Phase", "Name", "Timeline", "Key Deliverables", "Status"];
    return new Table({
        width: { size: 9360, type: WidthType.DXA }, columnWidths: cols,
        rows: [
            new TableRow({ children: headers.map((t, i) => headerCell(t, cols[i])) }),
            ...PHASE_SUMMARY.map((p, ri) => new TableRow({ children: [
                cell(p.phase,        { width: cols[0], bg: ri % 2 === 0 ? C.rowNorm : C.rowAlt, bold: true, size: 19 }),
                cell(p.name,         { width: cols[1], bg: ri % 2 === 0 ? C.rowNorm : C.rowAlt, bold: true, size: 19 }),
                cell(p.timeline,     { width: cols[2], bg: ri % 2 === 0 ? C.rowNorm : C.rowAlt, size: 19 }),
                cell(p.deliverables, { width: cols[3], bg: ri % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18 }),
                statusCell(p.status),
            ]}))
        ]
    });
}

function changelogTable() {
    const cols = [1200, 1600, 2200, 4360];
    return new Table({
        width: { size: 9360, type: WidthType.DXA }, columnWidths: cols,
        rows: [
            new TableRow({ children: ["Rev", "Date", "Author", "Changes"].map((t, i) => headerCell(t, cols[i])) }),
            ...CHANGELOG.map((c, i) => new TableRow({ children: [
                cell(c.rev,     { width: cols[0], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, bold: true }),
                cell(c.date,    { width: cols[1], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt }),
                cell(c.author,  { width: cols[2], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt }),
                cell(c.changes, { width: cols[3], bg: i % 2 === 0 ? C.rowNorm : C.rowAlt, size: 18 }),
            ]}))
        ]
    });
}

// ── Build document children array ─────────────────────────────
const children = [];

// ── Cover ──────────────────────────────────────────────────────
children.push(
    new Paragraph({
        spacing: { before: 720, after: 200 }, alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: META.title, bold: true, font: "Arial", size: 72, color: C.navy })]
    }),
    new Paragraph({
        spacing: { before: 0, after: 160 }, alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: META.subtitle, font: "Arial", size: 32, color: C.accentSoft })]
    }),
    new Paragraph({
        spacing: { before: 0, after: 80 }, alignment: AlignmentType.CENTER,
        children: [new TextRun({ text: META.tagline, font: "Arial", size: 24, color: C.textSub, italics: true })]
    }),
    new Paragraph({
        spacing: { before: 200, after: 600 }, alignment: AlignmentType.CENTER,
        border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: C.accent, space: 1 } },
        children: [new TextRun({ text: `Revision ${META.revision}  |  Start Date: ${META.startDate}  |  Engine Codename: ${META.codename}`, font: "Arial", size: 20, color: C.textSub })]
    }),
);

// ── Section 1: Purpose ─────────────────────────────────────────
children.push(
    h1("1. Document Purpose & How to Use This Plan"),
    para("This document is the single source of truth for the NOVA Engine project. Any developer, AI assistant, or collaborator reading this document alongside the codebase should be able to instantly answer three questions:"),
    bullet("What has been built and is working?"),
    bullet("What is currently being worked on?"),
    bullet("What is planned but not yet started, and what is the correct order to build it?"),
    para(""),
    para("This plan is organized into four phases. Each phase contains a system breakdown table that maps every major system to its source file(s), current status, and next steps. As development progresses, the status column in each table must be updated to reflect reality.", { color: C.textSub }),
    para(""),
    h2("1.1 Status Legend"),
    legendTable(),
    para(""),
    para("IMPORTANT: Every table in this document uses these exact four statuses. Mark a system IN PROGRESS the moment you start it, and DONE only when it compiles, runs, and passes its basic test case.", { bold: true, color: C.navy }),
    divider(),
);

// ── Section 2: Vision ──────────────────────────────────────────
children.push(
    h1("2. Engine Vision & Architecture Philosophy"),
    h2("2.1 Goal"),
    para("Build a game engine that starts at Quake 2 (id Tech 2) complexity and grows into Source Engine capabilities — without ever rewriting core systems from scratch. Every system is designed with an abstraction layer so it can be upgraded, swapped, or extended without breaking dependents."),
    para(""),
    h2("2.2 Core Architecture Principles"),
    bullet("Abstraction First: Every major system (renderer, physics, audio, scripting) is hidden behind a pure virtual C++ interface. Nothing in the engine calls implementation details directly."),
    bullet("Isolation of Game Logic: Game code lives in a separate shared library (.dll / .so). The engine never links directly to game logic — only through IGameModule interface."),
    bullet("No Singletons for Core Systems: Systems are passed explicitly by pointer/reference. This enables multiple instances, testing, and future multiplayer scenarios."),
    bullet("Data-Oriented Where It Matters: Entity lists, render queues, and physics bodies use flat arrays for cache efficiency."),
    bullet("Platform Layer Wraps Everything: All window creation, input, file I/O, and threading go through a platform abstraction — only through IPlatform."),
    para(""),
    h2("2.3 Four-Phase Roadmap Overview"),
    phaseSummaryTable(),
    divider(),
);

// ── Section 3: Directory Structure ─────────────────────────────
children.push(
    pageBreak(),
    h1("3. Project Directory Structure"),
    para("Every file in the engine belongs to exactly one module. The directory structure reflects the dependency order — lower-level modules appear first. No module may include headers from a module listed after it."),
    para(""),
    fileTreeTable(DIRECTORY_STRUCTURE),
    divider(),
);

// ── Sections 4–7: One section per phase ───────────────────────
const sectionOffset = 4;

for (const phase of PHASES) {
    const secNum = sectionOffset + phase.number - 1;
    children.push(
        pageBreak(),
        h1(`${secNum}. Phase ${phase.number} — ${phase.name} (${phase.timeline})`),
        para(phase.goal),
        para(""),
        h2(`${secNum}.1 System Status — Phase ${phase.number}`),
        systemTable(phase.systems),
    );

    // Interfaces (Phase 1 only in data)
    if (phase.interfaces && phase.interfaces.length > 0) {
        children.push(
            para(""),
            h2(`${secNum}.2 Key Interfaces`),
            para("These interfaces must be finalized in Phase 1. Changing them later has cascading costs."),
            para(""),
            interfaceTable(phase.interfaces),
        );
    }

    // Code blocks
    if (phase.codeBlocks && phase.codeBlocks.length > 0) {
        for (const cb of phase.codeBlocks) {
            const subNum = phase.interfaces?.length > 0 ? 3 : 2;
            children.push(
                para(""),
                h2(`${secNum}.${subNum} ${cb.title}`),
                para(""),
                ...codeBlock(cb.lines),
            );
        }
    }

    // Acceptance criteria
    if (phase.criteria && phase.criteria.length > 0) {
        const criteriaNum = (phase.interfaces?.length > 0 ? 3 : 2) + (phase.codeBlocks?.length > 0 ? 1 : 0);
        children.push(
            para(""),
            h2(`${secNum}.${criteriaNum} Phase ${phase.number} Acceptance Criteria`),
            ...phase.criteria.map(c => bullet(c)),
        );
    }

    children.push(divider());
}

// ── Section 8: Coding Conventions ─────────────────────────────
children.push(
    pageBreak(),
    h1("8. Coding Conventions & Rules"),
    para("All contributors (human or AI) must follow these rules. Violations must be fixed before merging."),
    para(""),

    h2("8.1 Naming"),
    ...codeBlock(CONVENTIONS.naming),

    para(""),
    h2("8.2 Include Rules"),
    ...CONVENTIONS.includeRules.map(r => bullet(r)),

    para(""),
    h2("8.3 Memory Rules"),
    ...CONVENTIONS.memoryRules.map(r => bullet(r)),

    para(""),
    h2("8.4 Interface Rules"),
    ...CONVENTIONS.interfaceRules.map(r => bullet(r)),

    para(""),
    h2("8.5 File Header Template"),
    para("Every .cpp and .h file must begin with this header:"),
    ...codeBlock(CONVENTIONS.fileHeaderTemplate),
    divider(),
);

// ── Section 9: Build System ────────────────────────────────────
children.push(
    pageBreak(),
    h1("9. Build System & Dependencies"),
    para(""),
    h2("9.1 CMake Structure"),
    ...codeBlock(BUILD.cmakeStructure),
    para(""),
    h2("9.2 External Dependencies"),
    systemTable(BUILD.dependencies),
    divider(),
);

// ── Section 10: Testing ────────────────────────────────────────
children.push(
    h1("10. Testing Strategy"),
    para("Every system must have at minimum one test file. Tests live in tests/ and mirror the engine directory structure. Run via CTest."),
    para(""),
    systemTable(TESTS),
    divider(),
);

// ── Section 11: Quick Reference ───────────────────────────────
children.push(
    pageBreak(),
    h1("11. Quick Reference — What To Do Next"),
    para("This section answers: 'I just opened this project — where do I start?' Follow this order strictly."),
    para(""),
    h2("If ALL systems are TODO (fresh start):"),
    ...QUICK_REFERENCE.freshStart.map(s => bullet(s)),

    para(""),
    h2("Phase 2 is IN PROGRESS — next steps in order:"),
    ...QUICK_REFERENCE.phase2InProgress.map(s => bullet(s)),

    para(""),
    h2("If Phase 2 is DONE, starting Phase 3:"),
    ...QUICK_REFERENCE.startingPhase3.map(s => bullet(s)),
    divider(),
);

// ── Section 12: Changelog ─────────────────────────────────────
children.push(
    h1("12. Revision History"),
    changelogTable(),
);

// ── Assemble & Write ──────────────────────────────────────────
const doc = new Document({
    numbering: {
        config: [
            {
                reference: "bullets",
                levels: [{ level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT,
                    style: { paragraph: { indent: { left: 720, hanging: 360 } } } }]
            },
            {
                reference: "subbullets",
                levels: [{ level: 0, format: LevelFormat.BULLET, text: "◦", alignment: AlignmentType.LEFT,
                    style: { paragraph: { indent: { left: 1080, hanging: 360 } } } }]
            },
        ]
    },
    styles: {
        default: { document: { run: { font: "Arial", size: 22, color: C.textMain } } },
        paragraphStyles: [
            { id: "Heading1", name: "Heading 1", basedOn: "Normal", next: "Normal", quickFormat: true,
              run: { size: 36, bold: true, font: "Arial", color: C.navy },
              paragraph: { spacing: { before: 360, after: 180 }, outlineLevel: 0 } },
            { id: "Heading2", name: "Heading 2", basedOn: "Normal", next: "Normal", quickFormat: true,
              run: { size: 28, bold: true, font: "Arial", color: C.accentSoft },
              paragraph: { spacing: { before: 280, after: 140 }, outlineLevel: 1 } },
            { id: "Heading3", name: "Heading 3", basedOn: "Normal", next: "Normal", quickFormat: true,
              run: { size: 24, bold: true, font: "Arial", color: C.darkNavy },
              paragraph: { spacing: { before: 200, after: 100 }, outlineLevel: 2 } },
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
            default: new Header({ children: [new Paragraph({
                border: { bottom: { style: BorderStyle.SINGLE, size: 4, color: C.lightBorder, space: 1 } },
                children: [
                    new TextRun({ text: `${META.title} — Development Master Plan`, font: "Arial", size: 18, color: C.textSub, bold: true }),
                    new TextRun({ text: `   |   Confidential Draft  |  Rev ${META.revision}`, font: "Arial", size: 18, color: C.textSub }),
                ]
            })] })
        },
        footers: {
            default: new Footer({ children: [new Paragraph({
                border: { top: { style: BorderStyle.SINGLE, size: 4, color: C.lightBorder, space: 1 } },
                tabStops: [{ type: TabStopType.RIGHT, position: TabStopPosition.MAX }],
                children: [
                    new TextRun({ text: `© ${META.title} Project`, font: "Arial", size: 18, color: C.textSub }),
                    new TextRun({ text: "\tPage ", font: "Arial", size: 18, color: C.textSub }),
                    new TextRun({ children: ["PAGE"], font: "Arial", size: 18, color: C.textSub }),
                ]
            })] })
        },
        children,
    }]
});

Packer.toBuffer(doc).then(buf => {
    fs.writeFileSync("./NOVA_ENGINE_PLAN.docx", buf);
    console.log(`✅  NOVA_ENGINE_PLAN.docx generated — Rev ${META.revision}`);
}).catch(err => {
    console.error("❌  Error generating document:", err);
    process.exit(1);
});