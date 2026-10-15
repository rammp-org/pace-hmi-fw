#!/usr/bin/env python3
"""Generate the D2 and D3 diagrams in docs/architecture.md (CS-ARC-01..03).

  python tools/gen_diagrams/gen_diagrams.py write                rewrite the generated sections
  python tools/gen_diagrams/gen_diagrams.py check                exit 0 = PASS, 1 = FAIL
  python tools/gen_diagrams/gen_diagrams.py write --build-dir build
        refresh docs/diagrams/project_components.json from an ESP-IDF build, then rewrite
  python tools/gen_diagrams/gen_diagrams.py check --from-json <file>
        use another project_description.json or snapshot instead of the committed snapshot
  python tools/gen_diagrams/gen_diagrams.py selftest             parsers and determinism

Sources
- D2: components/topology/include/topology.hpp (the TASKS, FOREIGN_TASKS, COMPONENTS and
  CHANNELS rows, shape as in components/topology/include/topology_types.hpp). Until that file
  exists D2 is a placeholder paragraph.
- D3: first-party components and their direct dependencies. The source is an ESP-IDF build's
  project_description.json, trimmed to a path-free snapshot that is committed as
  docs/diagrams/project_components.json, because L0 has no IDF build. `check` reads the
  snapshot unless --build-dir or --from-json says otherwise.
- Safety marks in D3 come from the Components table of docs/project-profile.md (a row whose
  "Safety-relevant" cell starts with "yes").

Output is sorted, LF only, with no paths or timestamps, so `check` is a byte compare.
Stdlib only, Python 3.12+.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ARCH_DOC = Path("docs/architecture.md")
SNAPSHOT = Path("docs/diagrams/project_components.json")
TOPOLOGY = Path("components/topology/include/topology.hpp")
PROFILE = Path("docs/project-profile.md")

REGEN_CMD = "python tools/gen_diagrams/gen_diagrams.py write"
SNAPSHOT_CMD = "python tools/gen_diagrams/gen_diagrams.py write --build-dir <idf build dir>"

# components/* that are not first-party: drawn only as dependency targets.
GENERATED_COMPONENTS = frozenset({"ui"})
VENDORED_COMPONENTS = frozenset({"m5stack-tab5", "joystick"})

CLASSDEFS = (
    "  classDef hw fill:#d9dde3,stroke:#7a8590,color:#111\n"
    "  classDef safety stroke:#c0392b,stroke-width:3px\n"
    "  classDef gen stroke-dasharray:5 4\n"
)
CLASSDEF_LINES = tuple(line.strip() for line in CLASSDEFS.splitlines())

NODE_BUDGET = 25  # CS-ARC-01: about 25 nodes per diagram; warn well above it
NODE_WARN = 30

SNAPSHOT_FORMAT = 1

# Display groups for dependencies that are not drawn one by one.
GROUP_IDF = "ESP-IDF"
MANAGED_GROUP_LABELS = {"espp": "espp", "lvgl": "LVGL", "espressif": "Espressif registry"}


class GenError(Exception):
    """A source could not be read or a generated section could not be placed."""


# --------------------------------------------------------------------------------------------
# Markers
# --------------------------------------------------------------------------------------------


def begin_marker(name: str) -> str:
    return f"<!-- BEGIN GENERATED {name} (tools/gen_diagrams): do not edit -->"


def end_marker(name: str) -> str:
    return f"<!-- END GENERATED {name} -->"


def replace_between_markers(text: str, name: str, body: str) -> str:
    """Return text with everything between the two markers of `name` replaced by body."""
    begin, end = begin_marker(name), end_marker(name)
    if text.count(begin) != 1 or text.count(end) != 1:
        raise GenError(f"expected exactly one '{begin}' and one '{end}'")
    head, rest = text.split(begin, 1)
    _, tail = rest.split(end, 1)
    if not body.endswith("\n"):
        body += "\n"
    return f"{head}{begin}\n{body}{end}{tail}"


# --------------------------------------------------------------------------------------------
# Mermaid helpers
# --------------------------------------------------------------------------------------------

_ID_RE = re.compile(r"[^A-Za-z0-9]+")


def node_id(prefix: str, name: str) -> str:
    """A Mermaid-safe id: letters, digits and underscores, with a prefix (never a keyword)."""
    return f"{prefix}_{_ID_RE.sub('_', name).strip('_')}"


def mermaid_label(text: str) -> str:
    return text.replace('"', "#quot;").replace("<", "#lt;").replace(">", "#gt;")


_DECL_RE = re.compile(r"^\s*(?:subgraph\s+)?([A-Za-z][A-Za-z0-9_]*)\s*(?:\[|\(|\{)")


def validate_mermaid(block: str) -> list[str]:
    """Problems with a generated block: duplicate node ids or a missing classDef line."""
    problems = []
    seen: set[str] = set()
    for line in block.splitlines():
        stripped = line.strip()
        if "-->" in stripped or "==>" in stripped or "-.->" in stripped:
            continue
        m = _DECL_RE.match(line)
        if m:
            if m.group(1) in seen:
                problems.append(f"duplicate node id {m.group(1)}")
            seen.add(m.group(1))
    body = [line.strip() for line in block.splitlines() if not line.startswith("```")]
    tail = body[-len(CLASSDEF_LINES):]
    if tuple(tail) != CLASSDEF_LINES:
        problems.append("the block does not end with the template's three classDef lines")
    return problems


# --------------------------------------------------------------------------------------------
# D3: project_description.json -> snapshot -> diagram
# --------------------------------------------------------------------------------------------


def _norm(path: str) -> str:
    return path.replace("\\", "/").rstrip("/").lower()


def classify(name: str, comp_dir: str, project_path: str, idf_path: str) -> str:
    """first_party | generated | vendored | external | managed | idf | other."""
    d, proj, idf = _norm(comp_dir), _norm(project_path), _norm(idf_path)
    if proj and d.startswith(proj + "/"):
        rel = d[len(proj) + 1:]
        if rel == "main":
            return "first_party"
        if rel.startswith("components/"):
            if name in GENERATED_COMPONENTS:
                return "generated"
            if name in VENDORED_COMPONENTS:
                return "vendored"
            return "first_party"
        if rel.startswith("managed_components/"):
            return "managed"
        if rel.startswith("external/"):
            return "external"
    if idf and d.startswith(idf + "/"):
        return "idf"
    if "/managed_components/" in d:
        return "managed"
    return "other"


def trim_description(desc: dict) -> dict:
    """Path-free snapshot: first-party components with their reqs, and each direct dep's kind."""
    info = desc.get("build_component_info")
    if not isinstance(info, dict):
        raise GenError("project_description.json has no build_component_info")
    project_path = str(desc.get("project_path", ""))
    idf_path = str(desc.get("idf_path", ""))
    kinds = {
        name: classify(name, str(ci.get("dir", "")), project_path, idf_path)
        for name, ci in info.items()
    }
    components: dict[str, dict] = {}
    deps: dict[str, str] = {}
    for name in sorted(info):
        if kinds[name] != "first_party":
            continue
        ci = info[name]
        reqs = sorted(set(ci.get("reqs") or []))
        priv = sorted(set(ci.get("priv_reqs") or []) - set(reqs))
        components[name] = {"reqs": reqs, "priv_reqs": priv}
        for dep in reqs + priv:
            if dep not in components and kinds.get(dep, "other") != "first_party":
                deps[dep] = kinds.get(dep, "other")
    return {
        "format": SNAPSHOT_FORMAT,
        "note": "Generated by tools/gen_diagrams from an ESP-IDF build's project_description.json. "
        f"Do not edit; regenerate with: {SNAPSHOT_CMD}",
        "components": components,
        "dependencies": dict(sorted(deps.items())),
    }


