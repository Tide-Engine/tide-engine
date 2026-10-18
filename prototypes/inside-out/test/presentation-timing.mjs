// Public presentation contract, in 30 Hz match ticks. Animated probes retain
// 30 fps frames; clock-only waits may render at 10 fps while advancing real ticks.
export const CLOSE_TICKS = 36;
export const NAME_FADE_TICKS = 12;
export const NAME_HOLD_TICKS = 90;
export const INTRO_TICKS = NAME_FADE_TICKS * 2 + NAME_HOLD_TICKS;
export const REVEAL_TICKS = 36;
export const MANUAL_SETTLE_TICKS = INTRO_TICKS + REVEAL_TICKS;
// A solve closes, holds what it did, then the hall opens while the view
// comes back out of the puzzle's model (hall.tide).
export const RETURN_HOLD_TICKS = 60;
export const RETURN_TICKS = CLOSE_TICKS + RETURN_HOLD_TICKS;
export const EMERGE_TICKS = 45;
export const AUTO_SETTLE_TICKS = RETURN_TICKS + EMERGE_TICKS;
export const ANIMATED_FRAME_MS = 1000 / 30;
export const STATIC_FRAME_MS = 100;
