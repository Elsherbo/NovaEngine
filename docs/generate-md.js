"use strict";
// ============================================================
// FILE:    docs/generate-md.js
// PURPOSE: Reads ENGINE_PLAN_DATA.js and generates ENGINE_PLAN.md
//          Plain-text ASCII format — paste directly into AI chats.
// USAGE:   node generate-md.js
// ============================================================

const fs   = require("fs");
const DATA = require("./ENGINE_PLAN_DATA.js");
const { META, PHASE_SUMMARY, DIRECTORY_STRUCTURE, PHASES, CONVENTIONS, BUILD, TESTS, QUICK_REFERENCE, CHANGELOG } = DATA;

// ── ASCII Table Helpers ────────────────────────────────────────

/** Pad a string to exactly `len` chars (truncate with … if too long) */
function pad(str, len) {
    const s = String(str ?? "");
    if (s.length > len) return s.slice(0, len - 1) + "…";
    return s.padEnd(len);
}

/**
 * Render an ASCII table.
 * @param {string[]} headers
 * @param {number[]} widths   — column widths in chars (excl. padding)
 * @param {string[][]} rows
 */
function asciiTable(headers, widths, rows) {
    const sep = "+" + widths.map(w => "-".repeat(w + 2)).join("+") + "+";
    const hdr = "|" + headers.map((h, i) => " " + pad(h, widths[i]) + " ").join("|") + "|";
    const lines = [sep, hdr, sep.replace(/-/g, "=")];
    for (const row of rows) {
        lines.push("|" + row.map((c, i) => " " + pad(c, widths[i]) + " ").join("|") + "|");
    }
    lines.push(sep);
    return lines.join("\n");
}

/** Wrap long text into multiple lines of max `width` chars */
function wrap(text, width) {
    const words = String(text ?? "").split(" ");
    const lines = [];
    let current = "";
    for (const word of words) {
        if (current.length + word.length + 1 > width) {
            if (current) lines.push(current);
            current = word;
        } else {
            current = current ? current + " " + word : word;
        }
    }
    if (current) lines.push(current);
    return lines.length ? lines : [""];
}

/**
 * Render a table where the last column wraps.
 * Useful for "Notes" columns with long text.
 */
function wrappedTable(headers, widths, rows) {
    const sep = "+" + widths.map(w => "-".repeat(w + 2)).join("+") + "+";
    const hdr = "|" + headers.map((h, i) => " " + pad(h, widths[i]) + " ").join("|") + "|";
    const lines = [sep, hdr, sep.replace(/-/g, "=")];

    for (const row of rows) {
        // Wrap every cell independently, then zip rows together
        const wrapped = row.map((c, i) => wrap(c, widths[i]));
        const maxLines = Math.max(...wrapped.map(w => w.length));
        for (let li = 0; li < maxLines; li++) {
            const rowLine = "|" + wrapped.map((w, i) => " " + pad(w[li] ?? "", widths[i]) + " ").join("|") + "|";
            lines.push(rowLine);
        }
        lines.push(sep);
    }
    return lines.join("\n");
}

// ── Status emoji normaliser (strip emoji for pure ASCII if needed) ──
const STATUS_SHORT = {
    "✅ DONE":        "DONE       ",
    "🔄 IN PROGRESS": "IN PROGRESS",
    "⬜ TODO":        "TODO       ",
    "❌ MISSING":     "MISSING    ",
};
function shortStatus(s) { return STATUS_SHORT[s] ?? s; }

// ── Document builder ──────────────────────────────────────────
const lines = [];

function ln(s = "") { lines.push(s); }
function banner(text) {
    const bar = "=".repeat(72);
    ln(bar);
    ln(`  ${text}`);
    ln(bar);
}
function section(num, title) {
    ln("");
    ln("─".repeat(72));
    ln(`${num}. ${title.toUpperCase()}`);
    ln("─".repeat(72));
}
function sub(title) {
    ln("");
    ln(`  >> ${title}`);
    ln("  " + "-".repeat(title.length + 5));
}

// ── Cover ──────────────────────────────────────────────────────
ln("");
banner(`${META.title} — ${META.subtitle}`);
ln(`  ${META.tagline}`);
ln(`  Revision ${META.revision}  |  Start: ${META.startDate}  |  Codename: ${META.codename}`);
ln("=".repeat(72));
ln("");
ln("  HOW TO USE THIS DOCUMENT");
ln("  ─────────────────────────");
ln("  • Edit ENGINE_PLAN_DATA.js to change ANY content (statuses, notes, etc.)");
ln("  • Run: node generate-md.js   →  regenerates this file");
ln("  • Run: node generate.js      →  regenerates NOVA_ENGINE_PLAN.docx");
ln("  • Run: node changelog.js     →  adds a changelog entry interactively");
ln("  • Paste this file into an AI chat for full project context.");
ln("");

// ── Section 1: Status Legend ───────────────────────────────────
section(1, "Status Legend");
ln("");
ln("  DONE        = Fully implemented, tested, stable.");
ln("  IN PROGRESS = Work started, partially complete.");
ln("  TODO        = Planned, not yet started.");
ln("  MISSING     = Required but not yet designed/planned.");
ln("");

