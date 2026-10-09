# Cabin Nine: private prototype design

**Working premise:** two human cabin crew, eight passengers, one late charter flight, and an extra person after the lights fail. The crew must agree which familiar person is an imitation while keeping the real passengers together. The goal is a small complete horror experience that players want to tell someone about, with a 45-minute first-play target inside a 30–60 minute range.

This is a design target. The first implementation establishes the complete four-case route, but its duration, fear, difficulty and replay value have not been validated by a blind two-person playtest. Planned behavior below is distinguished from the current implementation at the end.

## Why this flight

An aircraft gives the mystery a beginning, an unavoidable middle and a visible ending: boarding, the long darkness between airports, then approach lights. Two crew have a believable reason to speak to everyone, move between both ends of a small cabin and maintain records. A small, older charter aircraft makes eight passengers and two crew feel intimate rather than anonymous.

The cabin should feel inhabited before it feels haunted. Worn plastic, seatback pockets, paper forms, a repaired cap and the low engine drone carry more of the identity than constant distortion. Warm service lighting gives way to emergency amber, with the outside remaining almost completely black. First-person silhouettes and repeated faces create the horror; excessive camera wobble and unreadable dark corners would interfere with the actual game.

The two players remain human partners for the entire session. There is no secret traitor crew role. At two players that role would remove the shared inference and turn every useful action into a reason to distrust the only other person who can help.

## What players do

The repeating loop is **meet someone, establish a baseline, notice a contradiction, reconstruct the change together, isolate the correct copy, compare the account**. The passengers have small needs and conversations so that inspecting them is a human interaction rather than a row of database entries.

Neither player has a permanent clerk or guard job. They can both walk the entire cabin, speak to every passenger and use either record station. A pair can split the work, swap midway, or investigate the same person together. At least two pieces of their work must meet in the final conclusion: a record of the earlier state and a reliable account of what changed later, checked against the person actually standing there.

In the fuller experience, one player might keep uninterrupted sight of a passenger during a light failure while the other retrieves a prior photograph; on the next incident they reverse those jobs. Cabin service and simple restoration tasks produce reasons to move and opportunities to miss a movement. Those tasks must stay short and comprehensible. The horror game cannot collapse into one player reading pages while the other waits by a switch.

The first slice uses two physical evidence stations and requires each player to collect a record before both can confirm an accusation. That enforces participation, but it is not yet the richer simultaneous fieldwork described above. Both players can see the collected notebook evidence; the prototype does not depend on inaccessible private UI or voice chat. Voice helps, but clear text, names and observable details must remain sufficient.

## The deduction contract

The player should be able to explain a verdict in an ordinary sentence: “Both know the first repair, but this one is wearing its old state; the other has the change witnessed later.” A delayed answer, a tremor, unusual food, age, grief or unfamiliar behavior never proves that someone is a creature.

The threat takes snapshots of people. A snapshot includes appearance and the things being worn at that moment. It can learn words and imitate helpful behavior after arrival, but learning a description does not retroactively update an earlier physical snapshot. It can hide a tell, move while unwatched or give a better answer in a later conversation. It cannot rewrite a saved photograph, undo a witnessed injury, or change what was already established about an original human.

The eight genuine passengers are established during boarding. Detecting two matching faces only proves duplication; it does not say which person is the original. The decisive question is which one carries the later, independently witnessed continuity of the person who boarded. Seating is not a trusted identity source. All actors change locations between investigations, and a familiar passenger may legitimately move or lend an ordinary object.

Every authored case has an accessible route to certainty using the baseline, a later witness and current physical detail. Sources state what they observed and when. A record must never quietly become corrupted just because the puzzle needs another surprise. An uncertain witness is explicitly uncertain, and an ordinary lie cannot be promoted into supernatural proof. The first slice uses fixed, truthful case records and does not simulate witness uncertainty.

There is no universal “look into a mirror”, pulse check, key phrase, red eye or safe scanner. The particular change matters. The prototype's four appearance comparisons are intentionally modest: they prove the basic loop works, but they must be supplemented with movement continuity, custody of an object and situated observation before the full experience can rely on this contract for 45 minutes.

Inspection provides a close visual description rather than a result. It should tell the player which wrist carries a dressing, not say “this is fake”. A player who noticed the physical difference without opening the inspection text should have made the same deduction. Color is always accompanied by position, count or shape so that hue recognition alone is not required.

## The people who boarded

