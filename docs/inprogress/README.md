# In-Progress Documentation Rules

The `docs/inprogress/` directory is used for tracking ongoing design documents, brainstorming, technical analysis, and feature specifications that are currently under development. 

## Directory Naming Convention
Every new topic must have its own directory named using the `YYYY-MM-DD-topic-name` format. 
- **Example**: `2026-08-26-debugger-enhancements`

## File Naming Convention
Markdown files within these directories (and all documentation files generally) must use lowercase with hyphens (kebab-case).
- **Example**: `technical-analysis.md`
- Do NOT use underscores.

## Status Markers

Every topic folder carries exactly one status marker file at its root:

| Marker | Meaning |
|--------|---------|
| `DONE.md` | Work item is **fully** implemented, verified and (where applicable) committed. Records what landed, the evidence, and where permanent documentation lives. |
| `TODO.md` | Work item is **partially done or not started**. Records progress so far, what remains, and pointers to the designs/plans holding the details. |
| (none) | Freshly created folder awaiting first triage — classify it promptly. |

Rules:

- The root [PLAN.md](PLAN.md) is the **cumulative, priority-ordered plan across all
  folders marked `TODO.md`**. When a folder's status changes, update both its marker
  and PLAN.md.
- Marker files use UPPERCASE names deliberately (they sort first and are greppable);
  the kebab-case rule applies to every other file in this tree.
- Adding/removing a marker is a documentation change — safe to do together with any
  commit; it never requires a code change.

## Lifecycle
Once a feature or specification is fully implemented and finalized, its documentation should be cleaned up and moved to the appropriate permanent location in the main `docs/` tree. The folder itself stays in `docs/inprogress/` with a `DONE.md` tombstone summarizing the outcome and evidence.
When work is partially complete and still valuable, leave the folder in place with a `TODO.md` explaining progress and remainders, and make sure the item appears in [PLAN.md](PLAN.md).
