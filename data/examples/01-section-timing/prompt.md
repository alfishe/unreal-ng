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

Each marker hit costs the agent two tool calls. That suits a few dozen passes. For hundreds of frames the emulator's
section timer measures on its own, at full speed, with the interrupts left out exactly:

| Step | Tool call |
|:--|:--|
| start it | `analyze_performance {"action":"section_timer","op":"start","sections":["Plasma","Sparkles","Frame"],"break_if":["Frame>69888"]}` (no `sections`: every label with an `.End`) |
| run | `control_execution {"action":"run_until","condition":"FRAME < 0","max_frames":200,"breakpoints":true}` (ends early at a pass over the limit; `run_frames` skips breakpoints and runs on) |
| the table | `analyze_performance {"action":"section_timer"}`: count, min, avg, max, net max, interrupted per section, and the pass it stopped at; `"limit":100` adds the passes |
| clean up | `analyze_performance {"action":"section_timer","op":"stop"}` |

Or ask the agent to run `profile_sections.py --native` (the same timer through the WebAPI) and to read the report it
writes:

> Run `python3 data/examples/01-section-timing/profile_sections.py --native --frames 200` for 48K and for PENTAGON and compare
> the two reports.
