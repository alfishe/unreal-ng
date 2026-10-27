# POC guidelines

A POC (proof of concept) answers a question with measurements before we commit to a design:
"is it worth it", "which variant", "where is the limit". Every POC follows these rules.

## 1. One hierarchy, numbered, named

- All POCs live in `tools/poc/`, one folder each: `tools/poc/NNN-meaningful-name/` — three-digit
  number, global across the project, assigned in order and **never reused** (an abandoned POC
  keeps its number); the name says what is tested, kebab-case (`022-breakpoint-matching`, not
  `022-test`). No other POC locations (no `poc/` at the root, no unnumbered folders).
- The "POC Index" table in `tools/poc/README.md` is the registry: number, name, purpose (the
  question in one line; status and outcome once known, the feature it serves — `docs/inprogress/`
  folder). A POC is added there — taking the next free number — before work starts.
- Features reference POCs by number (`POC 022`); a POC links back to its feature.

## 2. Required: the charter

Every POC folder starts with `README.md` (the charter), titled `# NNN — name`, written before any
measurement:

- **Goals** — the question(s) the POC answers.
- **Requirements** — what a usable answer must cover (platforms, machine models, workloads,
  GUI / headless, paths with and without GPU acceleration).
- **Constraints** — what must not change or be touched, budgets (time, machines), limits of the
  method.
- **Acceptance criteria / expected results** — measurable: what numbers decide "go", "no-go",
  "variant A over B"; expectations stated up front so results can be compared with them.

## 3. As needed, in this order

| File | Content |
|---|---|
| `architecture-<variant>.md` | the architecture of a variant (components, data flow; mermaid diagrams) |
| `design-<variant>.md` | the detailed design of a variant (data structures, algorithms, APIs) |
| `measurements-YYYY-MM-DD-<what>.md` | a measurement report: setup, method, raw-number summary, tables |
| `analysis.md` | what the numbers mean, comparison of variants, against the expectations |
| `conclusions.md` | the answers to the goals, criteria met / not met |
| `proposal.md` | what to do next: adopt (into which feature / `docs/inprogress/` folder), change, drop |

Variants are named the same in every file (`a-baseline`, `b-simd`, ...). A POC that has only one
variant and a short answer may keep architecture / design sections inside `README.md`;
measurements, analysis, conclusions and the proposal are always separate files.

## 4. Where the rest goes

- POC code and build files inside the POC folder (`cpp/`, `python/`, `CMakeLists.txt`); POC code
  is not linked into `core/` or the apps — adopting means re-implementing in the feature with its
  own review, tests and A/B benchmarks (`docs/guidelines/performance-guidelines.md`).
- Build outputs and raw data (logs, captures, traces): `scratch/YYYY-MM-DD-poc-NNN/` (git-ignored),
  never inside the POC folder; summaries and the tables the reports quote are committed with the
  reports.
- The project rules apply: `AGENTS.md` build / test wrappers, cross-platform (gcc / clang / mingw /
  msvc), no commit without an explicit request.
