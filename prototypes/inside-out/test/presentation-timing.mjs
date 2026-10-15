// Public presentation contract, in 30 Hz match ticks. Animated probes retain
// 30 fps frames; clock-only waits may render at 10 fps while advancing real ticks.
export const CLOSE_TICKS = 36;
export const NAME_FADE_TICKS = 12;
export const NAME_HOLD_TICKS = 90;
export const INTRO_TICKS = NAME_FADE_TICKS * 2 + NAME_HOLD_TICKS;
export const SOLVE_TICKS = CLOSE_TICKS + INTRO_TICKS;
export const REVEAL_TICKS = 36;
export const MANUAL_SETTLE_TICKS = INTRO_TICKS + REVEAL_TICKS;
export const AUTO_SETTLE_TICKS = SOLVE_TICKS + REVEAL_TICKS;
export const ANIMATED_FRAME_MS = 1000 / 30;
export const STATIC_FRAME_MS = 100;
