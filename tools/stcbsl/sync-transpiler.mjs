// Re-vendor the embedded transpiler bundle from sb3-creator, or (--check)
// fail if the committed copy has drifted from what sb3-creator would produce.
//
//   node tools/stcbsl/sync-transpiler.mjs [--dir ../../sb3-creator]
//   node tools/stcbsl/sync-transpiler.mjs --check [--dir ../../sb3-creator]
//
// The bundle (SB3Creator collapsed to one bare-engine script) is include_str!'d
// into the `bwc` binary, so it is a BUILD INPUT and lives committed under
// vendor/ — not gitignored. This script keeps it honest: it runs sb3-creator's
// own scripts/bundle-embed.mjs (the single source of truth) and copies the
// result, so the Rust CLI's transpiler can never silently lag the app's.
//
// --check is for CI (a sibling repo must be present): it regenerates in memory
// and diffs, exiting non-zero on any difference except the run-random @bw yield
// block-ids, which differ every run and are not drift.

import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const check = argv.includes('--check');
const dirIdx = argv.indexOf('--dir');
const sb3 = resolve(here, dirIdx >= 0 ? argv[dirIdx + 1] : '../../../sb3-creator');
const vendored = resolve(here, 'vendor/bw-transpiler.embed.js');

const HEADER = [
    '// VENDORED — DO NOT EDIT. The Brickwright transpiler (SB3Creator) collapsed',
    '// to one bare-engine script, embedded in the `bwc` binary via include_str!.',
    '// Source of truth: CrispStrobe/sb3-creator scripts/bundle-embed.mjs.',
    '// Re-vendor:  node tools/stcbsl/sync-transpiler.mjs --dir <sb3-creator>',
    '// Drift check: node tools/stcbsl/sync-transpiler.mjs --check --dir <sb3-creator>',
    '',
].join('\n');

if (!existsSync(resolve(sb3, 'scripts/bundle-embed.mjs'))) {
    console.error(`sync-transpiler: sb3-creator not found at ${sb3} (pass --dir <path>)`);
    process.exit(2);
}

// Build the bundle in the sb3-creator checkout (writes its build/ artifact).
execFileSync('node', [resolve(sb3, 'scripts/bundle-embed.mjs')], { cwd: sb3, stdio: 'pipe' });
const fresh = HEADER + readFileSync(resolve(sb3, 'build/bw-transpiler.embed.js'), 'utf8');

// The @bw yield markers carry run-random block-ids; strip them for comparison.
const stable = (s) => s.split('\n').filter((l) => !l.includes('@bw yield')).join('\n');

if (check) {
    if (!existsSync(vendored)) {
        console.error('sync-transpiler --check: vendor/bw-transpiler.embed.js is missing');
        process.exit(1);
    }
    const have = readFileSync(vendored, 'utf8');
    if (stable(have) !== stable(fresh)) {
        console.error('sync-transpiler --check: vendored bundle has DRIFTED from sb3-creator.');
        console.error('  re-vendor: node tools/stcbsl/sync-transpiler.mjs --dir ' + sb3);
        process.exit(1);
    }
    console.log(`sync-transpiler --check: vendored bundle is current (${(have.length / 1024).toFixed(0)} KiB)`);
} else {
    writeFileSync(vendored, fresh);
    console.log(`sync-transpiler: wrote vendor/bw-transpiler.embed.js (${(fresh.length / 1024).toFixed(0)} KiB)`);
}
