# The Last Light

An original two-player horror prototype built in Tide. Two attendants
sit at a ritual table in a shuttered sanatorium. Eighteen names have been
erased from its records. Remember them together, and the house will let you
leave.

The Pilgrim takes the exposed turn in the corridor. The Keeper stays at the
board, listens for the Pilgrim's symbol and seals its threats. Returning banks
names and swaps roles. Lingering conserves candlelight, but risks everything
still carried. Three acts lead to an escape that requires both players.

The complete ritual has approximately 11–12 minutes of successful active
play in automated runs, plus planning, conversation and two act reveals.
15–25 minutes is a first-session tuning target, still awaiting human playtests.

## Play

Build from the repository root:

```powershell
cmake --preset debug
cmake --build --preset debug --target last_light
```

Each person launches `build/debug/bin/last_light.exe`. One selects **Host a
room**, the other **Join a room**, then enters its code. Two real players are
required; there is no solo bot. Desktop and browser clients can share a match.
Use voice chat or sit within speaking distance; voice chat is not built into
this prototype.

With an installed Tide, `tide run prototypes/the-last-light` runs the folder.
The browser build is staged by `cmake --build --preset web-debug
--target private_games` at `docs/static/prototypes/the-last-light/index.html`.
After a website deployment, anyone can open and share
[tide-engine.dev/prototypes/the-last-light/](https://tide-engine.dev/prototypes/the-last-light/)
without admin sign-in. The **Admin → Prototypes** catalog still requires
sign-in and also lists Two Forks and Cabin Nine.

| Role / moment | Controls |
|---|---|
| Planning, active player | **1** Walk, **2** Shortcut; click/tap also works |
| Pilgrim | Hold **E** or the offering button; call **Sun**, **Key** or **Eye** to the Keeper |
| Normal threat | Release the offering when it approaches |
| Well threat | Keep holding the offering when it approaches |
| Keeper | Hold **1** Sun, **2** Key or **3** Eye through the strike; three touch buttons provide the same actions |
| Result, active player | **1** Return and bank; **2** Linger when allowed |
| Final escape | Both hold **E** or the offering button for eight seconds |
| Rules | **H** / **?**, available outside dangerous phases |
| Room / volume | **Escape** / Menu; Settings controls local volume |

Every offering needs one Keeper seal. The first seal or a rescue costs three
wax; acknowledging a safely dodged later threat is free. Banking restores
four wax, even after an unsuccessful offering. A landed threat injures only
the Pilgrim, costs six candlelight and discards unbanked names. Each player
starts with three nerve. The Well reverses the dodge rule; the final act's
Belfry answers with a second symbol. Read the room instruction before each
offering.

If someone disconnects, the ritual freezes. When a partner returns, an
interrupted approach starts afresh so reconnecting cannot cause an immediate
strike. An open menu releases that player's input; it is not a shared pause.

## Development

The room uses procedural low-poly meshes, vertex snapping, a 270-line render
target, point-sampled materials, color quantization and dithering. HUD text
stays legible. All sounds are original arithmetic-generated WAVs, reproducible
with `python prototypes/the-last-light/sfx/make_sounds.py`.

`last_light_tests` exercises actual generated commands, input, deterministic
worlds, complete bank/linger runs and two network clients under lag, jitter
and loss. WebGL and WebGPU tests use two real browser clients through a local
relay, with keyboard/touch controls and desktop/phone captures.

The researched design, primary sources and remaining human-playtest questions
are in [DESIGN.md](DESIGN.md). Fear, audio feel, first-time comprehension and
human session length remain subjective playtest work; automated checks do not
establish them.
