// tools/release/changelog_to_json.mjs: CHANGELOG.md -> data/updates.json (task-80).
//   node tools/release/changelog_to_json.mjs [changelog.md] [out.json]
// Heading:  "## 0.1.3 - 2026-10-13"  (a date is optional)
// Then one summary paragraph (its first sentence is the release name), then "- " bullets.
// The game's Updates panel reads the result; release.sh regenerates it so it matches the release notes.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

export function parseChangelog(text) {
  const releases = [];
  let cur = null;
  let inSummary = false;
  for (const raw of text.replace(/\r/g, "").split("\n")) {
    const line = raw.trimEnd();
    const h = /^##\s+v?(\d+\.\d+\.\d+)\s*(?:[-(]\s*(\d{4}-\d{2}-\d{2})\)?)?.*$/.exec(line);
    if (h) {
      cur = { version: h[1], date: h[2] || (/unreleased/i.test(line) ? "Not out yet" : ""), name: "", summary: "", notes: [] };
      releases.push(cur);
      inSummary = true;
      continue;
    }
    if (!cur) continue;
    if (/^##\s/.test(line)) { cur = null; continue; }
    const b = /^[-*]\s+(.*)$/.exec(line);
    if (b) { cur.notes.push(b[1].trim()); inSummary = false; continue; }
    if (line.trim() === "") continue;
    if (inSummary && !cur.notes.length) cur.summary = (cur.summary ? cur.summary + " " : "") + line.trim();
    else if (cur.notes.length) cur.notes[cur.notes.length - 1] += " " + line.trim();   // a wrapped bullet
  }
  for (const r of releases) {
    const m = /^([^:.]+)/.exec(r.summary);
    r.name = m ? m[1].trim() : r.date === "Not out yet" ? "Coming in the next release" : "Release " + r.version;
  }
  return releases;
}

export function toUpdatesJson(releases) {
  return JSON.stringify({ format: 1, releases }, null, 2) + "\n";
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
  const src = process.argv[2] || path.join(root, "tools/release/public/CHANGELOG.md");
  const out = process.argv[3] || path.join(root, "data/updates.json");
  const rel = parseChangelog(fs.readFileSync(src, "utf8"));
  if (!rel.length) { console.error("no releases found in " + src); process.exit(1); }
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, toUpdatesJson(rel));
  console.log(`updates.json: ${rel.length} releases, newest ${rel[0].version}`);
}
