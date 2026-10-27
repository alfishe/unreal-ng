# The same through MCP: what to ask an AI agent

unreal-qt runs an MCP server at `http://localhost:8092/mcp`. To connect Claude Code to it:

```bash
claude mcp add --transport http unreal-ng http://localhost:8092/mcp
```

Claude Desktop, Cline and others connect through the `unreal-mcp-bridge` binary in the package. Setup:
`docs/features/mcp/integration-guide.md` in the repository.

## Ask for it

> Load `data/examples/01-section-timing/demo.sna` into a new 48K emulator, with the labels from `demo.sym`, and mute
> it. Run 10 frames. Then measure how many T-states `Sparkles` .. `Sparkles.End` takes over 10 passes: min, average
> and max. Also tell me in which passes the frame interrupt (`Isr`) came in between.

> Same on a Pentagon. Why is `Plasma` faster there and constant?

> Run until a pass of `Frame` .. `Frame.End` takes more than 69888 T-states and stop there. What is in `SparkCount`?

The agent gets there with these tools:

| Step | Tool call |
|:--|:--|
| new machine, muted | `emulator_manage {"action":"create","model":"48K"}`, then `invoke_api {"method":"PUT","path":"/api/v1/emulator/{id}/audio/mixer/master","body":{"muted":true}}` |
| program and labels | `load_software {"path":".../demo.sna"}`, `manage_symbols {"action":"import","path":".../demo.sym"}` |
| set-up frames | `control_execution {"action":"run_frames","frames":10}` |
| markers | `control_execution {"action":"bp_add","address":"Sparkles"}` and the same for `Sparkles.End` and `Isr` (a label or a number) |
| run to the next marker | `control_execution {"action":"step_n","count":100000}` (stops at a breakpoint) |
| the time there | `inspect_state {"aspects":["snapshot"]}`: `frame 109 t 55803 of 69888 (global t 7673595)`; a pass = the difference of two global times |
| clean up | `control_execution {"action":"bp_clear"}`, `emulator_manage {"action":"destroy"}` |

Each marker hit costs the agent two tool calls. That suits a few dozen passes. For hundreds of frames, ask the agent
to run `profile_sections.py` (it uses the WebAPI directly) and to read the report it writes:

> Run `python3 data/examples/01-section-timing/profile_sections.py --frames 200` for 48K and for PENTAGON and compare
> the two reports.
