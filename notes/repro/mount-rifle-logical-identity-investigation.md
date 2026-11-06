# Mount & Rifle: intermediate identity validation evidence

These are intermediate diagnostic results from exact game commit `349c35a`,
not final performance claims. Final runs are in
[mount-rifle-logical-identity-20261106-summary.json](mount-rifle-logical-identity-20261106-summary.json).
All tools/runtimes were optimized builds made in this engine checkout, with
`TIDE_HIDDEN=1`, two players, latency 40, ARMY, 2100 frames and 1280x720.

Before canonical chunk preparation, the client averaged 140.35 ms match work per
frame, ran 6361 ticks over 2100 frames and took 372.70 wall seconds. The profile's
FallOnField averaged 19.094 ms per call even though battle bots immediately return
from that system. Logical key order produced many one-row physical runs; each
run began/ended 88 mutable Trooper lanes. A shared 1000-row chunk was copied and
compared repeatedly: approximately 352 MB copied and 352 MB compared per system
per tick. Preparation now copies/publishes each writable chunk once, reads current
pages in logical order and finalizes once, restoring unchanged old pages/hashes.
The final frame 900 capture reduces FallOnField to 0.33 ms, but the complete public
run still averages 123.90 ms match work per frame with 15977 ticks: fixing frame
parsing restores more prediction/replay work. This is not an overall CPU win.

During Entity128 migration validation, the first post-startup mismatch at endpoint
413 had 7092512 permitted bytes on the server versus 7092630 on the client. At byte 8,
the public identity clock was 414 versus 413: the client had not simulated the tick.
FRAME_DESTROYED wrote both words with varint64, while the authoritative-frame
skip loop and journal replay skip loop still read varint32. A high lineage made
the frame unreadable before run_tick. Both readers now consume varint64.
`visibility_full_entity_destroyed_frames_verify_and_replay` checks a destruction
with both words above 32 bits, continued verification without a repair, and exact
journal replay. It passes Windows ASan and WASI; all 16 visibility tests pass on
both. This migration bug was caught and fixed before delivery; it is distinct
from the original Entity8 allocation divergence.

The final public run retains three startup repairs through 20 simulation seconds,
then reaches 26 at 34 seconds and 28 at 35 seconds. A second bounded trace locates
the first later difference at endpoint 1301: both permitted projections are
7,093,074 bytes and both clocks are 1302. A complete typed decode finds exactly
one logical difference: public `Campaign.Works.stock` on Entity
`0000000000000020:5edd30a7f97041ed`, server 0 versus client 18.41666603088379,
four bytes at offset 6,397,623. This is Brevno ammo depot. Its hauler
`0000000000000022:0edeff6142131891` occurs only as the Works reference in each
projection; neither contains a visible Party row for it. No identity, clock or
physics value differs.

`campaign/economy.tide:145–154` creates its hauler convoy in Audience.None.
When that hidden Party reaches the works, `Haul` at lines 198–212 takes
`min(stock,24)` loads and updates the public Works. Its client lacks the hidden
convoy, so cannot execute that pickup; all 18.416666 loads fit and the server's
stock becomes zero. This is a legitimate public consequence of hidden campaign
simulation, requiring an authoritative update. The public battle does not make
the accompanying campaign fully public. The game can keep that authority-driven
update or revisit which economy data it publishes; sending the private convoy
merely to eliminate its correction would change the game's visibility policy.
Raw permitted-world dumps and full profiles stay in ignored scratch directories
and are not committed.