def dump_snapshot(snap: dict) -> str:
    return json.dumps(snap, indent=2, sort_keys=False, ensure_ascii=True) + "\n"


def load_components(path: Path) -> dict:
    """Read a full project_description.json or a snapshot; return a snapshot."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise GenError(f"cannot read {path.name}: {exc}") from exc
    if "build_component_info" in data:
        return trim_description(data)
    if data.get("format") != SNAPSHOT_FORMAT or "components" not in data:
        raise GenError(f"{path.name} is neither a project_description.json nor a snapshot")
    return data


def parse_profile_safety(text: str) -> set[str]:
    """Component names whose 'Safety-relevant' cell starts with 'yes' in the profile table."""
    names: set[str] = set()
    in_section = False
    col = -1
    for line in text.splitlines():
        if line.startswith("## "):
            in_section = line.strip() == "## Components"
            col = -1
            continue
        if not in_section or not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if col < 0:
            for i, cell in enumerate(cells):
                if cell.lower().startswith("safety"):
                    col = i
            continue
        if col >= len(cells) or not cells[col].lower().startswith("yes"):
            continue
        m = re.search(r"`([^`]+)`", cells[0])
        if m:
            names.add(m.group(1).rstrip("/").split("/")[-1])
    return names


@dataclass(frozen=True)
class Node:
    nid: str
    label: str
    css: str = ""  # gen | safety | hw | ""

    def decl(self) -> str:
        suffix = f":::{self.css}" if self.css else ""
        return f'  {self.nid}["{mermaid_label(self.label)}"]{suffix}'


def _ci(text: str) -> tuple[str, str]:
    """Case-insensitive sort key that still gives one order for every input."""
    return (text.lower(), text)


def _dep_group(name: str, kind: str) -> str | None:
    """The group a dependency is folded into, or None when it is drawn as its own node."""
    if kind in ("idf", "other"):
        return GROUP_IDF
    if kind == "managed":
        namespace = name.split("__", 1)[0] if "__" in name else name
        return MANAGED_GROUP_LABELS.get(namespace, namespace)
    return None


def render_d3(snap: dict, safety: set[str]) -> tuple[str, int]:
    """The D3 section body (markdown + mermaid) and its node count."""
    comps: dict[str, dict] = snap["components"]
    deps: dict[str, str] = snap["dependencies"]
    nodes: dict[str, Node] = {}
    edges: set[tuple[str, str]] = set()
    groups: dict[str, set[str]] = {}

    def add(node: Node) -> str:
        old = nodes.get(node.nid)
        if old is not None and old != node:
            raise GenError(f"node id collision: {node.nid} ({old.label} / {node.label})")
        nodes[node.nid] = node
        return node.nid

    for name in comps:
        add(Node(node_id("c", name), name, "safety" if name in safety else ""))
    for src in sorted(comps):
        src_id = node_id("c", src)
        for dep in comps[src]["reqs"] + comps[src]["priv_reqs"]:
            if dep in comps:
                dst = node_id("c", dep)
            else:
                kind = deps.get(dep, "other")
                group = _dep_group(dep, kind)
                if group is not None:
                    groups.setdefault(group, set()).add(dep)
                    dst = node_id("g", group)
                else:
                    css = "gen" if kind == "generated" else ("safety" if dep in safety else "")
                    label = {"vendored": f"{dep} (vendored)", "external": f"{dep} (submodule)"}.get(
                        kind, dep
                    )
                    dst = add(Node(node_id("c", dep), label, css))
            edges.add((src_id, dst))
    for group, members in groups.items():
        add(Node(node_id("g", group), f"{group} · {len(members)}"))

    lines = ["```mermaid", "flowchart LR"]
    lines += [nodes[k].decl() for k in sorted(nodes, key=_ci)]
    for src, dst in sorted(edges, key=lambda e: (_ci(e[0]), _ci(e[1]))):
        lines.append(f"  {src} -.-> {dst}")
    block = "\n".join(lines) + "\n" + CLASSDEFS + "```\n"

    out = [
        "First-party components (`main` and `components/*` except "
        + ", ".join(f"`{n}`" for n in sorted(GENERATED_COMPONENTS | VENDORED_COMPONENTS))
        + ") and their direct `REQUIRES`/`PRIV_REQUIRES`. Vendored, generated and submodule "
        "components appear only as targets. Third-party dependencies are folded into one node "
        "per source; the number is how many of its components are used directly.",
        "",
        block,
    ]
    if groups:
        out.append("<details><summary>Folded dependencies</summary>\n")
        for group in sorted(groups, key=_ci):
            out.append(f"- **{group}**: " + ", ".join(f"`{m}`" for m in sorted(groups[group])))
        out.append("\n</details>\n")
    out.append(
        f"Source: `{SNAPSHOT.as_posix()}`, a path-free snapshot of an ESP-IDF build's "
        f"`project_description.json`. Regenerate after a build with `{SNAPSHOT_CMD}`."
    )
    body = "\n".join(out) + "\n"
    problems = validate_mermaid(block)
    if problems:
        raise GenError("D3: " + "; ".join(problems))
    return body, len(nodes)


# --------------------------------------------------------------------------------------------
# D2: topology.hpp -> diagram
# --------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class TaskRowT:
    task: str  # enum name, e.g. CONTROL
    name: str
    role: str  # ISLAND | ADAPTER
    safety: bool
    start: str


@dataclass(frozen=True)
class ChannelRowT:
    cid: str
    kind: str  # MAILBOX | QUEUE | ATOMIC | CONST
    message: str
    producer: str
    consumer: str
    rate: str
    full: str


@dataclass
class Topology:
    tasks: list[TaskRowT] = field(default_factory=list)
    foreign: list[str] = field(default_factory=list)
    hosted: dict[str, bool] = field(default_factory=dict)  # adapter on a foreign task -> safety
    components: list[tuple[str, str]] = field(default_factory=list)  # (name, task enum)
    channels: list[ChannelRowT] = field(default_factory=list)


def strip_cpp_comments(text: str) -> str:
    """Drop // and /* */ comments, keeping string literals intact."""
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _split_args(body: str) -> list[str]:
    args, cur, in_str = [], [], False
    for c in body:
        if c == '"':
            in_str = not in_str
        if c == "," and not in_str:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
    if "".join(cur).strip():
        args.append("".join(cur).strip())
    return args