| ID | Passenger | What first makes them a person | Visual identity | Innocent oddity |
|---|---|---|---|---|
| 0 | Rowan Hale | Going home to mend his daughter's toy; carries its small screwdriver | Navy jacket, short dark hair | Talks impatiently after an accidental wrist scrape |
| 1 | Ada Finch | Former nurse who is less comfortable flying than helping someone else | Rust cardigan, grey hair, pinned watch | Counts breaths and rubs out crossword answers |
| 2 | Evelyn Park | Art teacher who calms herself by making things from paper | Teal sweater, dark bob | Turns the cabin count into a little craft activity |
| 3 | Gabriel Moss | Musician visiting a brother after a difficult separation | Olive jacket, dark cap | Misses words over engine noise and asks the crew to face him |
| 4 | Nisha Rao | Bringing her sister's forgotten phone home | Plum coat, dark hair, marker in pocket | Checks two phones repeatedly |
| 5 | Ivo Bell | Taking his first journey since his wife died | Mustard sweater, round glasses | Still says “we” when he talks about travelling |
| 6 | Daniel Voss | Rehearsing how to tell his family that he lost his job | Charcoal suit, red tie, folded envelope | Dodges questions about work and keeps retightening his tie |
| 7 | Leila Soto | Returning a bread starter her father kept alive for thirty years | Cream jacket, auburn hair, cloth-wrapped jar | Protects a jar containing something visibly alive |

Nobody is filler whose only purpose is to supply a suspicious gesture. In the full demo, at least one small need from each passenger is answered during boarding and a later line recalls it. The ending should pay off two or three of these details: the toy still needing repair, the bread starter reaching home, and Ivo allowing someone to help him with the next journey.

## Four concrete cases

The prototype's internal actor 8 is the extra person, but no player-facing label identifies that actor. Its copied face changes and all actors are reassigned to seats. Correctness is established through evidence, not by selecting a ninth chair. This table is an authoring aid and contains spoilers.

| Case | Earlier trustworthy state | Later independent observation | What the two apparent people carry now | What it teaches |
|---|---|---|---|---|
| The Second Rowan | Boarding photograph: blue dressing on Rowan's **left** wrist; right wrist unhurt | Ada helped replace the left dressing after a new scrape to the **right** wrist, and retained the discarded dressing | Human: fresh right dressing and scrape. Copy: old left dressing, right wrist uninjured | The right name and a real-looking dressing are not enough; the timeline identifies the original |
| A Better Answer | Gabriel's old cap has **one** red strip | Leila repaired another split with **two separate** red strips and kept the removed strip on the repair card | Human: two red strips. Copy: one, while giving a confident answer about the repair | A creature can learn the question without carrying the last change; hearing difficulty remains an innocent behavior |
| Something It Missed | Photograph of Ivo's fresh white glasses-bridge repair, taken before the fit check | Nisha added a **black diagonal** line after Ivo chose the fit and remained with him through the flicker | Human: white repair with black line. Copy: plain white repair | Matching a major feature does not match the final state; source timestamps matter |
| The Last Count | Evelyn's first white triangle tag has a small **square** hole | Leila punched a larger **round** hole around it after the string tore, retained the offcut and retied the tag | Human: round hole. Copy: earlier square hole; both have the triangle on the left collar | The final copy knows useful details and asks to join the safe count; an old accurate record cannot replace a later one |

These changes take place aboard rather than being personality tests. The old state is a real state: the wrong person is not simply drawn without an arbitrary accessory. In the current slice the service incidents are authored testimony and inspection details; the player does not yet perform each repair or witness each event as an animated scene.

The threat returns in successive manifestations of the same interference, rather than four unrelated monsters. A correct early case stops that manifestation's access attempt and establishes a rule; it does not secretly erase the victory. The eventual staging must show a clear transition between a secured copy, the eight-person recount, and a new intrusion. The current phase transition expresses this through announcements and a renewed outage. Autonomous escapes from a supposedly secure room are not implemented.

## Containment, mistakes and stopping the flight

The first action is humane, reversible separation in the rear crew compartment. The first slice has one holding space, explicitly shown as a solid service compartment in this fictional charter layout. Escort and release never reveal identity automatically. An accusation needs a local interview, an inspection, both records, contributions from both crew and both signatures.

A wrong final accusation releases that human, marks them as cleared for this case, and spends one of three cabin-trust points. The first two mistakes let the team reassess without replaying boarding. The third ends the investigation because the crew have repeatedly detained innocent people. The report states what happened and restart remains available. There is no passenger execution button, sacrificial trial-and-error test or long spectator punishment for a player.

Holding everyone is impossible in the current single-person holding space. Keeping everyone seated is sensible cabin safety and should not be punished; it simply does not establish which face is the intruder. Brute-force accusations fail the trust objective before the player can process eight humans. Confirmations, rather than ordinary conversations or reversible holds, spend trust.

