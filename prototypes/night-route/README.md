# NIGHT ROUTE

A delivery strategy prototype for one player, built entirely in Tide. One
neighbourhood, four parcel behaviours, and three minutes until dawn.

Choose one to three parcels at the depot. The matching numbered addresses
appear on the map. Move with WASD or the arrows; hold Shift to face north
while moving more slowly sideways or backwards. Hold E near an address to
deliver, or at the depot to open the next manifest. Touch has a directional
pad, north-facing toggle and handoff button. A gamepad uses its left stick,
left shoulder and south button.

Mothbox needs lamplight, Restless needs actual movement, Northbound needs
north-facing travel, and Echo dislikes revisited streets, including U-turns.
Each parcel has its own strain bar: obedience removes eight points a second;
100 spoils it and costs $10. Delivery immediately removes that parcel's rule.
Light footprints and purple route history are visible on the map. H or Rules
opens the guide; the shift continues while it is open.

Deliver every selected parcel to earn the bundle bonus: +35% for two or +70%
for three. Base pay arrives at each handoff; bonus pay arrives at the final
successful handoff. Spoiling or abandoning one loses that trip's bonus.
Return to the depot for new destinations and payouts. The first departure
starts the clock; subsequent depot planning uses shift time.

At dawn, compare earnings, deliveries, losses, bonus pay and violation time
for each rule. Retry repeats the same jobs; Next Night changes their seed.
There is no saved progress or online multiplayer in this slice.

Build with `TIDE_PRIVATE_PROTOTYPES=ON`: native targets `night_route` and
`night_route_tests`; web targets `night_route_page` and `night_route_tests`.
The page stages at `docs/static/prototypes/night-route/index.html`. Serve
`docs/static` and open `/prototypes/night-route/`.
