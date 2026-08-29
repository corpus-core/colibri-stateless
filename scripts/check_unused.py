#!/usr/bin/env python3
"""
Dead-code and consistency checker for Colibri C sources.

Scans `src/` (and usage in test/, bindings/, scripts/, CMake) for:

  * functions declared in headers but never defined
  * functions defined but never referenced
  * unused file-scope static functions / variables
  * headers that nothing includes
  * duplicate file-scope function definitions
  * duplicate `#define` names with conflicting values (src/ only)

Usage:
  python3 scripts/check_unused.py
  python3 scripts/check_unused.py --json
  python3 scripts/check_unused.py --fail-on unused-static,decl-without-def

Exit code is 1 when any selected `--fail-on` category has findings.
Default `--fail-on` is the high-confidence set used in CI.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
from dataclasses import asdict, dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

# Identifier usage is searched here so bindings / tests / CMake keep a symbol alive.
USAGE_ROOTS = [
    ROOT / "src",
    ROOT / "test",
    ROOT / "bindings",
    ROOT / "scripts",
    ROOT / "libs",
    ROOT / "CMakeLists.txt",
]

SKIP_DIR_NAMES = {
    "build",
    "build_static_analysis",
    "_deps",
    "node_modules",
    "target",
    ".git",
    "generated",
    "zk_proof",  # Rust, not C
    "kona_bridge",  # Rust
}

C_EXTS = {".c", ".h"}

# Types / keywords that are never function names.
TYPE_WORDS = {
    "auto",
    "bool",
    "break",
    "case",
    "char",
    "const",
    "continue",
    "default",
    "do",
    "double",
    "else",
    "enum",
    "extern",
    "float",
    "for",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "register",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "struct",
    "switch",
    "typedef",
    "union",
    "unsigned",
    "void",
    "volatile",
    "while",
    "restrict",
    "_Bool",
    "size_t",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "uint64_t",
    "int8_t",
    "int16_t",
    "int32_t",
    "int64_t",
    "true",
    "false",
    "NULL",
}

# Function-like macros / keywords that look like calls.
NOT_FUNCTIONS = TYPE_WORDS | {
    "if",
    "for",
    "while",
    "switch",
    "sizeof",
    "return",
    "defined",
}

IDENT_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\b")

# File-scope function: optional storage / qualifiers, return type, name, `(...)`.
# Intentionally rejects function-pointer decls (`(*name)`).
FUNC_HEAD_RE = re.compile(
    r"""^
    (?P<prefix>(?:(?:static|inline|extern|INTERNAL|API_PUBLIC|NONNULL|NONNULL_FOR|RETURNS_NONNULL|C4_UNUSED)\s+)*)
    (?P<ret>(?:const\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*?)
    \s+
    (?P<name>[A-Za-z_][A-Za-z0-9_]*)
    \s*
    \(
    (?P<args>[^;{}]*?)
    \)
    \s*
    (?P<attrs>(?:[A-Z_][A-Z0-9_]*\s*(?:\([^;{}]*\))?\s*)*)
    (?P<tail>[{;]|$)
    """,
    re.VERBOSE | re.MULTILINE,
)

# File-scope static / global variable (no `(` after the name).
VAR_RE = re.compile(
    r"""^
    (?P<prefix>(?:(?:static|const|extern|INTERNAL|API_PUBLIC|C4_UNUSED|volatile)\s+)*)
    (?P<type>[A-Za-z_][A-Za-z0-9_\s\*]*?)
    \s+
    (?P<name>[A-Za-z_][A-Za-z0-9_]*)
    \s*
    (?:[=\[;])
    """,
    re.VERBOSE | re.MULTILINE,
)

DEFINE_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)(?:\(([^)]*)\))?[ \t]*(.*)$", re.MULTILINE)
INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+[<"]([^>"]+)[>"]', re.MULTILINE)

# Dispatchers emitted into the CMake build dir (`verifiers.h`, `provers.h`,
# `chain_props.h`). They are declared under `src/` but defined only after configure.
GENERATED_FUNCS = {
    "c4_get_method_type",
    "c4_get_prover_payload",
    "c4_get_prover_cache_request",
    "c4_init_rpc_ctx",
    "c4_reset_verifier_caches",
    "c4_reset_prover_caches",
    "c4_chains_get_props",
}

# Hook argument names in add_verifier() / add_prover() / add_chain_props().
CMAKE_HOOK_RE = re.compile(
    r"\b(?:GET_REQ_TYPE|VERIFY|METHOD_TYPE|PROVER_PAYLOAD|PROVER_CACHE_URL|"
    r"RESET_CACHES|INIT_RPC_CTX|PROOF|FUNC)\s+([A-Za-z_][A-Za-z0-9_]*)"
)

# High-confidence categories that CI fails on by default.
# `conflicting-define` stays informational: local macros and fork-specific
# numeric constants collide by name on purpose.
DEFAULT_FAIL_ON = (
    "decl-without-def",
    "duplicate-def",
    "unused-static",
    "unused-static-var",
    "unused-header",
    "unused-func",
)


@dataclass
class Symbol:
    name: str
    kind: str  # func | var
    path: str
    line: int
    static: bool
    prototype: bool  # declaration only (ends with ;)
    defined: bool
    unused_ok: bool = False  # marked C4_UNUSED on purpose


@dataclass
class Finding:
    category: str
    name: str
    path: str
    line: int
    detail: str = ""


@dataclass
class Report:
    findings: list[Finding] = field(default_factory=list)

    def add(self, category: str, name: str, path: str, line: int, detail: str = "") -> None:
        self.findings.append(Finding(category, name, path, line, detail))

    def by_category(self) -> dict[str, list[Finding]]:
        out: dict[str, list[Finding]] = defaultdict(list)
        for f in self.findings:
            out[f.category].append(f)
        return dict(out)


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def should_skip(path: Path) -> bool:
    parts = set(path.parts)
    return bool(parts & SKIP_DIR_NAMES)


def exclusive_backend_defs(defs: list[Symbol]) -> bool:
    """True when every extra definition is an `#ifdef` twin, not a leftover copy.

    Same source file: PROVER_CACHE / USE_CHECKPOINTZ stubs.
    Two files whose stems are `foo` and `foo_<backend>`: bn254 vs bn254_mcl.
    """
    paths = {d.path for d in defs}
    if len(paths) == 1:
        return True
    if len(paths) != 2:
        return False
    stems = sorted(Path(p).stem for p in paths)
    return stems[1].startswith(stems[0] + "_")


def iter_c_files(root: Path) -> list[Path]:
    files: list[Path] = []
    if root.is_file():
        return [root]
    for p in root.rglob("*"):
        if not p.is_file() or p.suffix not in C_EXTS:
            continue
        if should_skip(p):
            continue
        files.append(p)
    return sorted(files)


def strip_noise(text: str) -> str:
    """Remove comments and string/char literals, keeping newlines so line numbers stay valid.

    Char literals are handled before strings so `'"'` cannot swallow the rest of a line
    (that used to hide real identifier uses and produce unused-static false positives).
    """
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        two = text[i : i + 2]
        if two == "//":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if two == "/*":
            out.extend("  ")
            i += 2
            while i < n:
                if text[i : i + 2] == "*/":
                    out.extend("  ")
                    i += 2
                    break
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            continue
        if text[i] == "'":
            out.append("'")
            i += 1
            if i < n and text[i] == "\\":
                out.append("x")
                i += 2
            elif i < n:
                out.append("x")
                i += 1
            if i < n and text[i] == "'":
                out.append("'")
                i += 1
            continue
        if text[i] == '"':
            out.append('"')
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\" and i + 1 < n:
                    out.append(" ")
                    i += 2
                    continue
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append('"')
                i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


def is_func_like(ret: str, name: str) -> bool:
    if name in TYPE_WORDS or name in NOT_FUNCTIONS:
        return False
    if name.startswith("SSZ_") or name.startswith("TRY_") or name.startswith("CHECK_"):
        return False
    # Reject leftover preprocessor / typedef noise.
    tokens = [t for t in ret.replace("*", " ").split() if t]
    if not tokens:
        return False
    if tokens[0] in {"typedef", "struct", "enum", "union", "case", "return", "else"}:
        return False
    return True


def looks_like_var(prefix: str, typ: str, name: str) -> bool:
    if name in TYPE_WORDS:
        return False
    tokens = [t for t in typ.replace("*", " ").split() if t]
    if not tokens:
        return False
    if tokens[0] in {"typedef", "struct", "enum", "union", "return", "else", "case", "goto"}:
        return False
    # Require a storage class or a known C type / `_t` suffix so we skip statements.
    if "static" not in prefix and "extern" not in prefix:
        last = tokens[-1]
        if last not in TYPE_WORDS and not last.endswith("_t") and last not in {"int", "char", "bool"}:
            return False
    return True


def extract_symbols(path: Path) -> list[Symbol]:
    raw = path.read_text(encoding="utf-8", errors="replace")
    text = strip_noise(raw)
    symbols: list[Symbol] = []

    for m in FUNC_HEAD_RE.finditer(text):
        name = m.group("name")
        ret = m.group("ret")
        if not is_func_like(ret, name):
            continue
        prefix = m.group("prefix") or ""
        attrs = m.group("attrs") or ""
        tail = m.group("tail")
        # A header prototype ends with `;`. A definition ends with `{` or a
        # following `{` on the next non-empty line (our formatter usually
        # keeps `{` on the same line, but tolerate both).
        after = text[m.end() : m.end() + 80].lstrip()
        is_proto = tail == ";" or (tail == "" and after.startswith(";"))
        is_def = tail == "{" or (tail == "" and after.startswith("{"))
        if not is_proto and not is_def:
            continue
        window = text[max(0, m.start() - 80) : m.end() + 80]
        symbols.append(
            Symbol(
                name=name,
                kind="func",
                path=rel(path),
                line=line_of(text, m.start()),
                static="static" in prefix.split(),
                prototype=is_proto,
                defined=is_def,
                unused_ok="C4_UNUSED" in prefix or "C4_UNUSED" in attrs or "C4_UNUSED" in window,
            )
        )

    if path.suffix == ".c":
        for m in VAR_RE.finditer(text):
            name = m.group("name")
            prefix = m.group("prefix") or ""
            typ = m.group("type")
            if not looks_like_var(prefix, typ, name):
                continue
            # Skip if this match is actually a function we already captured.
            if any(s.name == name and s.kind == "func" and s.line == line_of(text, m.start()) for s in symbols):
                continue
            window = text[max(0, m.start() - 80) : m.end() + 80]
            symbols.append(
                Symbol(
                    name=name,
                    kind="var",
                    path=rel(path),
                    line=line_of(text, m.start()),
                    static="static" in prefix.split(),
                    prototype="extern" in prefix.split(),
                    defined="extern" not in prefix.split(),
                    unused_ok="C4_UNUSED" in prefix or "C4_UNUSED" in window,
                )
            )

    return symbols


def extract_defines(path: Path) -> list[tuple[str, str, int, str]]:
    raw = path.read_text(encoding="utf-8", errors="replace")
    out: list[tuple[str, str, int, str]] = []
    for i, line in enumerate(raw.splitlines(), 1):
        m = DEFINE_RE.match(line)
        if not m:
            continue
        name, _args, value = m.group(1), m.group(2), m.group(3).strip()
        # Ignore include guards and empty/config toggles that are meant to be redefined.
        if name.endswith("_H") or name.endswith("_h__") or name.endswith("_H__"):
            continue
        out.append((name, value, i, rel(path)))
    return out


def extract_includes(path: Path) -> list[str]:
    raw = path.read_text(encoding="utf-8", errors="replace")
    return INCLUDE_RE.findall(raw)


USAGE_EXTS = C_EXTS | {".txt", ".cmake", ".md", ".py", ".js", ".mjs", ".ts", ".rs", ".swift", ".kt"}


def collect_usage_index() -> tuple[dict[str, str], dict[str, dict[str, int]]]:
    """Return (path -> stripped text, identifier -> {path -> count})."""
    blobs: dict[str, str] = {}
    index: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    for root in USAGE_ROOTS:
        paths: list[Path]
        if root.is_file():
            paths = [root]
        elif root.exists():
            paths = [p for p in root.rglob("*") if p.is_file()]
        else:
            continue
        for p in paths:
            if should_skip(p):
                continue
            if p.suffix not in USAGE_EXTS and p.name != "CMakeLists.txt":
                continue
            try:
                text = strip_noise(p.read_text(encoding="utf-8", errors="replace"))
            except (OSError, UnicodeError):
                continue
            key = rel(p)
            blobs[key] = text
            for ident in IDENT_RE.findall(text):
                index[ident][key] += 1
    return blobs, {k: dict(v) for k, v in index.items()}


def count_ident(index: dict[str, dict[str, int]], name: str) -> dict[str, int]:
    return index.get(name, {})


def cmake_hook_names() -> set[str]:
    names: set[str] = set()
    for p in ROOT.rglob("CMakeLists.txt"):
        if should_skip(p):
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        names.update(CMAKE_HOOK_RE.findall(text))
    return names


def analyze() -> Report:
    report = Report()
    src_files = iter_c_files(SRC)
    symbols: list[Symbol] = []
    for p in src_files:
        symbols.extend(extract_symbols(p))

    blobs, usage = collect_usage_index()
    hooks = cmake_hook_names() | GENERATED_FUNCS

    # --- functions ---
    funcs = [s for s in symbols if s.kind == "func"]
    defs_by_name: dict[str, list[Symbol]] = defaultdict(list)
    decls_by_name: dict[str, list[Symbol]] = defaultdict(list)
    for s in funcs:
        if s.defined:
            defs_by_name[s.name].append(s)
        if s.prototype:
            decls_by_name[s.name].append(s)

    # Duplicate non-static definitions (ODR / leftover copies).
    # `main` is expected in every CLI/tool. Same-file copies and `foo.c` vs
    # `foo_<backend>.c` pairs are `#ifdef` backends (bn254 vs MCL, PROVER_CACHE)
    # that compile only one copy — skip those so CI can fail on real leftovers.
    for name, defs in sorted(defs_by_name.items()):
        if name == "main":
            continue
        non_static = [d for d in defs if not d.static]
        if len(non_static) <= 1:
            continue
        if exclusive_backend_defs(non_static):
            continue
        locs = ", ".join(f"{d.path}:{d.line}" for d in non_static)
        report.add("duplicate-def", name, non_static[0].path, non_static[0].line, locs)

    # Header prototype without any definition in src/.
    for name, decls in sorted(decls_by_name.items()):
        if name in defs_by_name or name in GENERATED_FUNCS:
            continue
        # static inline helpers live entirely in the header
        if all(d.static for d in decls):
            continue
        for d in decls:
            if d.path.endswith(".h"):
                report.add(
                    "decl-without-def",
                    name,
                    d.path,
                    d.line,
                    "header declaration has no matching definition under src/",
                )

    # Unused: definition (or public prototype) never referenced outside its own site.
    seen_unused: set[tuple[str, str]] = set()
    for s in funcs:
        if s.unused_ok or s.name in hooks or s.name == "main":
            continue
        if s.prototype and s.name in defs_by_name:
            continue  # usage is judged on the definition
        hits = count_ident(usage, s.name)
        own = hits.get(s.path, 0)
        others = sum(n for p, n in hits.items() if p != s.path)
        # One hit in the defining file is the definition itself.
        # static inline in a header is "used" if other files mention it.
        if s.static:
            extra_in_file = own - 1
            if extra_in_file <= 0 and others == 0:
                key = (s.name, s.path)
                if key in seen_unused:
                    continue
                seen_unused.add(key)
                report.add(
                    "unused-static",
                    s.name,
                    s.path,
                    s.line,
                    "static function is never referenced",
                )
            continue

        if others == 0 and own <= 1:
            # Public function: allow a matching header prototype + definition
            # (two files) if nothing else uses it.
            proto_files = {d.path for d in decls_by_name.get(s.name, [])}
            def_files = {d.path for d in defs_by_name.get(s.name, [])}
            allowed = proto_files | def_files
            external = {p: n for p, n in hits.items() if p not in allowed}
            if not external:
                key = (s.name, "public")
                if key in seen_unused:
                    continue
                seen_unused.add(key)
                loc = defs_by_name[s.name][0] if s.name in defs_by_name else s
                report.add(
                    "unused-func",
                    s.name,
                    loc.path,
                    loc.line,
                    "public function is never referenced outside its declaration/definition",
                )

    # Unused file-scope static variables.
    for s in symbols:
        if s.kind != "var" or not s.static or s.prototype or s.unused_ok:
            continue
        hits = count_ident(usage, s.name)
        if hits.get(s.path, 0) <= 1 and sum(n for p, n in hits.items() if p != s.path) == 0:
            report.add("unused-static-var", s.name, s.path, s.line, "static variable is never referenced")

    # Unused headers: nothing includes this basename / relative path.
    included: set[str] = set()
    for p in src_files + iter_c_files(ROOT / "test") + iter_c_files(ROOT / "bindings"):
        for inc in extract_includes(p):
            included.add(inc)
            included.add(Path(inc).name)

    for p in src_files:
        if p.suffix != ".h":
            continue
        name = p.name
        rel_src = str(p.relative_to(SRC)) if p.is_relative_to(SRC) else name
        if name in included or rel_src in included:
            continue
        # Some headers are only included via a sibling-relative path
        # (`"../foo.h"`) — already covered by basename. If still unused:
        report.add("unused-header", name, rel(p), 1, "header is never included")

    # Conflicting #define values in src/ (same name, different body).
    defines: dict[str, list[tuple[str, int, str]]] = defaultdict(list)
    for p in src_files:
        for name, value, line, path in extract_defines(p):
            defines[name].append((value, line, path))
    for name, entries in sorted(defines.items()):
        unique_vals = {v for v, _, _ in entries}
        if len(unique_vals) <= 1:
            continue
        # Same include-guard-like redefinition across forks is noise; keep
        # only when values actually differ and appear in more than one file
        # or as clearly conflicting numeric constants.
        locs = "; ".join(f"{path}:{line}={value!r}" for value, line, path in entries)
        report.add("conflicting-define", name, entries[0][2], entries[0][1], locs)

    return report


def print_human(report: Report) -> None:
    grouped = report.by_category()
    if not grouped:
        print("No unused-code findings.")
        return

    titles = {
        "decl-without-def": "Header declarations without a definition",
        "unused-func": "Unused public functions",
        "unused-static": "Unused static functions",
        "unused-static-var": "Unused static variables",
        "unused-header": "Headers that are never included",
        "duplicate-def": "Duplicate non-static function definitions",
        "conflicting-define": "Conflicting #define values",
    }
    order = [
        "decl-without-def",
        "duplicate-def",
        "conflicting-define",
        "unused-header",
        "unused-static",
        "unused-static-var",
        "unused-func",
    ]
    for cat in order:
        items = grouped.get(cat)
        if not items:
            continue
        print(f"\n## {titles[cat]} ({len(items)})")
        for f in items:
            extra = f" — {f.detail}" if f.detail else ""
            print(f"  {f.path}:{f.line}: {f.name}{extra}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument(
        "--fail-on",
        default=",".join(DEFAULT_FAIL_ON),
        help="comma-separated categories that fail CI (default: high-confidence set)",
    )
    parser.add_argument(
        "--allow",
        action="append",
        default=[],
        help="symbol name to ignore (repeatable). Also reads scripts/check_unused.allow",
    )
    args = parser.parse_args()

    allow = set(args.allow)
    allow_file = ROOT / "scripts" / "check_unused.allow"
    if allow_file.exists():
        for line in allow_file.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                allow.add(line)

    report = analyze()
    if allow:
        report.findings = [f for f in report.findings if f.name not in allow]

    if args.json:
        print(json.dumps([asdict(f) for f in report.findings], indent=2))
    else:
        print_human(report)
        print(f"\n{len(report.findings)} finding(s).")

    fail_cats = {c.strip() for c in args.fail_on.split(",") if c.strip()}
    failing = [f for f in report.findings if f.category in fail_cats]
    if failing:
        print(f"\nCI fail-on {sorted(fail_cats)}: {len(failing)} finding(s).", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
