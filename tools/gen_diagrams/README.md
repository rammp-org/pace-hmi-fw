# tools/gen_diagrams

Writes the generated D2 and D3 diagrams into `docs/architecture.md`, between the
`BEGIN GENERATED` / `END GENERATED` markers, and checks them in L0 (CS-ARC-01..03).
Stdlib Python 3.12+, no IDF needed.

| Command | Does |
| --- | --- |
| `python tools/gen_diagrams/gen_diagrams.py write` | rewrite D2 and D3 from the committed sources |
| `python tools/gen_diagrams/gen_diagrams.py check` | exit 0 = PASS; 1 = the committed doc differs (L0) |
| `python tools/gen_diagrams/gen_diagrams.py write --build-dir <build>` | refresh the D3 snapshot from an IDF build, then rewrite |
| `python tools/gen_diagrams/gen_diagrams.py check --build-dir <build>` | also fail if the snapshot differs from that build |
| `... write/check --from-json <file>` | read another `project_description.json` or snapshot instead |
| `python tools/gen_diagrams/gen_diagrams.py selftest` | parsers, rendering and determinism on inline samples |

## Sources

- **D2 (islands and channels):** `components/topology/include/topology.hpp`. The parser reads the
  `TaskRow{...}`, `ForeignTaskRow{...}`, `ComponentRow{...}` and `ChannelRow{...}` initialisers
  in the shape of `docs/plans/refactor.md` §2.2 (comments are stripped first). Islands become
  subgraphs holding their components; adapter rows and foreign tasks named by a channel become
  stadiums; mailboxes are solid arrows, queues thick arrows. Rates and depths are printed as the
  constant's name (`0` on a mailbox reads "on change"). Until `topology.hpp` exists, D2 is a
  placeholder paragraph.
- **D3 (components):** an ESP-IDF build's `project_description.json`, trimmed to
  `docs/diagrams/project_components.json`: each first-party component with its direct `reqs` and
  `priv_reqs`, and the kind (idf, managed, vendored, generated, external) of each direct
  dependency. No paths, no timestamps. First-party is `main` plus `components/*` except `ui`
  (generated), `m5stack-tab5` and `joystick` (vendored); those appear only as targets. IDF and
  registry dependencies are folded into one node per source (ESP-IDF, espp, LVGL, Espressif
  registry, ...) so D3 stays near 25 nodes.
- **Safety marks in D3:** the Components table of `docs/project-profile.md` (rows whose
  "Safety-relevant" cell starts with "yes").

## Regenerating the snapshot

L0 has no IDF build, so `check` reads the committed snapshot. After a build that changes a
component or its `REQUIRES`, run (default variant, any build dir):

```
python tools/gen_diagrams/gen_diagrams.py write --build-dir build
```

and commit `docs/diagrams/project_components.json` with `docs/architecture.md`. `write` and
`check` print a warning (not a failure) when a first-party directory under `components/` is
missing from the snapshot, which means it predates that component.

## Output rules (CS-ARC-02)

Sorted (case-insensitive) node and edge lists, LF line endings, no paths or timestamps. Every
block ends with the template's three `classDef` lines; a duplicate node id fails generation.
