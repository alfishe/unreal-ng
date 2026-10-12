# The same through MCP: what to ask an AI agent

Connecting an agent: [../01-section-timing/prompt.md](../01-section-timing/prompt.md).

## Ask for it

> Load `data/examples/02-constant-time/constant.sna` into a new Pentagon with the labels from `constant.sym`, mute it
> and run 10 frames. Every pass from `Dispatch` to `Dispatch.End` must take exactly 354 T-states. Check 20 passes:
> for every pass, tell me IY at `Dispatch` (which handler it is: H0..H7), A' and B, and the T-states it took. Which
> passes are not 354, and what do they have in common?

> Run `python3 data/examples/02-constant-time/check.py` and explain the report: which handler is wrong and which
> branch of the prologue is wrong? Then show me the lines in `constant.asm` that cause it.

The agent gets there with these tools:

| Step | Tool call |
|:--|:--|
| markers | `control_execution {"action":"bp_add","address":"Dispatch"}`, the same for `Dispatch.End` |
| run to the next marker | `control_execution {"action":"step_n","count":100000}` |
| time and registers there | `inspect_state {"aspects":["snapshot","registers"]}`: `frame F t T of 71680 (global t G)`, and IY, AF', BC in the registers |
| the handler IY points at | `manage_symbols {"action":"resolve","address":"0x8102"}` → `H5` |

A pass takes the difference of the global times at `Dispatch` and at `Dispatch.End`. A few dozen passes suit the
agent's own tool calls; for hundreds, have it run `check.py`.
