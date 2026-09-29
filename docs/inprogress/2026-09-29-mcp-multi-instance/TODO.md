# TODO — MCP Bridge Multi-Instance Support

Status: **proposal only**, not started.

See [proposal.md](proposal.md) for goals, design, and open questions.

## Remaining

- [ ] Resolve open question #1 (bridge-side intercept vs. JSON-RPC layer)
      before starting implementation.
- [ ] Phase 1: `UNREAL_MCP_PORT` env-var override in
      `core/automation/mcp/src/automation-mcp.cpp`.
- [ ] Phase 2: `manage_instances` tool (`list`/`select`/`add`/`remove`),
      in-memory candidate list.
- [ ] Phase 3: config-file-backed candidate persistence.
- [ ] Documentation: `docs/features/mcp/README.md`, `.recipe/mcp/multi-instance.md`,
      `docs/emulator/design/control-interfaces/cli-interface.md`,
      `core/automation/mcp/README.md`.