// ── Section 2: Phase Roadmap ───────────────────────────────────
section(2, "Phase Roadmap");
ln("");
ln(asciiTable(
    ["Phase", "Name", "Timeline", "Key Deliverables", "Status"],
    [7, 16, 14, 50, 13],
    PHASE_SUMMARY.map(p => [p.phase, p.name, p.timeline, p.deliverables, shortStatus(p.status)])
));

// ── Section 3: Directory Structure ────────────────────────────
section(3, "Directory Structure");
ln("");
ln(wrappedTable(
    ["Path", "Type", "Purpose"],
    [30, 8, 40],
    DIRECTORY_STRUCTURE.map(e => [e.path, e.type, e.purpose])
));

// ── Sections 4–7: Phases ──────────────────────────────────────
const secOffset = 4;

for (const phase of PHASES) {
    const secNum = secOffset + phase.number - 1;
    section(secNum, `Phase ${phase.number} — ${phase.name} (${phase.timeline}) [${shortStatus(phase.status).trim()}]`);
    ln("");
    ln("  GOAL:");
    // word-wrap the goal at 70 chars
    for (const wl of wrap(phase.goal, 68)) ln(`  ${wl}`);
    ln("");

    sub(`${secNum}.1 System Status`);
    ln("");
    ln(wrappedTable(
        ["System / Class", "Source File(s)", "Status", "Notes / Next Steps"],
        [24, 32, 13, 40],
        phase.systems.map(s => [s.name, s.file, shortStatus(s.status), s.notes])
    ));

    if (phase.interfaces && phase.interfaces.length > 0) {
        sub(`${secNum}.2 Key Interfaces`);
        ln("");
        ln(wrappedTable(
            ["Interface", "Implementations", "Description"],
            [20, 20, 50],
            phase.interfaces.map(i => [i.iface, i.impls, i.desc])
        ));
    }

    if (phase.codeBlocks && phase.codeBlocks.length > 0) {
        for (const cb of phase.codeBlocks) {
            sub(cb.title);
            ln("");
            for (const l of cb.lines) ln(`    ${l}`);
            ln("");
        }
    }

    if (phase.criteria && phase.criteria.length > 0) {
        sub(`Acceptance Criteria`);
        ln("");
        for (const c of phase.criteria) ln(`  [${c.startsWith("✅") ? "x" : " "}] ${c.replace(/^✅ /, "")}`);
        ln("");
    }
}

// ── Section 8: Coding Conventions ─────────────────────────────
section(8, "Coding Conventions & Rules");

sub("8.1 Naming Conventions");
ln("");
for (const r of CONVENTIONS.naming) ln(`  ${r}`);

sub("8.2 Include Rules");
ln("");
for (const r of CONVENTIONS.includeRules) ln(`  - ${r}`);

sub("8.3 Memory Rules");
ln("");
for (const r of CONVENTIONS.memoryRules) ln(`  - ${r}`);

sub("8.4 Interface Rules");
ln("");
for (const r of CONVENTIONS.interfaceRules) ln(`  - ${r}`);

sub("8.5 File Header Template");
ln("");
for (const l of CONVENTIONS.fileHeaderTemplate) ln(`  ${l}`);
ln("");

// ── Section 9: Build & Dependencies ───────────────────────────
section(9, "Build System & Dependencies");

sub("9.1 CMake Structure");
ln("");
for (const l of BUILD.cmakeStructure) ln(`  ${l}`);

sub("9.2 External Dependencies");
ln("");
ln(wrappedTable(
    ["Library", "Directory", "Status", "Notes"],
    [14, 20, 13, 50],
    BUILD.dependencies.map(d => [d.name, d.file, shortStatus(d.status), d.notes])
));

// ── Section 10: Tests ─────────────────────────────────────────
section(10, "Testing Strategy");
ln("");
ln(wrappedTable(
    ["Test File", "Covers", "Status", "Notes"],
    [34, 30, 13, 40],
    TESTS.map(t => [t.name, t.file, shortStatus(t.status), t.notes])
));

// ── Section 11: Quick Reference ───────────────────────────────
section(11, "Quick Reference — What To Do Next");

sub("Fresh Start (all TODO)");
ln("");
for (const s of QUICK_REFERENCE.freshStart) ln(`  ${s}`);

sub("Phase 2 In Progress");
ln("");
for (const s of QUICK_REFERENCE.phase2InProgress) ln(`  ${s}`);

sub("Starting Phase 3");
ln("");
for (const s of QUICK_REFERENCE.startingPhase3) ln(`  ${s}`);
ln("");

// ── Section 12: Changelog ─────────────────────────────────────
section(12, "Revision History");
ln("");
ln(wrappedTable(
    ["Rev", "Date", "Author", "Changes"],
    [5, 12, 18, 56],
    CHANGELOG.map(c => [c.rev, c.date, c.author, c.changes])
));

// ── Footer ─────────────────────────────────────────────────────
ln("");
ln("=".repeat(72));
ln(`  Generated by generate-md.js from ENGINE_PLAN_DATA.js`);
ln(`  To update: edit ENGINE_PLAN_DATA.js, then run: node generate-md.js`);
ln("=".repeat(72));
ln("");

// ── Write ──────────────────────────────────────────────────────
const output = lines.join("\n");
fs.writeFileSync("./ENGINE_PLAN.md", output, "utf8");
console.log(`✅  ENGINE_PLAN.md generated — Rev ${META.revision}  (${output.length.toLocaleString()} chars)`);