def _rows(text: str, row_type: str) -> list[list[str]]:
    return [_split_args(m.group(1)) for m in re.finditer(rf"\b{row_type}\s*\{{([^{{}}]*)\}}", text)]


def _enum(arg: str, enum: str) -> str:
    prefix = f"{enum}::"
    if not arg.startswith(prefix):
        raise GenError(f"expected {prefix}..., got {arg!r}")
    return arg[len(prefix):]


def _str(arg: str) -> str:
    if len(arg) < 2 or arg[0] != '"' or arg[-1] != '"':
        raise GenError(f"expected a string literal, got {arg!r}")
    return arg[1:-1]


def parse_topology(text: str) -> Topology:
    """Read the TaskRow, ForeignTaskRow, ComponentRow and ChannelRow initialisers."""
    text = strip_cpp_comments(text)
    topo = Topology()
    for a in _rows(text, "TaskRow"):
        if len(a) != 8:
            raise GenError(f"TaskRow needs 8 fields, got {len(a)}: {a}")
        topo.tasks.append(
            TaskRowT(_enum(a[0], "Task"), _str(a[1]), _enum(a[2], "Role"), a[6] == "true",
                     _enum(a[7], "Start"))
        )
    for a in _rows(text, "ForeignTaskRow"):
        # name, start[, hosts[, safety]]; the name is a literal or ANY_TASK (the log hook)
        if not 2 <= len(a) <= 4:
            raise GenError(f"ForeignTaskRow needs 2 to 4 fields, got {len(a)}: {a}")
        topo.foreign.append("*" if a[0] == "ANY_TASK" else _str(a[0]))
        if len(a) >= 3:
            topo.hosted[_enum(a[2], "Task")] = len(a) == 4 and a[3] == "true"
    for a in _rows(text, "ComponentRow"):
        if len(a) != 2:
            raise GenError(f"ComponentRow needs 2 fields, got {len(a)}: {a}")
        topo.components.append((_str(a[0]), _enum(a[1], "Task")))
    for a in _rows(text, "ChannelRow"):
        # id (Ch::X), name, kind, message, producer, consumer, rate or depth, full policy
        if len(a) != 8:
            raise GenError(f"ChannelRow needs 8 fields, got {len(a)}: {a}")
        if _enum(a[0], "Ch") != _str(a[1]):
            raise GenError(f"ChannelRow id {a[0]} and name {a[1]} differ")
        topo.channels.append(
            ChannelRowT(_str(a[1]), _enum(a[2], "Kind"), _str(a[3]), _enum(a[4], "Task"),
                        _enum(a[5], "Task"), a[6], _enum(a[7], "Full"))
        )
    if not topo.tasks or not topo.channels:
        raise GenError("topology.hpp: no TaskRow or no ChannelRow found")
    return topo


