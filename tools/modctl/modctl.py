#!/usr/bin/env python3
"""modctl: manage SimGolf mods from the command line (task-11, docs/modding/CORE.md).

Every decision (parsing mod.toml, the load order, conflicts, save checks) is
made by modctl_core.exe, built from the same C sources as the game's mod
platform, so this tool and the game always agree. This script is the front
end: arguments, editing profiles, scaffolding, readable output.

  modctl list                       installed mods, which ones load
  modctl enable ID / disable ID     in the active profile (or --profile P)
  modctl order                      the resolved load order
  modctl order ID --before X|--after X|--first|--last
                                    move ID in the profile (a preference: dependencies
                                    and load_after/load_before still apply)
  modctl check                      the same report the game logs at startup; exit 1 on errors
  modctl conflicts                  every key two or more mods touch, the winner, merges
  modctl new ID [--name NAME]       a new mod folder with a commented mod.toml
  modctl validate DIR               check one mod folder's mod.toml
  modctl profile [list|use P|create P [--from Q]|delete P]
  modctl save-info SAVE.sve         which mods a save was made with, against the loaded ones

Options for every command: --mods-dir DIR (default: the repo's mods/),
--core EXE (default: build/native/modctl_core.exe, or MODCTL_CORE), --json.
check/order/conflicts/list/save-info also take --profile P, --ini FILE, --no-mods.
"""

import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def die(msg, code=2):
    print(f"modctl: {msg}", file=sys.stderr)
    sys.exit(code)


class Core:
    def __init__(self, exe):
        self.exe = exe
        if not os.path.isfile(exe):
            die(f"{exe} not found: build it with `bash tools/build_native.sh`", 3)

    def run(self, *args, check=True):
        p = subprocess.run([self.exe, *args], capture_output=True, text=True, encoding="utf-8")
        if p.returncode == 2 or (check and p.returncode not in (0, 1)):
            die(p.stderr.strip() or f"modctl_core {args[0]} failed ({p.returncode})")
        return p

    def json(self, *args):
        p = self.run(*args)
        try:
            return json.loads(p.stdout), p.returncode
        except json.JSONDecodeError as e:
            die(f"modctl_core {args[0]}: bad output ({e})")


def session_args(a):
    out = ["--mods-dir", a.mods_dir]
    if getattr(a, "profile", None):
        out += ["--profile", a.profile]
    if getattr(a, "ini", None):
        out += ["--ini", a.ini]
    if getattr(a, "no_mods", False):
        out.append("--no-mods")
    return out


def load_order_path(a):
    return os.path.join(a.mods_dir, "load_order.toml")


def read_load_order(core, a):
    lo, _ = core.json("loadorder", load_order_path(a))
    if lo["error"]:
        die(f"{load_order_path(a)}: {lo['error']} (fix it by hand first)")
    return lo


def target_profile(core, a, lo):
    """The profile an edit applies to: --profile, else the active one."""
    name = a.profile or lo["active"]
    if not lo["exists"] and not a.profile:
        name = "default"   # first edit: start the default profile and make it active
    if name == "vanilla":
        die('the active profile is "vanilla" (the original game, always empty); '
            'pick another with `modctl profile use NAME` or pass --profile NAME')
    return name


def write_profile(core, a, name, ids, activate=False):
    core.run("set-profile", load_order_path(a), name, *ids)
    if activate:
        core.run("set-active", load_order_path(a), name)


def resolve(core, a):
    return core.json("resolve", *session_args(a))


# ------------------------------------------------------------------ commands

def cmd_list(core, a):
    rep, _ = resolve(core, a)
    if a.json:
        print(json.dumps(rep["installed"], indent=2))
        return 0
    order = {m: i + 1 for i, m in enumerate(rep["order"])}
    print(f'profile "{rep["profile"]}", {len(rep["order"])} of {len(rep["installed"])} mods load')
    for m in rep["installed"]:
        state = f"#{order[m['id']]}" if m["id"] in order else ("broken" if m["error"] else "off")
        tag = " (built-in)" if m["builtin"] else ""
        print(f"  {state:>6}  {m['id']} {m['version'] or '?'}{tag}: {m['name']}")
        if m["error"]:
            print(f"          {m['error']}")
    return 0


def cmd_enable(core, a, on):
    lo = read_load_order(core, a)
    name = target_profile(core, a, lo)
    ids = list(lo["profiles"].get(name, []))
    if on:
        rep, _ = resolve(core, a)
        if a.id not in [m["id"] for m in rep["installed"]]:
            print(f"modctl: note: {a.id} is not installed in {a.mods_dir} (enabled anyway)", file=sys.stderr)
        if a.id not in ids:
            ids.append(a.id)
    else:
        if a.id not in ids:
            print(f'{a.id} is not in profile "{name}"')
            return 0
        ids.remove(a.id)
    write_profile(core, a, name, ids, activate=not lo["exists"])
    print(f'{"enabled" if on else "disabled"} {a.id} in profile "{name}"')
    a.profile = name
    return cmd_check(core, a, quiet_ok=True)


