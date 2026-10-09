# The Last Light

Private prototype design and research, 9 October 2026.

The intended experience is two people trying to keep each other alive at a haunted table. The player whose turn it is becomes the Pilgrim and accepts the immediate danger; the other becomes the Keeper and watches for the moment to intervene. Both win or lose together. The important exchange is: **"I can get one more. Do you have enough light to bring me back?"**

This is an original premise and ruleset. The references below inform design principles; their stories, characters, art, maps and exact rule systems are not used. The prototype stays under `prototypes/`, outside the public examples and documentation. A private build should not be presented as a released Tide demo.

## Premise and scope

Two night attendants take the final shift in a closed sanatorium. In its last lit room, an old ward board has twelve spaces, a brass die, three engraved ward signs and an empty third chair. When the die moves the shared pawn, the room changes. The board seems to describe the building, until recovered fragments reveal that it has been recording the people who watch it.

Collect eighteen fragments, six per act, and complete a final vigil to open the way out. The slice has one carefully composed room, one recurring presence and three acts. The room changes around the same table, so familiarity itself becomes unreliable. Each act needs a new observation, a new constraint and a reveal; simply increasing the speed of an identical test would not sustain a complete demo.

Two connected players are the intended mode. Roles rotate after a banked expedition or a collapse. A debug partner can help development and capture, but an automated partner does not establish that the cooperative design works.

## Research and what it changes here

