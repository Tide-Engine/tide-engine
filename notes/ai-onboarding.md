# Tide AI onboarding trials

## Materials and method (2026-10-21)

The owner requested game-author onboarding for AI agents, including documentation,
portable skills, project instructions, and fresh-model trials followed by corrections.
The shipped entry is `docs/guide/ai.md`; the compact reference is
`docs/guide/ai-context.md`. Export/download contracts are in `notes/docs-site.md`.

Five fresh T3 delegated sessions used only the supplied prompt, generated overview,
skill and searchable offline user-doc bundle for their game work. No parent conversation
was copied. Project/provider harness instructions still applied: this is an onboarding
smoke study, not a blind model benchmark or a model-quality comparison. Prompts restricted
source lookup to those materials and each trial's generated output/SDK, with no engine
edits, other sample games, networking services, or child delegation. All were permitted
to compile and fix their own code. Initial attempts were retained before corrections.

`gpt-6-luna` used medium reasoning and the standard service tier; the comparison
`claude-sonnet-5-5` used high effort. Both came from the live orchestrator catalog.
The exact common prompt and task briefs are saved in
`notes/ai-onboarding/evaluation-prompt.txt`. The overview snapshots in that folder
are the actual round inputs; later review fixed the published script's frame zero
to frame one. Reports under each trial are the evaluated agent's own account;
the parent corrections below take precedence over unsupported visual claims.

The test SDK was built in this checkout, based on `16dcab23`, version `0.4.0-dev`:
`mingw-release` with `TIDE_EMBED_LLVM=OFF` and `TIDE_EDITOR_EXTENSIONS=OFF`, targets
`tide_cli`, `tidels`, `tide_editor`; plus `web-debug`'s `tide_platform`, installed
into ignored `build/onboarding/sdk`. It uses local LLVM 23.1.2 rather than an embedded
LLVM distribution. No installed Tide or owner configuration was changed. Node 24.21.0
was downloaded into ignored build tools because PATH Node 24.15 predates the Windows
test requirement. Every shell set TIDE_HIDDEN; browsers were muted.

## Tasks

**Collector:** a 640×360 game, 5×3 board, row zero at the top. The player starts
at (0,1), coins at (2,1)/(4,2), arrow edges move one cell within the board,
coins count once, two coins show completion, and R resets everything.

**Bank:** two players with three credits each and a shared zero pool. Each client
has an independent local 1–3 selector and a Deposit button. A typed command transfers
from the authenticated sender; over-balance transfers are rejected. Both clients
show the pool and both balances. No intended implementation was supplied.

## Observations and corrections

| Trial | First attempt | Final artifact and parent findings |
|---|---|---|
| Luna collector, round 1 | Native/web compiled without diagnostics; screen rows were inverted | Agent corrected orientation. Collection, completion and reset work. Its report calls reset.png black; parent reopened that exact file and it clearly shows player (0,1), both coins and zero count. |
| Claude bank, round 1 | Native/web compiled without diagnostics | Replaced cramped slider with explicit 1/2/3 buttons. Independent selectors, deposits, over-balance rejection and both clients' shared displays work. Report incorrectly describes Draw.SetScreen as Y-up; saved source/captures do not support that interpretation. |
| Luna collector, round 2 | Native/web compiled without diagnostics; no source correction needed | Correct orientation from the revised overview. Parent independently checked bounds, revisiting a collected coin, both pickups and reset, including browser behavior. |
| Luna bank, round 2 | Compiled, but assumed PlayerID(1)/PlayerID(2) were the two display slots | Agent switched to join-order wallet seats. A later combined filter typo was fixed from its diagnostic. It claimed the second client's labels were absent; parent reopened final-2.png and saw pool 4 and both balances (0/2), with a separate local selector. No replication defect was demonstrated. |
| Luna bank, round 3 | Used join identities/seats immediately; `for (var row in wallets)` failed | Corrected iteration to the documented `foreach`. Final native/web builds pass. Command logs and two-client captures agree on pool 3, balances 0/3 and an over-balance rejection. The report now correctly distinguishes actual evidence and coverage. |

The overview now explicitly gives world versus screen/GUI axes, device-pointer
conversion, shape centers versus GUI rectangle corners, persistent versus scoped
screen cameras, and `--size` for a target test viewport. It says to retain joined
player identities and use separate UI seats, and repeats the `with` modifier for
each filter. The existing query example already gives `foreach`; a single corrected
syntax error is not a reason to replace it with an exhaustive language manual.

The skill/guide now recommend state-change logs or assertions for numerical behavior
alongside actual image inspection. This addresses two false visual failure reports;
neither was used to justify engine/runtime changes. A review also caught the new
published smoke script's invalid frame zero, fixed it to one, and the parent executed
that exact four-line script successfully with the starter game.

## Independent validation

The parent copied final games into separate ignored check folders and added small,
read-only observation/assertion views. Their inputs and logs are retained under
`notes/ai-onboarding/parent-checks/`; `.tide.txt` files are evidence, not build inputs.

- Collector (round 2): observed `(0,1,0) → (1,1,0) → (2,1,1) → (1,1,1) →
  (2,1,1) → (3,1,1) → (4,1,1) → (4,2,2) → (0,1,0) → (0,0,0)` for x/y/count.
  Repeated entry did not award another coin; left/top bounds and reset held.
- Claude bank: both processes observed pool/balances `0/3,3 → 2/1,3 → 5/1,0 →
  6/0,0`. Over-balance commands changed only the rejection count. Extra test-only
  buttons submitted a non-owner target and an amount of four: both were rejected,
  ending with four total rejections and unchanged money. Assertions checked every
  wallet stayed within 0..3 and the pool plus both balances remained six.
- The collector probe ran the same movement/recollection/reset/bounds checks in
  Chrome 155.0.8059.85 on WebGL 2 and WebGPU, using the existing served test runner
  with Tide.checkGL enabled and software GPU flags. Both emitted
  ONBOARDING_WEB_PASS. Debug.Log goes through the page's printErr, so the probe
  observes both output callbacks; its initial print-only probe was corrected.
- Production docs UI: clipboard text equals the supplied starter prompt; both
  downloads are discoverable; 1280×900 and 390×844 layouts have no horizontal
  overflow, and downloaded-guide/skill paths are correct. Screenshots were opened.

All five final trial artifacts compiled natively and for web. The bank browser
runtime, real UDP/WebRTC, latency/rollback stress, phones, other OSes and sanitizers
were not tested by this study. Simulated --players processes are not real transport
coverage. The snippets are small authored smoke tasks, not proof that agents can
build arbitrary Tide games without supervision.

## Repeat the study

Build a matching local SDK, download the actual skill ZIP/overview/full bundle from
the built docs, and create an otherwise empty ignored trial folder per fresh session.
Supply the saved common prompt plus one task brief, replacing the SDK/trial paths.
Keep the models' first attempts, final code, diagnostic logs, scripts and own reports.
Inspect the code and actual captures independently, then add numerical probes where
visual evidence is inconclusive. Change only guidance supported by the observed
failure, and use a fresh session for a forward check. Keep harness restrictions and
unavailable coverage explicit. Do not put the engine contributor AGENTS.md into a
game or turn the trials into an automatically charged recurring task.
