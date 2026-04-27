#!/usr/bin/env node
"use strict";
// ============================================================
// FILE:    docs/changelog.js
// PURPOSE: CLI helper to add a new changelog entry to
//          ENGINE_PLAN_DATA.js without editing it manually.
//
// USAGE (interactive):    node changelog.js
// USAGE (one-liner):      node changelog.js --rev 1.3 --author "Ahmed" --changes "Fixed physics sweep"
// ============================================================

const fs       = require("fs");
const path     = require("path");
const readline = require("readline");

const DATA_FILE = path.join(__dirname, "ENGINE_PLAN_DATA.js");

// ── Parse CLI args ────────────────────────────────────────────
function parseArgs() {
    const args = process.argv.slice(2);
    const out = {};
    for (let i = 0; i < args.length; i++) {
        if (args[i] === "--rev")     out.rev     = args[++i];
        if (args[i] === "--author")  out.author  = args[++i];
        if (args[i] === "--date")    out.date    = args[++i];
        if (args[i] === "--changes") out.changes = args[++i];
    }
    return out;
}

// ── Today's date YYYY-MM-DD ───────────────────────────────────
function today() {
    return new Date().toISOString().slice(0, 10);
}

// ── Detect next revision from current CHANGELOG ───────────────
function suggestNextRev(source) {
    const matches = [...source.matchAll(/rev:\s*"(\d+\.\d+)"/g)];
    if (!matches.length) return "1.0";
    const last = matches[matches.length - 1][1];
    const [maj, min] = last.split(".").map(Number);
    return `${maj}.${min + 1}`;
}

// ── Prompt helper ─────────────────────────────────────────────
function prompt(rl, question) {
    return new Promise(resolve => rl.question(question, resolve));
}

// ── Inject entry into CHANGELOG array in the data file ────────
function injectChangelog(source, entry) {
    // Find the closing of the CHANGELOG array: the last };
    // Strategy: find `const CHANGELOG = [` then find the matching `];`
    const marker = "const CHANGELOG = [";
    const start  = source.indexOf(marker);
    if (start === -1) throw new Error("Could not find CHANGELOG array in data file.");

    // Find the closing `];` after the marker
    let depth = 0;
    let i     = source.indexOf("[", start);
    let closeIdx = -1;
    while (i < source.length) {
        if (source[i] === "[") depth++;
        if (source[i] === "]") {
            depth--;
            if (depth === 0) { closeIdx = i; break; }
        }
        i++;
    }
    if (closeIdx === -1) throw new Error("Could not find end of CHANGELOG array.");

    const indent = "    ";
    const entryStr =
        `,\n${indent}{\n` +
        `${indent}    rev:    "${entry.rev}",\n` +
        `${indent}    date:   "${entry.date}",\n` +
        `${indent}    author: "${entry.author}",\n` +
        `${indent}    changes: "${entry.changes.replace(/"/g, '\\"')}",\n` +
        `${indent}}`;

    return source.slice(0, closeIdx) + entryStr + "\n" + source.slice(closeIdx);
}

// ── Also bump META.revision ────────────────────────────────────
function bumpRevision(source, newRev) {
    return source.replace(
        /revision:\s*"[^"]*"/,
        `revision:  "${newRev}"`
    );
}

// ── Main ──────────────────────────────────────────────────────
async function main() {
    const cliArgs  = parseArgs();
    const source   = fs.readFileSync(DATA_FILE, "utf8");
    const suggested = suggestNextRev(source);

    let entry = {
        rev:     cliArgs.rev     || "",
        date:    cliArgs.date    || today(),
        author:  cliArgs.author  || "",
        changes: cliArgs.changes || "",
    };

    // If all args supplied via CLI, skip interactive mode
    const isCLI = cliArgs.rev && cliArgs.author && cliArgs.changes;

    if (!isCLI) {
        const rl = readline.createInterface({ input: process.stdin, output: process.stdout });

        console.log("\n  ╔══════════════════════════════════════╗");
        console.log("  ║   NOVA ENGINE — Changelog Helper     ║");
        console.log("  ╚══════════════════════════════════════╝\n");

        entry.rev     = (await prompt(rl, `  Revision number [${suggested}]: `)).trim() || suggested;
        entry.date    = (await prompt(rl, `  Date [${today()}]: `)).trim() || today();
        entry.author  = (await prompt(rl, `  Author: `)).trim();
        console.log("  Changes (describe what you did — press Enter twice to finish):");
        let changeLines = [];
        let prev = "";
        while (true) {
            const l = await prompt(rl, "  > ");
            if (l === "" && prev === "") break;
            changeLines.push(l);
            prev = l;
        }
        entry.changes = changeLines.filter(Boolean).join(" ");
        rl.close();
    }

    // Validate
    if (!entry.rev || !entry.author || !entry.changes) {
        console.error("\n  ❌  Missing required fields (rev, author, changes). Aborting.\n");
        process.exit(1);
    }

    // Inject & write
    let updated = injectChangelog(source, entry);
    updated     = bumpRevision(updated, entry.rev);
    fs.writeFileSync(DATA_FILE, updated, "utf8");

    console.log("\n  ✅  Changelog entry added to ENGINE_PLAN_DATA.js");
    console.log(`      Rev: ${entry.rev}  |  Date: ${entry.date}  |  Author: ${entry.author}`);
    console.log("\n  Next steps:");
    console.log("    node generate-md.js   →  update ENGINE_PLAN.md");
    console.log("    node generate.js      →  update NOVA_ENGINE_PLAN.docx\n");
}

main().catch(err => {
    console.error("❌  Error:", err.message);
    process.exit(1);
});