The airplane introduces a serious design risk: “land, keep everyone on board, and let authorities investigate” is a rational solution. The full demo must make the immediate threat concrete before escalating past it. The proposed final intrusion is an attempt to reach the cockpit during approach; a temporary hold buys time to complete the evidence and stop that access. Waiting, seating everyone or making an unproved accusation cannot complete the crew's immediate job. The prototype currently records a final outcome; it does not simulate a cockpit breach or physical landing. Do not imply those behaviors are already solved.

A locked room also cannot be used as a universal supernatural test. The door, lighting or instrument panel must not conveniently announce whether its occupant is a creature. Any fuller escape or rescue mechanics need observation and physical causality, with no scripted breakout that wipes out a correct deduction.

## Pacing hypothesis for the complete demo

| First blind play | Intended beat | Why the time is spent |
|---|---|---|
| 0–7 minutes | Boarding and quiet service | Meet the eight people, learn movement and create memorable baseline interactions |
| 7–15 minutes | First flicker and the second Rowan | Notice the changed count, learn the two-source comparison and complete a safe first containment |
| 15–24 minutes | Gabriel's confident imitation | Work through a repair witnessed at the opposite end; introduce an innocent suspicious behavior and a useful way to clear it |
| 24–33 minutes | Ivo and the unfinished record | A short power-restoration task separates the crew while one protects continuity; compare the photograph with its later check |
| 33–43 minutes | Evelyn and approach | Reuse learned rules under bounded pressure, protect the originals and stop the final access attempt |
| 43–47 minutes | Quiet landing account | Release held humans, hear personal payoffs and review the team's choices together |

Forty-five minutes is a hypothesis, not four mandatory ten-minute timers. Experienced players may finish sooner; searching a cupboard or waiting for an announcement should not pad the run. If blind pairs solve all four cases in fifteen minutes, the solution is to add meaningful firsthand observation and service situations, not slow their walking speed or increase blackout waits.

Four major investigations are the content spine. Each needs a short ordinary setup, a changing situation, a fair proof route, a recoverable mistake and a quiet consequence. The supporting service conversations are brief. There is one main cabin, two galleys and a rear compartment rather than multiple maps, combat arenas, crafting systems or a campaign.

Replay value comes first from discovering details a team missed. Later, two or three authored variants can alter which evidence changes, when it changed and which human it concerns. Changing only the seat positions or the face on an identical missing-sticker puzzle will not create meaningful replayability. A procedural rule generator should wait until the authored cases consistently work.

## Presentation and usability

Use low-resolution rendering, sparse geometry, flatish light, restrained dithering and analog UI materials. Faces and witness objects must remain readable. The UI should remain crisp enough to compare evidence even when the world is rendered at a lower resolution.

The strongest sound beats are a familiar engine drone stopping for a fraction too long, an intercom beginning before its button is pressed, and a voice at the other end of a cabin that contains only one apparent speaker. These need original audio and reliable subtitles. A sudden sound is punctuation rather than the primary threat.

No proof can depend on hearing an uncaptioned whisper, recognizing a particular color, tiny text, or recalling the exact sentence from twenty minutes ago. Collected source text belongs in the notebook with the sequence of observations. Both crew can inspect it. Mouse capture is released for readable UI. The fuller build should add sensitivity controls, readable contrast settings and subtitle options before claiming polish.

## What the first implementation proves

The current build supplies two human crew, networking through the established room package, first-person movement, recurring procedural passenger meshes, a low-resolution aircraft presentation, original ambient sound, boarding dialogue, four fixed evidence cases, inspected physical details, a shared notebook, two-player agreement, one reversible holding space, trust-based recovery, final outcome and restart. Baseline dialogue avoids describing events that have not yet happened.

It does **not** yet prove the 30–60 minute experience, autonomous interdependent fieldwork, a threat that listens to conversation, fully acted service incidents, dynamic witness ambiguity, stalking/chase behavior or a physical landing/extraction. The current copied dialogue is authored per case. Distinctive geometry and narrated inspection support the physical differences, but finished character animation and close-up cinematics are outside this slice.

The next evidence of quality is a blind two-player run with timestamps: first suspect, first shared conclusion, first wrong hold, each case resolution, and ending. Ask the players afterwards to explain each verdict and name two passengers they cared about. If they cannot explain a verdict, fix the clue contract; if one player did all the thinking, fix the fieldwork; if nobody matters, fix the boarding interactions. A native or web smoke test establishes runtime correctness, not those experiential outcomes.