def _title(enum_name: str) -> str:
    return enum_name if len(enum_name) <= 2 else enum_name.replace("_", " ").capitalize()


def _channel_edge(ch: ChannelRowT) -> tuple[str, str]:
    """(arrow, label) for a channel, per the legend."""
    msg = mermaid_label(ch.message)
    rate = "" if ch.rate in ("0", "ON_CHANGE") else f" · {ch.rate}"
    if ch.kind == "QUEUE":
        return "==>", f"Queue#lt;{msg}#gt;{rate}"
    if ch.kind == "MAILBOX":
        return "-->", f"Mailbox#lt;{msg}#gt;{rate or ' · on change'}"
    if ch.kind == "ATOMIC":
        return "-->", f"atomic {msg}"
    return "-->", f"const {msg}"


def render_d2(topo: Topology) -> tuple[str, int]:
    tasks = {t.task: t for t in topo.tasks}
    by_task: dict[str, list[str]] = {}
    for comp, task in topo.components:
        if task not in tasks:
            raise GenError(f"component {comp} names unknown task {task}")
        by_task.setdefault(task, []).append(comp)

    endpoint: dict[str, str] = {}
    lines = ["```mermaid", "flowchart LR"]
    count = 0
    seen_ids: set[str] = set()

    def claim(nid: str) -> str:
        if nid in seen_ids:
            raise GenError(f"node id collision: {nid}")
        seen_ids.add(nid)
        return nid

    for task in sorted(t for t in tasks if tasks[t].role == "ISLAND"):
        row = tasks[task]
        sid = claim(node_id("i", task))
        endpoint[task] = sid
        lines.append(f'  subgraph {sid}["{_title(task)} island · {mermaid_label(row.name)}"]')
        comps = sorted(by_task.get(task, [])) or [row.name]
        for comp in comps:
            css = ":::safety" if row.safety else ""
            lines.append(f'    {claim(node_id("c", comp))}["{mermaid_label(comp)}"]{css}')
            count += 1
        lines.append("  end")
        count += 1
    used = {ch.producer for ch in topo.channels} | {ch.consumer for ch in topo.channels}
    adapters = {t for t in tasks if tasks[t].role == "ADAPTER"} | (used - set(tasks))
    for task in sorted(adapters):
        row = tasks.get(task)
        label = row.name if row else task.lower()
        safety = row.safety if row else topo.hosted.get(task, False)
        css = ":::safety" if safety else ""
        nid = claim(node_id("a", task))
        endpoint[task] = nid
        lines.append(f'  {nid}(["{mermaid_label(label)}"]){css}')
        count += 1
    for ch in sorted(topo.channels, key=lambda c: c.cid):
        arrow, label = _channel_edge(ch)
        lines.append(f"  {endpoint[ch.producer]} {arrow}|{label}| {endpoint[ch.consumer]}")
    block = "\n".join(lines) + "\n" + CLASSDEFS + "```\n"
    problems = validate_mermaid(block)
    if problems:
        raise GenError("D2: " + "; ".join(problems))
    body = (
        f"Islands (subgraphs), adapters (stadiums) and channels from `{TOPOLOGY.as_posix()}`. "
        "Components inside an island are those its COMPONENTS rows assign to the island's task.\n"
        "\n" + block
    )
    return body, count