def cmd_order(core, a):
    if a.id:
        lo = read_load_order(core, a)
        name = target_profile(core, a, lo)
        ids = list(lo["profiles"].get(name, []))
        if a.id not in ids:
            die(f'{a.id} is not in profile "{name}" (enable it first)')
        ids.remove(a.id)
        if a.first:
            ids.insert(0, a.id)
        elif a.last:
            ids.append(a.id)
        else:
            other = a.before or a.after
            if other not in ids:
                die(f'{other} is not in profile "{name}"')
            k = ids.index(other)
            ids.insert(k if a.before else k + 1, a.id)
        write_profile(core, a, name, ids)
        a.profile = name
    rep, _ = resolve(core, a)
    if a.json:
        print(json.dumps(rep["order"]))
        return 0
    versions = {m["id"]: m["version"] for m in rep["installed"]}
    print(f'load order, profile "{rep["profile"]}":')
    for i, m in enumerate(rep["order"]):
        print(f"  {i + 1}. {m} {versions.get(m, '?')}")
    if not rep["order"]:
        print("  (no mods: the original game)")
    return 0


def cmd_check(core, a, quiet_ok=False):
    rep, rc = resolve(core, a)
    if a.json:
        print(json.dumps(rep, indent=2))
    elif not (quiet_ok and rep["total_errors"] == 0):
        for line in rep["lines"]:
            print(line)
    return 1 if rep["total_errors"] else 0


def cmd_conflicts(core, a):
    rep, _ = resolve(core, a)
    keys = [k for k in rep["keys"] if k["mods"] >= 2]
    if a.json:
        print(json.dumps(keys, indent=2))
        return 0
    if not keys:
        print("no key is touched by two mods")
        return 0
    for k in keys:
        head = "CONFLICT" if k["conflict"] else "merged"
        print(f"{head}: {k['kind']} {k['key']}")
        for t in k["touches"]:
            val = f" {t['value']}" if t["value"] is not None else ""
            print(f"    {t['mod']}: {t['op']}{val}")
        if k["conflict"]:
            print(f"    -> {k['winner']} wins (later in load order)" + (f", value {k['result']}" if k["result"] else ""))
        else:
            print("    -> all apply" + (f", value {k['result']}" if k["result"] else ""))
    return 0


TEMPLATE = '''# {name}: a SimGolf mod (docs/modding/CORE.md).
id = "{id}"
name = "{name}"
version = "0.1.0"
authors = ["{author}"]
description = "What this mod changes, in one sentence."
platform = "^1.0"          # mod platform versions this works with
# game = ">=1.2.0"         # game versions (golf.exe v1.02 is 1.2.0)
# load_after = ["other_mod"]
# load_before = ["other_mod"]
# provides = ["some.feature"]

# [dependencies]            other mods this one needs, with version ranges
# other_mod = "^1.0"
# [optional_dependencies]   used when present, loaded first
# [incompatibilities]       never together with these

# [settings]                values this mod sets (a conflict if another mod sets them too)
# "economy.green_fee_scale" = 1.2

# [patch]                   values this mod combines with other mods' (no conflict)
# "economy.income_scale" = {{ op = "mul", value = 1.1 }}

# [touches.assets]          files it replaces (declared so conflicts show up front)
# "Interface/TitleBASE.pcx" = "replace"
'''


def cmd_new(core, a):
    if not re.fullmatch(r"[a-z0-9][a-z0-9_.\-]{0,62}", a.id):
        die("ids are lowercase letters, digits, '_', '-' or '.' (starting with a letter or digit)")
    d = os.path.join(a.mods_dir, a.id)
    if os.path.exists(d):
        die(f"{d} already exists")
    os.makedirs(d)
    name = a.name or a.id.replace("_", " ").capitalize()
    with open(os.path.join(d, "mod.toml"), "w", encoding="utf-8", newline="\n") as f:
        f.write(TEMPLATE.format(id=a.id, name=name, author=a.author))
    rep, rc = core.json("manifest", d)
    print(f"created {os.path.join(d, 'mod.toml')}; enable it with `modctl enable {a.id}`")
    return 0 if rep["ok"] else 1


def cmd_validate(core, a):
    rep, rc = core.json("manifest", a.dir)
    if a.json:
        print(json.dumps(rep, indent=2))
        return 0 if rep["ok"] else 1
    m = rep["installed"][0]
    if rep["ok"]:
        print(f"ok: {m['id']} {m['version']} ({len(m['touches'])} touches)")
    else:
        print(f"error: {m['error']}")
    for d in rep["diagnostics"]:
        print(f"{d['level']} {d['code']}: {d['message']}")
    return 0 if rep["ok"] else 1


