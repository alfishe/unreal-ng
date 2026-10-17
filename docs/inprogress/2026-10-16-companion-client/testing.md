# Companion clients: testing

Part of [README.md](README.md). Acceptance criteria: [requirements.md §7](requirements.md#7-acceptance).

## 1. Unit tests (`core-tests`)

`core/tests/emulator/execution/companionregistry_test.cpp`, `CompanionRegistry_Test`:

| Test | Checks |
|:--|:--|
| `APairingIsApprovedAndTheTokenIsDeliveredOnce` | request → pending → approve → poll returns the token, the next poll `unknown`; the token authenticates |
| `ADeniedPairingSaysSoAndIssuesNothing` | deny → `denied`; nothing authenticates |
| `APairingExpiresAfterItsTime` | injected clock: 121 s → `expired`, approve fails |
| `PairingIsRefusedWithoutAnApprover` | `SetApprovalAvailable(false)` → `pairing_unavailable` |
| `PendingPairingsAreLimitedInTotalAndPerPeer` | the fifth and a second from one peer → `too_many_pairings` |
| `ApprovalMayNarrowTheScopes` | approve with fewer scopes → the companion has those |
| `ARevokedTokenNoLongerAuthenticates` | by id, by name, all |
| `TheStoreKeepsDigestsAndSurvivesARestart` | the file holds no token, a second registry loads it and authenticates |
| `ABrokenStoreIsSetAsideAndTheRegistryStartsEmpty` | `companions.json.bad`, empty list |
| `TheEnvironmentProvisionsCompanionsWithScopes` | `UNREAL_COMPANION_TOKENS` parsing: scopes, `all`, defaults, short tokens and duplicates skipped |
| `EveryChangeIsAnnounced` | `NC_COMPANION_CHANGED` per event |
| `NamesAreCleanedAndScopeNamesRoundTrip` | control characters, the 64-byte limit on a whole UTF-8 character; names ↔ bits, unknown names refused |

`CompanionCheckAction_Test.ScopeSessionAndPauseDecide`: the decision the WebAPI and the CLI ask - a granted scope
passes (debugger actions too) while nothing holds the machine; a missing scope before a held session; a step while
running `not_now`, paused allowed; the take-back hint.

`CompanionActions_Test.AvailabilityFollowsTheMachinesState`: the catalogue of `me` - running (pause yes, resume and
step no, memory yes, rewind no without history), paused with history, held (only take-back and pause), stopped.

`RunControlClaim_Test.ATakeBackNamesWhoTookIt`: `RevokeRunControl("companion 'X'")` revokes as the GUI's take-back
and announces `by`.

`core/tests/emulator/execution/automationgate_test.cpp`, new cases:

| Test | Checks |
|:--|:--|
| `CompanionScopesCoverTheUserActions` | each row of the catalogue (design §3) maps to its scope; the take-back routes change no machine |
| `EveryOtherActionIsDebugOrConfiguration` | memory, registers, ports, breakpoints, steps, runs → `debug`; other settings and features, start / stop, network config → `machine` |
| `SteppingNeedsAPause` | steps, direct runs, register writes, TTD instruction steps need a pause; memory writes, breakpoints, seeks do not (HTTP and CLI) |
| `CompanionCliScopesFollowTheCommandForms` | `tape play` → media, `setting speed 2` → run, `setting trdos_traps 1` → machine, `disk write` → debug |

`core/tests/automation/webapi/companion-routes_test.cpp`, `CompanionRoutes_Test.EveryChangingRouteHasAScopeAndTheDebugOnesAreListed`:
every route of `emulator_api.h` that `ChangesMachine` has a scope, and the ones in `debug` are exactly the test's list.
A new route that falls to `debug` fails the test until someone decides; a listed route that is gone or moved fails it too.

`core/tests/automation/mcp-tools-test.cpp`, `McpTools_Test.CompanionManage_ListsAndRevokes` (the fake WebAPI caller):
the text table, revoke by name (URL-encoded), revoke all, a revoke without an id refused.
`McpDispatcher_Test.ToolsList_ContainsAllTwentyTools` lists `companion_manage`.

The CLI processor is not linked into `core-tests`; its companion gate is `CheckAction` (above) plus the scope table,
and the live script drives the CLI itself.

`companion-event-hub_test.cpp` (`CompanionEventHub_Test`): the handshake token gets `hello`; only a changed instance is
sent, a new one too, a closed one as `emulator_gone`; authentication by message, nothing before it; an unknown token
told and closed; a revoked companion told once and closed; a notification refreshes.

`PairingTurnedOffRefusesRequestsAndDeniesTheWaitingOnes` (`CompanionRegistry_Test`): off → the waiting request is
`denied` (an event for its prompt), a new one `pairing_disabled`, `pairing_allowed: false` in the report, paired
companions still authenticate; on again → requests are accepted.

## 2. Qt tests (`unreal-qt-tests`)

`unreal-qt/tests/qt/companioncontroller_test.cpp`, `CompanionController_Test`:

| Test | Checks |
|:--|:--|
| `ARequestOpensAPromptAndAllowPairsWithTheTickedScopes` | `pairing_requested` → the prompt with the code and the asked scopes ticked; untick one, Allow → the companion has the rest; the prompt goes |
| `DenyRefusesAndACancelledRequestTakesItsPromptAway` | Deny → `denied`; a cancelled request closes its prompt without a decision |
| `TheCompanionsDialogListsAndRevokes` | the rows, Revoke on the selected one: its token stops, the other stays |
| `UntickingAcceptPairingRefusesNewRequestsAndDeniesTheWaitingOnes` | on by default; untick → the signal, the waiting request `denied` and its prompt gone, a new one `pairing_disabled`; tick → accepted again |

## 3. Live verification

`tools/verification/mcp/test_companion.py`, run by `run_all.py --launch <unreal-qt>`. `mcplib.launch` sets a
verification companion (`verify`, every scope) and a narrow one (`narrow`, `run` only) in `UNREAL_COMPANION_TOKENS`.

| Step | Plane | Expect |
|:--|:--|:--|
| list | MCP `companion_manage list` | the verification companion, `source: environment` |
| act | WebAPI with the token: resume, keyboard tap, reset | 200; `GET control/session` → `held: false` after each |
| debugger | WebAPI memory write; a step while running; `me`; pause; the step | 200; 409 `not_now`; `step` unavailable "pause first"; available; 200 |
| not granted | the `narrow` companion (run only) ejects a disk; its `me` | 403 `companion_forbidden`, `required_scope: media`; `disk` unavailable |
| baseline | WebAPI resume without the token | the implicit session opens (the gate is unchanged) |
| held | MCP `take_control`; WebAPI resume with the token | 409 `held_by_session`, holder `mcp` |
| take back | WebAPI `control/take-back` with the token | 200; the MCP agent's next call is the stop error |
| CLI | `companion auth`, `resume`, `memory write`, `step`, `companion`, `session take` | ok, ok, ok, `not_now`, "Available now" with the reasons, `companion_session` |
| agents | WebAPI with the token and `X-Unreal-Source: mcp` | 403 `companion_not_for_agents` |
| me | `GET /api/v1/companions/me` | the companion, the instance, `can_act` |
| events | WebSocket with the token; resume; MCP `take_control`; take-back; `snapshot` | `hello` (paused, step lit); an `emulator` entry with step dark ("pause first") within 0.9 s; `held` with `can_take_back`; not held; `snapshot` |
| events auth | WebSocket without a header: `snapshot`, then `auth`; another with a wrong token | `companion_required`; `hello`; `companion_unknown` and closed |
| revoke | MCP `companion_manage revoke` (an events channel open) | the token then gets 401; the channel gets `revoked` and closes |

The script checks `content[]` text as well as `structuredContent` (MCP testing guidelines).

## 4. Manual checks with the GUI

1. Start unreal-qt. `POST /api/v1/companions/pairings`; the prompt shows; Allow.
2. Collect the token; resume, type, insert a disk with it: no bar, the keyboard keeps working at the GUI.
3. Take control with MCP; the bar shows; the companion is refused; take back from the companion: the bar goes, the
   toast says who took control back.
4. Tools → Companions…: the companion is listed; Revoke; its next request is 401.
5. Restart: the store brings the companion back.