D2_PLACEHOLDER = (
    "D2 is generated once `topology.hpp` lands (draft on `dev_ai_refactor_topology`). "
    "Until then the target sketch is in [the plan, §2.5](plans/refactor.md#25-diagrams).\n"
)


# --------------------------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------------------------


def first_party_on_disk(repo: Path) -> set[str]:
    names = {"main"} if (repo / "main" / "CMakeLists.txt").is_file() else set()
    comp_root = repo / "components"
    if comp_root.is_dir():
        for d in comp_root.iterdir():
            if (d / "CMakeLists.txt").is_file() and d.name not in (
                GENERATED_COMPONENTS | VENDORED_COMPONENTS
            ):
                names.add(d.name)
    return names


def generate(repo: Path, snap: dict) -> tuple[str, list[str]]:
    """The new docs/architecture.md text and warnings (stderr only, never in the output)."""
    warnings: list[str] = []
    doc_path = repo / ARCH_DOC
    try:
        text = doc_path.read_bytes().decode("utf-8")
    except OSError as exc:
        raise GenError(f"cannot read {ARCH_DOC.as_posix()}: {exc}") from exc
    text = text.replace("\r\n", "\n")

    topo_path = repo / TOPOLOGY
    if topo_path.is_file():
        d2, n2 = render_d2(parse_topology(topo_path.read_text(encoding="utf-8")))
        if n2 > NODE_WARN:
            warnings.append(f"D2 has {n2} nodes (budget about {NODE_BUDGET}, CS-ARC-01): split it")
    else:
        d2 = D2_PLACEHOLDER

    profile = repo / PROFILE
    safety = parse_profile_safety(profile.read_text(encoding="utf-8")) if profile.is_file() else set()
    d3, n3 = render_d3(snap, safety)
    if n3 > NODE_WARN:
        warnings.append(f"D3 has {n3} nodes (budget about {NODE_BUDGET}, CS-ARC-01): split it")
    missing = sorted(first_party_on_disk(repo) - set(snap["components"]))
    if missing:
        warnings.append(
            "the D3 snapshot predates first-party component(s) "
            + ", ".join(missing)
            + f"; after the next IDF build run: {SNAPSHOT_CMD}"
        )

    text = replace_between_markers(text, "D2", d2)
    text = replace_between_markers(text, "D3", d3)
    return text, warnings


def _resolve_snapshot(repo: Path, args: argparse.Namespace) -> tuple[dict, str | None]:
    """The snapshot to draw from, and its new file text when a build was given."""
    if args.build_dir:
        desc = Path(args.build_dir) / "project_description.json"
        snap = load_components(desc)
        return snap, dump_snapshot(snap)
    src = Path(args.from_json) if args.from_json else repo / SNAPSHOT
    return load_components(src), None


def cmd_write(repo: Path, args: argparse.Namespace) -> int:
    snap, snap_text = _resolve_snapshot(repo, args)
    text, warnings = generate(repo, snap)
    if snap_text is not None:
        (repo / SNAPSHOT).parent.mkdir(parents=True, exist_ok=True)
        (repo / SNAPSHOT).write_bytes(snap_text.encode("utf-8"))
        print(f"wrote {SNAPSHOT.as_posix()}")
    (repo / ARCH_DOC).write_bytes(text.encode("utf-8"))
    print(f"wrote {ARCH_DOC.as_posix()}")
    for w in warnings:
        print(f"WARNING: {w}", file=sys.stderr)
    return 0