def cmd_profile(core, a):
    lo = read_load_order(core, a)
    path = load_order_path(a)
    if a.action in (None, "list"):
        if a.json:
            print(json.dumps(lo, indent=2))
            return 0
        for name, ids in lo["profiles"].items():
            mark = "*" if name == lo["active"] else " "
            print(f" {mark} {name}: {', '.join(ids) if ids else '(no mods: the original game)'}")
        return 0
    if not a.name:
        die(f"profile {a.action} needs a name")
    if a.action == "use":
        if a.name not in lo["profiles"]:
            die(f'no profile "{a.name}" (create it with `modctl profile create {a.name}`)')
        core.run("set-active", path, a.name)
        print(f'active profile: "{a.name}"')
        return 0
    if a.action == "create":
        if a.name in lo["profiles"]:
            die(f'profile "{a.name}" already exists')
        src = lo["profiles"].get(a.from_profile, []) if a.from_profile else []
        if a.from_profile and a.from_profile not in lo["profiles"]:
            die(f'no profile "{a.from_profile}"')
        core.run("set-profile", path, a.name, *src)
        print(f'created profile "{a.name}"' + (f' from "{a.from_profile}"' if a.from_profile else ""))
        return 0
    if a.action == "delete":
        core.run("delete-profile", path, a.name)
        print(f'deleted profile "{a.name}"')
        return 0
    die(f"unknown profile action {a.action}")


def cmd_save_info(core, a):
    rep, _ = core.json("check-save", a.save, *session_args(a))
    if a.json:
        print(json.dumps(rep, indent=2))
        return 0
    name = os.path.basename(a.save)
    if not rep["record"]:
        print(f"{name}: no mod record (made by the original game or before mods existed)")
        return 0
    if rep["stale"]:
        print(f"{name}: the mod record belongs to an earlier version of the file (ignored)")
        return 0
    mods = ", ".join(f"{m['id']} {m['version']}" for m in rep["mods"]) or "no mods"
    print(f'{name}: made with {mods} (profile "{rep["profile"]}", platform {rep["platform"]})')
    for d in rep["diagnostics"]:
        print(f"  {d['level']}: {d['message']}")
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(prog="modctl", description="Manage SimGolf mods (docs/modding/CORE.md).")
    p.add_argument("--mods-dir", default=os.environ.get("SIMGOLF_MODS_DIR", os.path.join(ROOT, "mods")))
    p.add_argument("--core", default=os.environ.get("MODCTL_CORE", os.path.join(ROOT, "build", "native", "modctl_core.exe")))
    p.add_argument("--json", action="store_true", help="machine-readable output")
    sub = p.add_subparsers(dest="cmd", required=True)

    def session(sp):
        sp.add_argument("--profile")
        sp.add_argument("--ini", help="a mods.ini whose [mods] switches apply, as in the game")
        sp.add_argument("--no-mods", action="store_true", help="the original game: profile vanilla")

    for name in ("list", "check", "conflicts"):
        session(sub.add_parser(name))
    for name in ("enable", "disable"):
        sp = sub.add_parser(name)
        sp.add_argument("id")
        sp.add_argument("--profile")
    sp = sub.add_parser("order")
    sp.add_argument("id", nargs="?")
    g = sp.add_mutually_exclusive_group()
    g.add_argument("--before")
    g.add_argument("--after")
    g.add_argument("--first", action="store_true")
    g.add_argument("--last", action="store_true")
    session(sp)
    sp = sub.add_parser("new")
    sp.add_argument("id")
    sp.add_argument("--name")
    sp.add_argument("--author", default="you")
    sp = sub.add_parser("validate")
    sp.add_argument("dir")
    sp = sub.add_parser("profile")
    sp.add_argument("action", nargs="?", choices=["list", "use", "create", "delete"])
    sp.add_argument("name", nargs="?")
    sp.add_argument("--from", dest="from_profile")
    sp = sub.add_parser("save-info")
    sp.add_argument("save")
    session(sp)

    argv = list(sys.argv[1:] if argv is None else argv)
    as_json = "--json" in argv   # accepted anywhere on the line
    argv = [x for x in argv if x != "--json"]
    a = p.parse_args(argv)
    a.json = as_json
    a.mods_dir = os.path.abspath(a.mods_dir).replace("\\", "/")   # as the game writes paths
    if a.cmd == "order" and a.id and not (a.before or a.after or a.first or a.last):
        die("order ID needs --before X, --after X, --first or --last")
    core = Core(a.core)
    if a.cmd == "list":
        return cmd_list(core, a)
    if a.cmd in ("enable", "disable"):
        a.ini, a.no_mods = None, False
        return cmd_enable(core, a, a.cmd == "enable")
    if a.cmd == "order":
        return cmd_order(core, a)
    if a.cmd == "check":
        return cmd_check(core, a)
    if a.cmd == "conflicts":
        return cmd_conflicts(core, a)
    if a.cmd == "new":
        return cmd_new(core, a)
    if a.cmd == "validate":
        return cmd_validate(core, a)
    if a.cmd == "profile":
        return cmd_profile(core, a)
    if a.cmd == "save-info":
        return cmd_save_info(core, a)
    return 2


if __name__ == "__main__":
    sys.exit(main())