| Primary source | Relevant finding | Design consequence |
|---|---|---|
| Thomas Grip, [9 Years, 9 Lessons on Horror](https://frictionalgames.com/2019-10-9-years-9-lessons-on-horror/) (2019) | Grip argues for anticipation, an established player role, uncertain voluntary choices and restraint with scares. He describes reducing scares in an Amnesia area and allowing more mystery to improve its effect. | Let the Pilgrim choose whether to linger. Establish why the attendants must finish their shift. Leave quiet gaps and give the room a history. This is a design inference, not evidence that this prototype is already frightening. |
| Frictional Games, [Rebirth: consequences](https://frictionalgames.com/2021-03-amnesia-rebirth-five-months-later-and-soma-sells-1m-copies/) (2021) | The studio describes the tension between meaningful failure and repeated unsuccessful attempts turning fear into frustration. It also identifies the need to make the consequence legible. | Losing carried fragments and candle feels consequential; losing all banked progress on each strike would invite repetition. A collapse must visibly alter the run and say what was lost. |
| Thomas Grip, [Thoughts on Alien: Isolation and Horror Simulation](https://frictionalgames.com/2014-10-thoughts-on-alien-isolation-and-horror-simulation/) (2014) | Grip distinguishes horror actions that players perform from conventional gameplay with a frightening presentation. He proposes concern about negatively affecting the world as another source of tension. | Holding a ritual while watching a doorway, deciding to stop, and spending the shared light are the actual play. The board should make the room dangerous, rather than sit beside an unrelated action game. |
| Alistair Hope / Creative Assembly, [Building Fear in Alien: Isolation](https://www.gdcvault.com/play/1021852/Building-Fear-in-Aliens) (GDC 2015) | The session's published overview describes a single threatening creature, an underprepared player and the importance of an immersive visual and audio environment. Only the overview was used here, not an unwatched talk transcript. | Focus the asset budget on one presence, convincing sound tells and the space surrounding the table. Multiple enemy types are unnecessary for the first slice. |
| The Impossible Dream, [Dread Quick Reference](https://www.tiltingatwindmills.net/wp-content/uploads/2014/10/dread_quickstart_letter.pdf) (2004), pp. 1–3 | A player can refuse or abandon a risky pull and fail the attempted task; successful risky actions make the shared tower more precarious. Elective pulls are possible. | Make extra progress something the exposed player elects to risk. Banking is a deliberate safety decision. Do not reproduce Dread's tower or eliminate a cooperative partner for the rest of the session. |
| Steel Crate Games, [Keep Talking: remote play](https://keeptalkinggame.com/how-to-play-remotely/) | The Defuser sees the bomb while Experts consult the manual. The official setup gives collaborators different necessary information and allows roles to rotate. | Pilgrim and Keeper should each own information or decisions the partner lacks. A one-second window is insufficient for a new spoken clue plus a partner's response; preview a sigil early or allow a longer warning window. |
| Z-Man Games, [Pandemic: Hot Zone Europe rules](https://cdn.svc.asmodee.net/production-zman/uploads/2024/10/zm7142__rulebook_web.pdf), p. 6 | Event cards can be played during another player's turn, and their owner chooses how they are used. The rule also specifies the boundary beyond which intervention is too late. | Keeper interventions have a clear window, limited supply and an owner. The Keeper chooses whether to spend protection; Pilgrim controls the exposure. The intervention cannot be a retroactive undo. |
| Puppet Combo, [developer FAQ](https://puppetcombo.com/faq/) | The developer describes deliberately combining retro low-poly games and horror-film influences, rather than strict historical emulation; filters are commonly optional. | Use PSX visual constraints as art direction. Critical clues and controls must remain readable. Heavy distortion is not a substitute for strong composition, sound or pacing. |

## The turn

1. **Agree and commit.** Both players can inspect the board and resources. The Pilgrim rolls the die, then chooses the safer route or a shortcut. A shortcut offers progress or a better destination while raising the next encounter's danger. Show its price before commitment.
2. **Read the room.** Name the room rule in a short diegetic card. Pilgrim reads their omen; Keeper prepares a ward. Start with a few seconds of stillness so players can notice what has changed.
3. **Keep vigil.** Pilgrim holds `E` to make progress while the presence is away. A fragment needs 24 seconds of safe channel time, a minimum of 33 seconds elapsed, and one successful Keeper seal. The usual tell is an instruction to release; the Well explicitly reverses that response and requires holding during the tell. The sigil is available throughout the approach, rather than only at the moment of impact.
4. **Intervene.** Keeper holds the correct `1`, `2` or `3` ward through a strike. The first seal of a vigil costs three wax. Later correct acknowledgements are free when the Pilgrim is safe; shielding an exposed Pilgrim costs three wax and adds a small progress bonus. A safe Pilgrim needs no ward to dodge damage, but the fragment still needs its first seal. Resolve the strike once, then clearly show whether the dodge, ward or hit occurred.
5. **Bank or linger.** A completed ritual adds a carried fragment. Banking preserves carried fragments, restores a small amount of support and rotates the roles. Lingering begins another ritual at a higher threat, up to three rituals in one expedition. Carried fragments remain at risk until banked.
6. **Recover.** One unprotected injury ends the expedition, discards carried fragments and lets the Pilgrim return to pass the turn. Banked fragments persist. Each attendant starts with three nerve; an act transition restores one. Candle exhaustion or an attendant losing their final nerve ends the shared run for both players.

Do not advance ritual progress during a tell. Consume a ward at most once per strike, never once per tick of a held key. The correct room response avoids personal and candle damage even if the Keeper has no wax: release in ordinary rooms, hold in the Well.

The clock can create urgency during exposure, but planning and result screens should give both players time to read. Avoid an enforced wait on a completed task merely to lengthen the demo.

## Keeping the Keeper involved

The Keeper is a second role, not a camera spectator. They own the ward choice and spending decision; the Pilgrim owns the continue/bank decision. The Pilgrim's sigil and the Keeper's threat timing are complementary information. A dominant player may advise, but cannot perform both roles' inputs.

The Keeper's first seal is required for each fragment, so their action is essential even when the Pilgrim dodges perfectly. At zero wax, safe dodges still prevent damage. A return from an unsuccessful result grants four wax even with empty hands, providing a recovery path for the next expedition; make that consequence understandable before players waste a full vigil. A free warning or ping would improve zero-wax participation in a later iteration, without shielding damage or solving the ritual. Test whether observing and advising alone is enough during this rare depleted state.

Prototype information separation is presentation, not a security boundary: Tide currently sends the whole simulation state to clients. Use role-specific views for fair cooperative play; do not claim that clues are protected against inspecting the client.

## Three acts and rule variation

These are the intended teaching and pacing goals. The implemented board uses four recurring landmarks; their actual rules are listed below. The implementation's constants remain the authority for what this build does.

| Act | Mechanical focus | Room change and narrative beat |
|---|---|---|
| I — Attendance, fragments 1–6 | A legible approach teaches safe holding, release and banking. Both can initially see enough to understand a failure. | Rain, a distant corridor and an unused chair. First fragment reads like routine ward paperwork. By fragment six, the roster contains the attendants' names. |
| II — The Watch, fragments 7–12 | Increase dependence on the Keeper's observation. Introduce a quiet room or a warning owned by the Keeper; keep a safe zero-wax response. | The chair has turned toward the table. Reflections or footsteps disagree with the visible room. The ledger refers to the watcher rather than the patient. |
| III — The Witness, fragments 13–18 | The Pilgrim sees a private sigil and must call it; Keeper chooses a ward while watching the approach. Combine known rules, with a clearly signaled faster cadence. | The table's miniature doors seem to match the real doorway. The final entries record when the witness looked away. The third chair is no longer reliably empty. |

| Implemented landmark | Consequence and ritual rule |
|---|---|
| Chapel | Adds six wax on arrival. Uses the normal release response. |
| Gallery | Removes four dread on arrival. Uses the normal release response; its presentation can shift attention toward the partner's warning. |
| Well | Reverses the strike response: hold through its tell. Its title and instruction must clearly distinguish it from a normal vigil. |
| Belfry | Normal response in early acts. In act III, each approach is answered by a second tell using a different sigil. Safe channel gain doubles to compensate for the additional time spent dodging. |

Differentiate landmark rules with a distinct sound, icon and visible instruction. Introduce a new rule in a recoverable encounter before testing it under late-act pressure. A false ambience cue may unnerve players; a false instruction that causes unavoidable damage teaches distrust of the interface instead. A Keeper-only warning in the Gallery is a presentation/design goal and should not be described as implemented unless its view supplies that information split.

The implemented finale asks both attendants to hold together for eight seconds; letting go reduces the shared progress. Its presentation should resolve the immediate goal: the door opens and morning or an exit is visible. An ambiguous final image can leave a hook, but the slice needs an ending the players earned. An exposed finale that combines earlier threat responses is a potential later expansion, rather than a claim about this build.

## Economy checks

The current baseline is candle 100, an expedition cost of 3, a linger cost of 2 and a candle loss of 6 per landed unprotected strike. These are tuning assumptions, not measured completion rates. Eighteen one-fragment expeditions cost 54 candle before injuries, leaving 46. Nine two-fragment expeditions cost 45; six three-fragment expeditions cost 42. Lingering conserves expedition fuel, while increasing dread and risking every carried fragment. Banking protects progress, restores support and gives the partner their exposed turn.

Wax starts at 40, caps at 100 and costs 3 for the first seal or an exposure shield. Returning gives 4, act transitions give 20 and Chapel arrivals give 6. Eighteen one-fragment banks offer 152 wax before Chapels and cap losses; the eighteen required seals consume 54, leaving up to 98 for shielding mistakes. Six three-fragment banks offer 104, leaving up to 50. Frequent banking is therefore the more generous support path, while lingering conserves candle. This is feasible but potentially forgiving: test whether warding removes so much risk that players stop obeying room tells. Reduce starting wax or act refills only after observing that behavior.

Protecting every threat for eighteen vigils is unnecessary. Each completed vigil needs one paid seal; subsequent safe acknowledgements are free. Correct strike responses cost no candle or wax. Extra ward expenditure buys a margin for mistakes or speed. Wrong Keeper choices do not directly damage the Pilgrim on their own; an exposed Pilgrim at an unprotected strike creates the injury. A no-wax pair can return for a refill, although an earned fragment remains impossible until the Keeper can bind its seal.

Avoid automatic candle decay throughout planning or connection stalls. It would punish slower readers and turn discussion into an unaffordable activity. Expedition cost provides finite pressure without charging people for talking.

## Runtime target

The desired first-session arc is **about 20–30 minutes**, with a **15–25-minute initial tuning target** for this first build. Neither range is a tested promise. Vigils have a 33-second minimum and act-dependent limits of 40, 42 and 44 seconds. Eighteen successful vigils therefore supply roughly 10–13 minutes of action; introductions, routing, decisions, banking, act reveals and the finale must earn the rest. Fast experienced players may finish appreciably sooner, and failed expeditions may lengthen a run.

Nine vigils at 20–28 seconds would supply only three to four minutes of action, which is why the design moved to eighteen. Measure duration from first playable instruction to ending, excluding lobby connection time. If sessions are too short, add a meaningful decision or authored encounter, not a longer progress bar.

## Presentation and polish priorities

Use a narrow palette: tarnished amber around the candle, cold green in the ward and black beyond the doorway. Low-resolution room rendering, coarse textures, slight geometry wobble and restrained ordered dithering can suggest PSX imagery; the board, sigils and text need stronger contrast and stable outlines. The light should make hands, die and nearby objects feel tangible.

Audio owns anticipation: rain and electrical hum establish normality; footsteps have distance and direction; the tell is distinct from ordinary ambience; the strike briefly removes room sound. Give warning sounds a corresponding visible cue so the mechanic remains understandable at low volume. Keep loud moments scarce. A presence that is usually implied by space, shadow and changing reflections can be more effective than repeated full-screen attack imagery.

Provide role-specific prompts, an obvious whose-turn indication, the last action result and an explanation of what banking saves. Persistent resources should be understandable as candle, wax and personal nerve before exposing their exact numbers. Controls and game outcomes remain exact even when the lore is mysterious.

## Playtest questions and completion gates

- Can a new pair explain release, ward and bank after their first two turns without outside coaching?
- Does the Pilgrim hesitate before lingering? Does the Keeper feel responsible and useful, including at zero wax?
- Is the ward clue window long enough to speak, understand and respond over a normal online connection? Observe rather than assume it.
- Do players recognize the three rule types, and can they identify what changed between acts?
- Is candle loss usually traceable to a decision or error? Can a competent pair finish all eighteen fragments and the finale without a favorable die sequence?
- Across several seeds, do one-fragment banking and selective two-fragment lingering both remain plausible? Track candle, wax, strikes, bank frequency and completion time.
- Does a collapse cause a tense recovery, or a feeling that the last few minutes must be tediously repeated?
- Are the quiet periods unsettling, or are players waiting for a progress bar? If the latter, change the encounter or shorten it.
- Does each participant get comparable exposed time? One player's skill should not leave the other doing nearly every dangerous turn.
- Can desktop and web peers complete the same run, and can a reconnect or a pause return both to an understandable state?

Local deterministic tests can validate rules, conservation and progression. Visual captures can validate composition and readability. They cannot prove fear, social cooperation, or a 20–30-minute human runtime. A polished demo claim needs at least several complete two-human sessions with recorded observations and the remaining issues resolved.