def cmd_check(repo: Path, args: argparse.Namespace) -> int:
    snap, snap_text = _resolve_snapshot(repo, args)
    text, warnings = generate(repo, snap)
    for w in warnings:
        print(f"WARNING: {w}", file=sys.stderr)
    failed = False
    committed = (repo / ARCH_DOC).read_bytes()
    if committed != text.encode("utf-8"):
        failed = True
        print(f"FAIL: {ARCH_DOC.as_posix()} differs from the generated diagrams; run: {REGEN_CMD}")
        old = committed.decode("utf-8", "replace").replace("\r\n", "\n").splitlines()
        for i, (a, b) in enumerate(zip(old, text.splitlines())):
            if a != b:
                print(f"  first difference at line {i + 1}:\n    committed: {a}\n    generated: {b}")
                break
        if b"\r\n" in committed:
            print("  the committed file has CRLF line endings; generated output is LF")
    if snap_text is not None:
        snap_path = repo / SNAPSHOT
        if not snap_path.is_file() or snap_path.read_bytes() != snap_text.encode("utf-8"):
            failed = True
            print(f"FAIL: {SNAPSHOT.as_posix()} differs from this build; run: {SNAPSHOT_CMD}")
    if failed:
        return 1
    print("PASS: generated diagrams match docs/architecture.md")
    return 0


# --------------------------------------------------------------------------------------------
# Self test
# --------------------------------------------------------------------------------------------

_SAMPLE_TOPOLOGY = r"""
namespace hmi::topo {
inline constexpr std::array TASKS{
  TaskRow{Task::UI,      "ui",      Role::ISLAND,  16384, 20, 1,  false, Start::BOOT},
  TaskRow{Task::CONTROL, "control", Role::ISLAND,   6144, 22, 0,  true,  Start::BOOT},
  // TaskRow{Task::GHOST, "ghost", Role::ISLAND, 1, 1, 1, false, Start::BOOT},
  TaskRow{Task::STICK_BUTTON, "Button", Role::ADAPTER, 4096, 5, -1, true, Start::BOOT},
};
inline constexpr std::array FOREIGN_TASKS{
  ForeignTaskRow{"rtps_worker", Start::BOOT, Task::RTPS_RX, true},
  ForeignTaskRow{ANY_TASK, Start::BOOT, Task::LOG_HOOK}, ForeignTaskRow{"main", Start::BOOT},
};
inline constexpr std::array COMPONENTS{
  ComponentRow{"hmi_ui", Task::UI}, ComponentRow{"stick", Task::CONTROL},
  ComponentRow{"drive_session", Task::CONTROL},
};
inline constexpr std::array CHANNELS{
  /* MCB -> HMI, "quoted, with a comma" */
  ChannelRow{Ch::MCB_TO_CONTROL, "MCB_TO_CONTROL", Kind::MAILBOX, "McbStatusMsg", Task::RTPS_RX, Task::CONTROL, MIB_STATUS_HZ, Full::OVERWRITE},
  ChannelRow{Ch::DRIVE_INTENT,   "DRIVE_INTENT",   Kind::QUEUE,   "DriveIntentMsg", Task::UI, Task::CONTROL, DRIVE_INTENT_DEPTH, Full::RAISE_FAULT},
  ChannelRow{Ch::STICK_BUTTON,   "STICK_BUTTON",   Kind::ATOMIC,  "bool", Task::STICK_BUTTON, Task::CONTROL, 0, Full::OVERWRITE},
  ChannelRow{Ch::STICK_SETTINGS, "STICK_SETTINGS", Kind::MAILBOX, "StickSettingsMsg", Task::UI, Task::CONTROL, ON_CHANGE, Full::OVERWRITE},
};
}
"""

_SAMPLE_DESC = {
    "project_path": "C:/w/x",
    "idf_path": "C:/esp/v6.0/esp-idf",
    "build_component_info": {
        "main": {"dir": "C:/w/x/main", "reqs": [], "priv_reqs": [
            "fw_core", "ui", "joystick", "espp__rtps", "espp__adc", "lvgl__lvgl", "esp_wifi",
            "espressif__w5500", "rammp_rtps_messages", "joltwallet__littlefs"]},
        "fw_core": {"dir": "C:/w/x/components/fw_core", "reqs": ["freertos"],
                    "priv_reqs": ["espp__logger", "freertos"]},
        "ui": {"dir": "C:/w/x/components/ui", "reqs": ["lvgl__lvgl"], "priv_reqs": []},
        "joystick": {"dir": "C:/w/x/components/joystick", "reqs": ["espp__math"], "priv_reqs": []},
        "espp__rtps": {"dir": "C:/w/x/managed_components/espp__rtps", "reqs": [], "priv_reqs": []},
        "espp__adc": {"dir": "C:/w/x/managed_components/espp__adc", "reqs": [], "priv_reqs": []},
        "espp__logger": {"dir": "C:/w/x/managed_components/espp__logger", "reqs": [], "priv_reqs": []},
        "espp__math": {"dir": "C:/w/x/managed_components/espp__math", "reqs": [], "priv_reqs": []},
        "lvgl__lvgl": {"dir": "C:/w/x/managed_components/lvgl__lvgl", "reqs": [], "priv_reqs": []},
        "espressif__w5500": {"dir": "C:/w/x/managed_components/espressif__w5500", "reqs": [],
                             "priv_reqs": []},
        "joltwallet__littlefs": {"dir": "C:/w/x/managed_components/joltwallet__littlefs",
                                 "reqs": [], "priv_reqs": []},
        "esp_wifi": {"dir": "C:/esp/v6.0/esp-idf/components/esp_wifi", "reqs": [], "priv_reqs": []},
        "freertos": {"dir": "C:/esp/v6.0/esp-idf/components/freertos", "reqs": [], "priv_reqs": []},
        "rammp_rtps_messages": {"dir": "C:/w/x/external/rammp-rtps/components/rammp_rtps_messages",
                                "reqs": [], "priv_reqs": []},
    },
}

_SAMPLE_PROFILE = """
## Components
| Component | Concern (one sentence) | Safety-relevant | Builds for linux | D4 diagram |
| --- | --- | --- | --- | --- |
| `main` | everything | yes | no | none |
| `components/joystick` | stick | yes | yes | none |
| `components/ui` | export | no | n/a | none |

## Tasks and islands
| `not_a_component` | x | yes | | |
"""

_SAMPLE_DOC = (
    "# A\n\n## D2\n" + begin_marker("D2") + "\nold\n" + end_marker("D2") + "\n\n## D3\n"
    + begin_marker("D3") + "\n" + end_marker("D3") + "\ntail\n"
)


def selftest() -> int:
    failures: list[str] = []

    def expect(cond: bool, what: str) -> None:
        print(f"  {'ok  ' if cond else 'FAIL'} {what}")
        if not cond:
            failures.append(what)

    print("topology parser")
    topo = parse_topology(_SAMPLE_TOPOLOGY)
    expect([t.task for t in topo.tasks] == ["UI", "CONTROL", "STICK_BUTTON"],
           "three TaskRows, the commented-out one skipped")
    expect(topo.tasks[1].safety and not topo.tasks[0].safety, "safety flag read")
    expect(topo.tasks[2].role == "ADAPTER" and topo.tasks[2].name == "Button", "adapter row")
    expect(topo.foreign == ["rtps_worker", "*", "main"], "foreign tasks, ANY_TASK as '*'")
    expect(topo.hosted == {"RTPS_RX": True, "LOG_HOOK": False}, "hosted adapters and safety")
    expect(("stick", "CONTROL") in topo.components and len(topo.components) == 3, "components")
    expect([c.cid for c in topo.channels] ==
           ["MCB_TO_CONTROL", "DRIVE_INTENT", "STICK_BUTTON", "STICK_SETTINGS"], "channels")
    expect(topo.channels[0].producer == "RTPS_RX" and topo.channels[0].rate == "MIB_STATUS_HZ",
           "channel fields")
    try:
        parse_topology('TaskRow{Task::UI, "ui"}; ChannelRow{"X"}')
        expect(False, "a short row is rejected")
    except GenError:
        expect(True, "a short row is rejected")
    try:
        parse_topology('TaskRow{Task::UI, "ui", Role::ISLAND, 1, 1, 1, false, Start::BOOT}; '
                       'ChannelRow{Ch::A, "B", Kind::QUEUE, "M", Task::UI, Task::UI, 1, Full::RAISE_FAULT}')
        expect(False, "a channel whose id and name differ is rejected")
    except GenError:
        expect(True, "a channel whose id and name differ is rejected")

    print("D2 render")
    d2, n2 = render_d2(topo)
    expect('subgraph i_CONTROL["Control island · control"]' in d2, "island subgraph title")
    expect('subgraph i_UI["UI island · ui"]' in d2, "two-letter task name kept upper case")
    expect('c_stick["stick"]:::safety' in d2, "component on a safety task marked")
    expect('a_RTPS_RX(["rtps_rx"]):::safety' in d2,
           "hosted producer drawn as an adapter, safety from its FOREIGN_TASKS row")
    expect('a_STICK_BUTTON(["Button"]):::safety' in d2, "adapter row drawn as a safety stadium")
    expect("i_UI ==>|Queue#lt;DriveIntentMsg#gt; · DRIVE_INTENT_DEPTH| i_CONTROL" in d2,
           "queue is a thick arrow")
    expect("a_RTPS_RX -->|Mailbox#lt;McbStatusMsg#gt; · MIB_STATUS_HZ| i_CONTROL" in d2,
           "mailbox is a solid arrow")
    expect("Mailbox#lt;StickSettingsMsg#gt; · on change" in d2, "ON_CHANGE mailbox is on change")
    expect(not validate_mermaid(d2.split("\n\n", 1)[1]), "D2 block valid")
    expect(n2 == 3 + 2 + 2, f"D2 node count 7 (got {n2})")

    print("project_description trim")
    snap = trim_description(_SAMPLE_DESC)
    dumped = dump_snapshot(snap)
    expect(sorted(snap["components"]) == ["fw_core", "main"], "first-party = main + fw_core")
    expect(snap["components"]["fw_core"] == {"reqs": ["freertos"], "priv_reqs": ["espp__logger"]},
           "priv_reqs drop what reqs already has")
    expect(snap["dependencies"]["ui"] == "generated" and snap["dependencies"]["joystick"] == "vendored",
           "generated and vendored kinds")
    expect(snap["dependencies"]["esp_wifi"] == "idf" and
           snap["dependencies"]["rammp_rtps_messages"] == "external", "idf and external kinds")
    expect("espp__math" not in snap["dependencies"], "a dependency's own deps are not kept")
    expect(":/" not in dumped and "C:" not in dumped and "esp-idf" not in dumped, "no paths")
    expect(load_components_from_obj(snap) == snap, "a snapshot reloads as itself")

    print("profile parser")
    safety = parse_profile_safety(_SAMPLE_PROFILE)
    expect(safety == {"main", "joystick"}, f"safety set (got {sorted(safety)})")

    print("D3 render")
    d3, n3 = render_d3(snap, safety)
    expect('c_main["main"]:::safety' in d3, "main marked safety")
    expect('c_ui["ui"]:::gen' in d3, "generated ui marked gen")
    expect('c_joystick["joystick (vendored)"]:::safety' in d3, "vendored target labelled")
    expect('g_espp["espp · 3"]' in d3, "espp folded, 3 used directly")
    expect('g_ESP_IDF["ESP-IDF · 2"]' in d3, "IDF folded")
    expect('g_joltwallet["joltwallet · 1"]' in d3, "unknown registry namespace folded by name")
    expect("c_main -.-> c_fw_core" in d3 and "c_fw_core -.-> g_espp" in d3, "dotted dependencies")
    expect("c_joystick -.->" not in d3 and "c_ui -.->" not in d3, "targets have no out-edges")
    expect(n3 == 10, f"D3 node count 10 (got {n3})")

    print("markers")
    doc = replace_between_markers(_SAMPLE_DOC, "D2", "new")
    expect("\nnew\n" + end_marker("D2") in doc and "old" not in doc, "D2 body replaced")
    expect(doc.endswith("tail\n"), "text after the markers kept")
    try:
        replace_between_markers("no markers", "D2", "x")
        expect(False, "missing markers rejected")
    except GenError:
        expect(True, "missing markers rejected")

    print("determinism")
    expect(render_d2(parse_topology(_SAMPLE_TOPOLOGY)) == (d2, n2), "D2 twice identical")
    expect(dump_snapshot(trim_description(_SAMPLE_DESC)) == dumped, "snapshot twice identical")
    expect(render_d3(trim_description(_SAMPLE_DESC), safety) == (d3, n3), "D3 twice identical")
    shuffled = dict(reversed(list(_SAMPLE_DESC["build_component_info"].items())))
    rev = dict(_SAMPLE_DESC, build_component_info=shuffled)
    expect(dump_snapshot(trim_description(rev)) == dumped, "input order does not matter")
    if (REPO / ARCH_DOC).is_file() and (REPO / SNAPSHOT).is_file():
        repo_snap = load_components(REPO / SNAPSHOT)
        first, _ = generate(REPO, repo_snap)
        second, _ = generate(REPO, load_components(REPO / SNAPSHOT))
        expect(first == second, "repo document generated twice identical")
        expect("\r" not in first, "repo document is LF only")

    print(f"selftest: {'PASS' if not failures else 'FAIL'} ({len(failures)} failed)")
    return 0 if not failures else 1


def load_components_from_obj(obj: dict) -> dict:
    """load_components() on an in-memory object (self test only)."""
    if "build_component_info" in obj:
        return trim_description(obj)
    return json.loads(dump_snapshot(obj))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    for name in ("write", "check"):
        p = sub.add_parser(name)
        src = p.add_mutually_exclusive_group()
        src.add_argument("--build-dir", help="ESP-IDF build dir holding project_description.json")
        src.add_argument("--from-json", help="a project_description.json or a snapshot")
        p.add_argument("--repo", default=str(REPO), help=argparse.SUPPRESS)
    sub.add_parser("selftest")
    args = parser.parse_args(argv)
    try:
        if args.cmd == "selftest":
            return selftest()
        repo = Path(args.repo)
        return cmd_write(repo, args) if args.cmd == "write" else cmd_check(repo, args)
    except GenError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
