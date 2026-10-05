// Tide's web runtime. It runs a WebAssembly program built with clang and
// wasi-libc (cmake/wasi-toolchain.cmake) in a page, and gives it:
//
// - what platform/web/tide_web.h declares: the canvas, input and the frame loop;
// - what its renderer draws with (platform/src/gpu.h): WebGPU where the browser
//   has it on a GPU, or else the OpenGL ES 3 functions it calls
//   (platform/src/gl.h), on WebGL 2;
// - the few WASI functions wasi-libc needs: output, the clock, arguments, exit.
//
// The page defines `var Tide = { canvas, print, printErr, arguments, onExit,
// onAbort, checkGL, backend }` (all optional) before this script, and
// TIDE_PROGRAM holds the program as base64 (cmake/web_page.mjs, or the tide
// command). `backend` is 'webgpu' or 'webgl', to draw with that one only; so
// is `backend=` in the page's address. For a page with rooms (below), `relay`
// is another relay's address and `lag` the round trip its player is to have,
// and the page reads `Tide.room`, the code of the room the program is in. For
// a game shown in a frame of another page, `wait` holds the program until that
// page calls `Tide.start()`, and `embedded` leaves that page its scrolling
// (both below).
//
// The lines between the `tide run` markers below are for hot reloading, in the
// page `tide run --web` serves; the pages made to ship leave them out.

(async () => {
    'use strict';
    // `#download` after the page's address: the page is only being downloaded,
    // by a page that will show the game in a frame later. It loads the game's
    // page once in a frame nobody sees, so the frame it shows finds the page
    // in the browser's cache, which keeps a frame's page apart from what a
    // script fetches. Nothing runs. It's after the `#`, which is no part of
    // the address a cache goes by.
    if (location.hash === '#download') return;
    const config = typeof Tide !== 'undefined' ? Tide : {};
    const canvas = config.canvas || document.getElementById('canvas');
    const print = config.print || (text => console.log(text));
    const printErr = config.printErr || (text => console.error(text));

    let memory = null;
    let exports = null;
    let gl = null;        // The canvas's WebGL 2 context, when the program draws with it...
    let device = null;    // ...or the WebGPU device it draws with instead (see WebGPU, below)
    let gpuFormat = null; // ...and what the canvas's pixels are then
    const wantedBackend = config.backend || new URLSearchParams(location.search).get('backend') || '';

    const u8 = () => new Uint8Array(memory.buffer);
    const i32 = () => new Int32Array(memory.buffer);
    const f32 = () => new Float32Array(memory.buffer);
    const view = () => new DataView(memory.buffer);
    const decoder = new TextDecoder();
    const encoder = new TextEncoder();

    // A NUL-terminated string in the program's memory. Copied out, as for
    // every browser function that won't take shared memory: the program's is
    // shared when it runs on threads.
    function string(ptr) {
        const bytes = u8();
        let end = ptr;
        while (bytes[end]) end++;
        return decoder.decode(bytes.slice(ptr, end));
    }

    // Thrown to leave WebAssembly: tide_web_run unwinds main's stack back to the
    // browser, and exit() ends the program.
    const unwind = { tide: 'unwind' };
    class Exit {
        constructor(code) { this.code = code; }
    }

    // Random bytes, as many as asked: the browser gives 64 KiB at a time.
    function randomBytes(count) {
        const bytes = new Uint8Array(count);
        for (let at = 0; at < count; at += 65536) crypto.getRandomValues(bytes.subarray(at, Math.min(count, at + 65536)));
        return bytes;
    }

    let stopped = false;
    let failed = false;             // It stopped for a mistake (fail), which no exit code stands for
    const shaderChecks = new Set(); // A test page's: what WebGPU has yet to say about its shaders (checkShader)
    function finish(code) {
        stopped = true;
        endCursorLock();
        endThreads();
        flush(1);
        flush(2);
        broken(new Error('the program ended before its first frame'));
        // A shader's warning that comes after the program ended still fails a test page
        const end = () => { if (!failed && config.onExit) config.onExit(code); };
        if (shaderChecks.size) Promise.all([...shaderChecks]).then(end);
        else end();
    }

    // -----------------------------------------------------------------------
    // WASI: output, the clock, arguments and exit. There's no file system.

    const EBADF = 8, ENOSYS = 52, ESPIPE = 70, ENOTCAPABLE = 76;
    const pending = { 1: '', 2: '' };
    function flush(fd) {
        if (pending[fd]) (fd === 1 ? print : printErr)(pending[fd]);
        pending[fd] = '';
    }
    function output(fd, text) {
        const lines = (pending[fd] + text).split('\n');
        pending[fd] = lines.pop();
        for (const line of lines) (fd === 1 ? print : printErr)(line);
    }

    // The program's arguments are the page's. `debug-views` in the page's
    // address is --debug-views, as for a desktop game started with it, and
    // `host` and `join=K7QF2M` are --host and --join K7QF2M: a page that shows
    // a game, or a link to one, says which match it's in. The page's own
    // arguments go first, and win.
    const words = [...(config.arguments || [])];
    const address = new URLSearchParams(location.search);
    if (address.has('debug-views') && !words.includes('--debug-views')) {
        words.push('--debug-views');
    }
    if (!words.some(word => word === '--host' || word === '--join' || word === '--connect')) {
        // A code is letters and digits: the program says what's wrong with one that isn't a room's
        const code = (address.get('join') || '').replace(/[^0-9a-z]/gi, '');
        if (code) words.push('--join', code);
        else if (address.has('host')) words.push('--host');
    }
    const args = ['program', ...words].map(a => encoder.encode(a + '\0'));

    // `Tide.wait`, or `wait` in the page's address: the program loads, and
    // starts when the page calls `Tide.start()`. That's for a page that loads
    // a game before it shows it, like one further down that starts once it's
    // scrolled to. With `wait` or without, `Tide.start()` gives a promise of
    // the program's first frame, from which the canvas has something to show;
    // a program that stopped before one breaks it.
    let release = null, shown = null, broken = null;
    const released = new Promise(done => { release = done; });
    const firstFrame = new Promise((done, refuse) => { shown = done; broken = refuse; });
    firstFrame.catch(() => {}); // Nobody has to ask
    if (!config.wait && !address.has('wait')) release();
    config.start = () => {
        release();
        return firstFrame;
    };

    const wasi = {
        args_sizes_get(countPtr, sizePtr) {
            view().setUint32(countPtr, args.length, true);
            view().setUint32(sizePtr, args.reduce((n, a) => n + a.length, 0), true);
            return 0;
        },
        args_get(argvPtr, bufPtr) {
            for (const arg of args) {
                view().setUint32(argvPtr, bufPtr, true);
                u8().set(arg, bufPtr);
                argvPtr += 4;
                bufPtr += arg.length;
            }
            return 0;
        },
        environ_sizes_get(countPtr, sizePtr) {
            view().setUint32(countPtr, 0, true);
            view().setUint32(sizePtr, 0, true);
            return 0;
        },
        environ_get() { return 0; },
        clock_time_get(id, precision, timePtr) {
            const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
            view().setBigUint64(timePtr, ns, true);
            return 0;
        },
        clock_res_get(id, resPtr) {
            view().setBigUint64(resPtr, 1000n, true);
            return 0;
        },
        random_get(ptr, len) {
            u8().set(randomBytes(len), ptr);
            return 0;
        },
        sched_yield: () => 0, // malloc's lock, which threads spin on
        fd_write(fd, iovs, count, writtenPtr) {
            let written = 0;
            for (let i = 0; i < count; i++) {
                const ptr = view().getUint32(iovs + i * 8, true);
                const len = view().getUint32(iovs + i * 8 + 4, true);
                if (fd === 1 || fd === 2) output(fd, decoder.decode(u8().slice(ptr, ptr + len)));
                written += len;
            }
            view().setUint32(writtenPtr, written, true);
            return fd === 1 || fd === 2 ? 0 : EBADF;
        },
        fd_read(fd, iovs, count, readPtr) {
            view().setUint32(readPtr, 0, true);
            return 0;
        },
        // stdout and stderr are terminals, so wasi-libc flushes them line by line.
        fd_fdstat_get(fd, statPtr) {
            if (fd > 2) return EBADF;
            u8().fill(0, statPtr, statPtr + 24);
            u8()[statPtr] = 2; // Character device
            return 0;
        },
        fd_fdstat_set_flags() { return 0; },
        fd_close() { return 0; },
        fd_seek() { return ESPIPE; },
        fd_prestat_get() { return EBADF; }, // No directories
        fd_prestat_dir_name() { return EBADF; },
        path_open() { return ENOTCAPABLE; },
        path_filestat_get() { return ENOTCAPABLE; },
        proc_exit(code) {
            finish(code);
            throw new Exit(code);
        },
    };

    // -----------------------------------------------------------------------
    // The platform (tide_web.h)

    // Keys by the DOM's `code`, which names physical positions. A key pressed
    // and released between two frames still reads as held for one, so no tap
    // is lost however slow the frames are.
    const keyIndex = new Map();
    let keysHeld = new Uint8Array(0);
    let keysTapped = new Uint8Array(0);
    // Keys whose browser default (scrolling, moving focus) would fight the game.
    const keepFromBrowser = new Set(['Space', 'Tab', 'Backspace', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
        'PageUp', 'PageDown', 'Home', 'End']);
    function onKey(event, held) {
        // A phone's keyboard says no position for its keys: Enter is the one
        // the GUI needs from it, as what it types comes through the field.
        const index = keyIndex.get(event.code || (event.key === 'Enter' ? 'Enter' : ''));
        if (index === undefined) return;
        keysHeld[index] = held;
        if (held) keysTapped[index] = 1;
        // Typing into the page's field, a space has to reach it: what's typed there comes from it.
        const intoField = field && event.target === field && event.code === 'Space';
        if (keepFromBrowser.has(event.code) && !intoField) event.preventDefault();
    }
    function tapKey(code) {
        const index = keyIndex.get(code);
        if (index !== undefined) keysTapped[index] = 1;
    }
    // Characters typed, which follow the keyboard layout: `key` is one
    // character for keys that type one, and a name like "Shift" for the others.
    const typed = [];
    function onType(event) {
        if (event.ctrlKey || event.metaKey || event.isComposing) return;
        if (field && event.target === field) return; // Its input says what's typed
        const c = event.key.codePointAt(0);
        if ([...event.key].length !== 1 || c < 32 || c === 127) return;
        if (typed.length < 64) typed.push(c);
    }
    // A field of the page's own, hidden, which has the focus while the player
    // types into the GUI, so phones show their keyboard (tide_web_typing).
    // What's typed there comes as characters typed, and deleting it as
    // Backspace; it always holds a space, so there's something to delete.
    let field = null;
    function typingField() {
        if (field) return field;
        field = document.createElement('input');
        field.type = 'text';
        field.autocomplete = 'off';
        field.setAttribute('autocapitalize', 'off');
        field.setAttribute('autocorrect', 'off');
        field.spellcheck = false;
        // 16px, or iOS zooms into it; on the screen, or browsers won't focus it
        Object.assign(field.style, { position: 'fixed', left: '0', bottom: '0', width: '1px', height: '1px',
            opacity: '0', border: '0', padding: '0', fontSize: '16px' });
        field.value = ' ';
        field.addEventListener('input', event => {
            if (event.inputType === 'deleteContentBackward') tapKey('Backspace');
            else if (event.inputType === 'insertLineBreak') tapKey('Enter');
            else if (event.data) {
                for (const ch of event.data) {
                    const c = ch.codePointAt(0);
                    if (c >= 32 && c !== 127 && typed.length < 1024) typed.push(c);
                }
            }
            field.value = ' ';
        });
        document.body.appendChild(field);
        return field;
    }
    addEventListener('keydown', event => {
        onKey(event, 1);
        onType(event);
        // A key press is when the browser gives the cursor's lock (see below):
        // not Escape, which is how the player takes the cursor back, nor a key
        // that's only still held
        if (event.code !== 'Escape' && !event.repeat && !(field && event.target === field)) askForCursor();
    });
    // Ctrl+V (Cmd+V) pastes as typing, but for newlines and tabs.
    addEventListener('paste', event => {
        const text = event.clipboardData ? event.clipboardData.getData('text') : '';
        for (const ch of text) {
            const c = ch.codePointAt(0);
            if (c >= 32 && c !== 127 && typed.length < 1024) typed.push(c);
        }
        event.preventDefault();
    });
    // The clipboard API where the browser has it, else the old way: copying a
    // hidden field's selection.
    function copyText(text) {
        const field = document.createElement('textarea');
        field.value = text;
        field.style.position = 'fixed';
        field.style.opacity = '0';
        document.body.appendChild(field);
        field.select();
        try { document.execCommand('copy'); } catch { /* Nothing more to try */ }
        field.remove();
        focusCanvas();
    }
    addEventListener('keyup', event => onKey(event, 0));
    // The focus is the keyboard's: taking it never scrolls a page the game is a frame of.
    const focusCanvas = () => canvas.focus({ preventScroll: true });
    // Keys and buttons released elsewhere never send keyup or mouseup
    addEventListener('blur', () => { keysHeld.fill(0); keysTapped.fill(0); mouse.buttons = 0; });

    // The canvas's size in CSS pixels, which the program counts in, as desktop
    // builds count in the display's logical pixels. It renders devicePixelRatio
    // times as many, so it's as sharp as the page's text. The whole page when
    // `fill`. Checked every frame, which also catches zooming and moving to a
    // screen with another ratio.
    const size = { width: 0, height: 0, fill: false };
    function fit() {
        if (size.fill) {
            size.width = innerWidth;
            size.height = innerHeight;
        }
        const ratio = devicePixelRatio || 1;
        const width = Math.max(1, Math.round(size.width * ratio));
        const height = Math.max(1, Math.round(size.height * ratio));
        // Setting the size clears the canvas, even to the same size.
        if (canvas.width !== width) canvas.width = width;
        if (canvas.height !== height) canvas.height = height;
        const cssWidth = size.width + 'px', cssHeight = size.height + 'px';
        if (canvas.style.width !== cssWidth) canvas.style.width = cssWidth;
        if (canvas.style.height !== cssHeight) canvas.style.height = cssHeight;
    }

    // The mouse over the canvas, in CSS pixels. Buttons in tide_mouse's order:
    // left, right, middle, back, forward (the DOM's is left, middle, right, ...).
    // While the canvas has the cursor's lock (below), the cursor stays where
    // it was, and what the mouse moved adds up until the program takes it.
    const mouse = { x: 0, y: 0, buttons: 0, tapped: 0, wheelX: 0, wheelY: 0, movedX: 0, movedY: 0 }; // Taps as for keys
    const buttonBit = [1, 4, 2, 8, 16];
    function onMouseMove(event) {
        if (cursor.locked) { // Its events say where the cursor was locked, and how the mouse moved
            mouse.movedX += event.movementX || 0;
            mouse.movedY += event.movementY || 0;
            return;
        }
        const rect = canvas.getBoundingClientRect();
        mouse.x = (event.clientX - rect.left) * size.width / (rect.width || 1);
        mouse.y = (event.clientY - rect.top) * size.height / (rect.height || 1);
    }
    canvas.addEventListener('mousemove', onMouseMove);
    canvas.addEventListener('mousedown', event => {
        onMouseMove(event);
        mouse.buttons |= buttonBit[event.button] || 0;
        mouse.tapped |= buttonBit[event.button] || 0;
        focusCanvas();
        askForCursor(); // A click is when the browser gives the lock
    });
    addEventListener('mouseup', event => { mouse.buttons &= ~(buttonBit[event.button] || 0); });
    canvas.addEventListener('wheel', event => {
        if (embedded) return; // The page's, to scroll with (see below)
        mouse.wheelX -= Math.sign(event.deltaX);
        mouse.wheelY -= Math.sign(event.deltaY); // Positive away from the user
        event.preventDefault();
    }, { passive: false });

    // The cursor's lock (tide_web_cursor_lock): the canvas's pointer lock,
    // which hides the cursor and keeps it there, for a camera that looks
    // around. The program says whether it wants it, and the browser decides:
    //
    // - It only gives the lock when asked from a click or a key press. So the
    //   page asks at the next one while the program wants it, and at once when
    //   the program starts wanting it, which works while the last one still
    //   counts (a button's click that starts the game).
    // - It takes the lock back when the player presses Escape, which the page
    //   can't prevent, and may never see the key of. So Escape never asks.
    // - It takes it back when the page loses the focus or is hidden, too.
    // - For a moment after Escape, it refuses to give it again.
    //
    // The program is told what the browser says (pointerlockchange), never
    // what was asked. Nothing asks again on its own, on a timer or otherwise:
    // a lock refused or taken back waits for the player's next click or key.
    const cursor = { wanted: false, locked: false, asking: false };
    function askForCursor(plain) {
        if (!cursor.wanted || cursor.locked || cursor.asking || document.hidden || !canvas.requestPointerLock) return;
        cursor.asking = true;
        let asked;
        try {
            // The mouse's own movement, without the system's acceleration, where the browser has it
            asked = plain ? canvas.requestPointerLock() : canvas.requestPointerLock({ unadjustedMovement: true });
        } catch {
            cursor.asking = false;
            return;
        }
        // Browsers that answer say why they refused; all of them send an event
        if (asked && asked.catch) {
            asked.catch(error => {
                cursor.asking = false;
                // This system has no such movement: the usual kind, which is the same request
                if (!plain && error && error.name === 'NotSupportedError') askForCursor(true);
            });
        }
    }
    function letCursorGo() {
        if (document.pointerLockElement === canvas && document.exitPointerLock) document.exitPointerLock();
    }
    // The program is over: nothing wants the cursor any more.
    function endCursorLock() {
        cursor.wanted = false;
        letCursorGo();
    }
    document.addEventListener('pointerlockchange', () => {
        cursor.asking = false;
        cursor.locked = document.pointerLockElement === canvas;
        mouse.movedX = mouse.movedY = 0;
        if (cursor.locked && !cursor.wanted) letCursorGo(); // Given after the program stopped wanting it
    });
    document.addEventListener('pointerlockerror', () => { cursor.asking = false; });

    // `Tide.embedded`, or `embedded` in the page's address: the game is part
    // of a page that scrolls, in a frame of it, and leaves that page its
    // scrolling. The wheel is the page's, and so is a finger that swipes up or
    // down on the canvas, until the player taps the game: from then its
    // touches are all the game's, as they are without `embedded`, until the
    // player touches or clicks the page elsewhere (the frame loses the focus).
    // The page can change it as the game runs.
    let embedded = config.embedded === undefined ? address.has('embedded') : !!config.embedded;
    let playing = false; // The player tapped the game, and hasn't gone elsewhere since
    function followGestures() {
        const action = embedded && !playing ? 'pan-y pinch-zoom' : 'none';
        if (canvas.style.touchAction !== action) canvas.style.touchAction = action;
    }
    Object.defineProperty(config, 'embedded', { configurable: true, enumerable: true,
        get: () => embedded,
        set: value => { embedded = !!value; followGestures(); } });
    addEventListener('blur', () => { playing = false; followGestures(); });

    // Fingers on the canvas, from pointer events, in the order they happened;
    // the program takes them each frame. A finger is only the touchscreen's:
    // the browser makes no mouse events of it (its pointerdown's and its touch
    // events' defaults are canceled), and takes no gestures of its own on the
    // canvas (touch-action). Phases are tide_touch_phase's.
    //
    // Embedded, before a tap, the browser may take a finger to scroll the page
    // with, which it says with a pointercancel. So what such a finger does
    // waits (`unsure`): the program hears of it once it lifted, all at once, a
    // tap, and never of one the page scrolled with, or a swipe down a page
    // would press every game it starts on.
    const touches = [];
    let touchTaken = null;
    const unsure = new Map(); // By finger: what it did so far
    function onTouch(event, phase) {
        if (event.pointerType !== 'touch') return;
        event.preventDefault();
        const rect = canvas.getBoundingClientRect();
        const touch = {
            phase,
            source: event.pointerId >>> 0,
            x: (event.clientX - rect.left) * size.width / (rect.width || 1),
            y: (event.clientY - rect.top) * size.height / (rect.height || 1),
        };
        if (phase === 0 && embedded && !playing) {
            unsure.set(touch.source, [touch]);
            return;
        }
        const waiting = unsure.get(touch.source);
        if (waiting) {
            if (phase === 3) { // The page scrolled with it
                unsure.delete(touch.source);
                return;
            }
            // Where it is now is all that's left of how it moved
            if (phase === 1 && waiting[waiting.length - 1].phase === 1) waiting[waiting.length - 1] = touch;
            else waiting.push(touch);
            if (phase !== 2) return;
            // A tap: the game has the touchscreen from here, with every finger on it
            playing = true;
            followGestures();
            for (const known of unsure.values()) touches.push(...known);
            unsure.clear();
            focusCanvas();
            return;
        }
        if (phase === 1 && touches.length >= 1024) return; // The program isn't taking them: moves can go
        touches.push(touch);
        if (phase === 0) focusCanvas();
    }
    followGestures();
    canvas.addEventListener('pointerdown', event => onTouch(event, 0));
    canvas.addEventListener('pointermove', event => onTouch(event, 1));
    canvas.addEventListener('pointerup', event => onTouch(event, 2));
    canvas.addEventListener('pointercancel', event => onTouch(event, 3));
    for (const name of ['touchstart', 'touchmove', 'touchend', 'touchcancel']) {
        canvas.addEventListener(name, event => {
            // A finger the page may scroll with keeps its default until it lifts, and
            // one it scrolled with has none left to cancel
            const scrolls = embedded && !playing && (name === 'touchstart' || name === 'touchmove');
            if (!scrolls && event.cancelable) event.preventDefault();
        }, { passive: false });
    }

    // Frames come on animation frames or, with `timerFrames`, as fast as
    // timers allow (headless pages have no animation frames). A hidden page
    // gets neither at its pace: browsers stop its animation frames and slow its
    // timers to about one a second, which would stop this machine's part in a
    // match, a server its players time out of or a player its server drops. A
    // worker's timers keep their pace, so while the page is hidden, frames come
    // from a worker's, when the program says it next needs one (its match's
    // next tick), and draw nothing (tide_web_hidden).
    let timerFrames = false;
    let looping = false;   // The program runs frames
    let scheduled = false; // A frame is scheduled, on the page's own clock
    let heartbeat = null;  // The worker, while the page is hidden
    let beating = false;   // ...which has a frame coming
    let nextFrame = 1000 / 60; // Milliseconds until the program needs a frame, as its last one said
    function scheduleFrame() {
        if (scheduled || heartbeat) return;
        scheduled = true;
        if (timerFrames || document.hidden) setTimeout(scheduledFrame, 0);
        else requestAnimationFrame(scheduledFrame);
    }
    function scheduledFrame() {
        scheduled = false;
        if (looping) frame();
    }
    function beat() {
        if (beating) return; // One frame coming at a time, whatever else runs one
        beating = true;
        heartbeat.postMessage(nextFrame);
    }
    function stopHeartbeat() {
        if (heartbeat) heartbeat.terminate();
        heartbeat = null;
        beating = false;
    }
    function followVisibility() {
        if (document.hidden && !heartbeat && looping) {
            try {
                const wait = 'onmessage = event => setTimeout(() => postMessage(0), event.data);';
                heartbeat = new Worker(URL.createObjectURL(new Blob([wait], { type: 'text/javascript' })));
                heartbeat.onmessage = () => {
                    beating = false;
                    if (!looping) stopHeartbeat(); // The program is over
                    else if (document.hidden) frame();
                };
                beat();
            } catch (error) { // Frames come as slowly as the browser lets them
                heartbeat = null;
                printErr('tide: no worker to keep frames going while the page is hidden: ' + error);
            }
        } else if (!document.hidden && heartbeat) {
            stopHeartbeat();
            if (looping) scheduleFrame();
        }
    }
    document.addEventListener('visibilitychange', followVisibility);
    function frame() {
        if (stopped) {
            looping = false;
            return;
        }
        fit();
        try {
            // Timers take whole milliseconds, dropping the rest: one rounded
            // down would come before the tick, for nothing
            const seconds = exports.tide_web_frame();
            nextFrame = seconds > 0 ? Math.ceil(seconds * 1000) : 0;
            shown();
        } catch (error) {
            looping = false;
            if (!(error instanceof Exit)) fail(error);
            return;
        }
        if (heartbeat) beat();
        else scheduleFrame();
    }

    const platform = {
        init_canvas(width, height, resizable) {
            size.width = width;
            size.height = height;
            size.fill = !!resizable;
            fit();
            // WebGPU where the page found a device (below), else WebGL 2. A
            // canvas only ever has the one it first gave.
            if (device) {
                windowTarget.context = windowTarget.context || canvas.getContext('webgpu');
                if (windowTarget.context) {
                    windowTarget.context.configure({ device, format: gpuFormat, alphaMode: 'opaque' });
                    if (!groupLayouts) gpuStart();
                    devices = devices || 1;
                    return 2;
                }
                device = null;
            }
            if (wantedBackend === 'webgpu') return 0; // Asked for, and not to be had
            gl = canvas.getContext('webgl2', { alpha: false, antialias: false, depth: true, stencil: false });
            return gl ? 1 : 0;
        },
        canvas_width: () => size.width,
        canvas_height: () => size.height,
        canvas_pixel_width: () => canvas.width,
        canvas_pixel_height: () => canvas.height,
        mouse_x: () => mouse.x,
        mouse_y: () => mouse.y,
        mouse_buttons() { const held = mouse.buttons | mouse.tapped; mouse.tapped = 0; return held; },
        take_wheel_x() { const v = mouse.wheelX; mouse.wheelX = 0; return v; },
        take_wheel_y() { const v = mouse.wheelY; mouse.wheelY = 0; return v; },
        take_mouse_moved_x() { const v = mouse.movedX; mouse.movedX = 0; return v; },
        take_mouse_moved_y() { const v = mouse.movedY; mouse.movedY = 0; return v; },
        cursor_lock(lock) {
            lock = !!lock;
            if (lock === cursor.wanted) return;
            cursor.wanted = lock;
            if (!lock) letCursorGo();
            // At once, if a click or a key press still counts; else at the next one
            else if (!navigator.userActivation || navigator.userActivation.isActive) askForCursor();
        },
        cursor_locked: () => cursor.locked && !document.hidden ? 1 : 0,
        touchscreen: () => (navigator.maxTouchPoints || 0) > 0 ? 1 : 0,
        take_touch() { touchTaken = touches.shift() || null; return touchTaken ? 1 : 0; },
        touch_phase: () => touchTaken ? touchTaken.phase : 0,
        touch_source: () => touchTaken ? touchTaken.source : 0,
        touch_x: () => touchTaken ? touchTaken.x : 0,
        touch_y: () => touchTaken ? touchTaken.y : 0,
        watch_key(index, codePtr) {
            keyIndex.set(string(codePtr), index);
            if (index >= keysHeld.length) {
                const grown = new Uint8Array(index + 1);
                grown.set(keysHeld);
                keysHeld = grown;
                const tapped = new Uint8Array(index + 1);
                tapped.set(keysTapped);
                keysTapped = tapped;
            }
        },
        key_held(index) { const held = keysHeld[index] || keysTapped[index] || 0; keysTapped[index] = 0; return held; },
        take_char: () => typed.length ? typed.shift() : 0,
        typing(on) {
            if (on) typingField().focus({ preventScroll: true });
            else if (field && document.activeElement === field) focusCanvas();
        },
        copy(textPtr) {
            const text = string(textPtr);
            if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(text).catch(() => copyText(text));
            else copyText(text);
        },
        gamepad_connected(pad) {
            const gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
            return gamepads[pad] && gamepads[pad].connected ? 1 : 0;
        },
        gamepad_axis(pad, axis) {
            const gamepad = navigator.getGamepads()[pad];
            return gamepad && axis < gamepad.axes.length ? gamepad.axes[axis] : 0;
        },
        gamepad_button(pad, button) {
            const gamepad = navigator.getGamepads()[pad];
            return gamepad && button < gamepad.buttons.length ? gamepad.buttons[button].value : 0;
        },
        run(timers) {
            timerFrames = !!timers;
            if (!looping) {
                looping = true;
                scheduleFrame();
                followVisibility(); // It may start in a tab in the background
            }
            throw unwind;
        },
        hidden: () => document.hidden ? 1 : 0,
        stop() { stopped = true; },
        eval: scriptPtr => (0, eval)(string(scriptPtr)) ? 1 : 0,
    };

    // -----------------------------------------------------------------------
    // Rooms: matches players find by a code, through the relay (relay/). The
    // relay only introduces players, passing along what WebRTC needs to
    // connect them; their packets then go straight between them, on data
    // channels that neither order nor resend, like UDP. A program is in one
    // room at a time: the one it hosts, or the one it joined. Players in it are
    // numbered: the host is 0 to those who join, and they're 1 and up to it.
    // A room it hosts has a key, which lets the players of its match meet there
    // again when it goes (host migration): the relay makes the first one there
    // its host, and introduces the others to it.

    const relayUrl = config.relay || 'wss://relay.tide-engine.dev';
    // `Tide.lag`, or `lag=` in the page's address: the round trip to its
    // match's server this page's player is to have, in milliseconds, to see a
    // match as on a slow connection with both players at one desk. It's the
    // whole trip, there and back, and what the trip takes already is part of
    // it: the page adds what's missing (`added`), half to what it sends and
    // half to what it receives, so a lag of 100 reads as a ping of 100, not as
    // 100 more than it was. A page that hosts adds nothing: its machine runs
    // the server, and each player who joined says its own. The page can change
    // it while the game runs. `Tide.room` is the code of the room the program
    // hosts or joined ("" for none), for the page to show or to link to.
    if (config.lag === undefined) config.lag = Number(new URLSearchParams(location.search).get('lag')) || 0;
    Object.defineProperty(config, 'room', { configurable: true, enumerable: true,
        get: () => room && !room.failed && room.reachable ? room.code : '' });
    const CODE_LETTERS = '23456789ABCDEFGHJKLMNPQRSTUVWXYZ'; // No look-alikes: 0 and O, 1 and I
    let room = null;
    let rooms = 0; // Rooms opened so far, which numbers them

    const newCode = () => Array.from(crypto.getRandomValues(new Uint8Array(6)), b => CODE_LETTERS[b & 31]).join('');
    const newKey = () => Array.from(crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2, '0')).join('');

    // `key`: going to the room again with it, to host it or join its host,
    // whichever the relay says (`moved`: 1 hosting, 2 joining, -1 failed).
    function openRoom(hosting, code, key) {
        closeRoom();
        room = {
            number: ++rooms, hosting, code, failed: false, reachable: true, connected: false, opened: performance.now(),
            ws: null, announced: false, ice: [], peers: new Map(), byRelay: new Map(), next: 1, inbox: [],
            key: key || (hosting ? newKey() : ''), moving: !!key, moved: 0,
            // `Tide.lag`: what the page adds to a round trip and since when,
            // what a trip takes with nothing added, once one was measured,
            // and whether the program heard from the room since it last said
            added: 0, addedSince: 0, rest: 0, measured: false, heard: false,
        };
        if (/^[2-9A-HJ-NP-Z]{6}$/.test(code)) {
            connectRelay(room);
        } else {
            room.failed = true;
            if (room.moving) room.moved = -1;
            printErr(`tide: '${code}' isn't a room code: they're 6 letters and digits, like K7QF2M`);
        }
        return room.number;
    }

    function closeRoom() {
        if (!room) return;
        const r = room;
        room = null;
        if (r.ws) r.ws.close();
        for (const peer of r.peers.values()) peer.pc.close();
    }

    // The milliseconds this page adds to a round trip in room `r` now, for its
    // player to have the one `Tide.lag` says: none while the trip takes that
    // long by itself.
    function added(r) {
        const lag = r.hosting ? 0 : Math.min(Number(config.lag) || 0, 2000);
        const add = Math.max(Math.round(lag - r.rest), 0);
        if (add !== r.added) {
            r.added = add;
            r.addedSince = performance.now();
        }
        return add;
    }

    // The match in room `r`, which this program hosts, ended: the relay keeps
    // the room as ended a while, so its players don't take it over. It may not
    // know the room yet (its hello hasn't come, or it's being reached again),
    // so the room asks for it first: the relay reads a connection's messages
    // in order. A connection still opening, or a new one, says it once open,
    // and the room closing leaves it be.
    function sayEnded(r) {
        const say = (ws, announced) => {
            if (!announced) ws.send(JSON.stringify({ host: r.code, key: r.key }));
            ws.send(JSON.stringify({ end: r.code }));
        };
        if (r.ws && r.ws.readyState === WebSocket.OPEN) {
            say(r.ws, r.announced);
            return;
        }
        let ws = r.ws && r.ws.readyState === WebSocket.CONNECTING ? r.ws : null;
        r.ws = null;
        if (!ws) {
            try {
                ws = new WebSocket(relayUrl);
            } catch {
                return;
            }
        }
        ws.onmessage = ws.onclose = null;
        ws.onopen = () => {
            say(ws, false);
            ws.close();
        };
    }

    // A host that loses the relay opens its room again once it's back, under
    // the same code if it's still free. Players already in keep playing: only
    // joining needs the relay.
    function connectRelay(r) {
        let ws;
        try {
            ws = new WebSocket(relayUrl);
        } catch (error) {
            printErr(`tide: can't reach the relay at ${relayUrl}: ${error.message}`);
            r.failed = !r.hosting;
            r.reachable = false;
            return;
        }
        r.ws = ws;
        r.announced = false; // Whether this connection asked the relay for the room yet
        const send = message => { if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(message)); };
        ws.onmessage = event => {
            if (room !== r) return;
            let m;
            try {
                m = JSON.parse(event.data);
            } catch {
                return;
            }
            if (m.relay) {
                r.ice = m.ice || [];
                send(r.moving ? { migrate: r.code, key: r.key } : r.hosting ? { host: r.code, key: r.key } : { join: r.code });
                r.announced = true;
            } else if (m.hosting) {
                r.reachable = true;
                if (r.moving) { // The room's host was gone: this program is now
                    r.moving = false;
                    r.hosting = true;
                    r.moved = 1;
                }
            } else if (m.ended && r.moving) { // Its host ended the match there
                r.moving = false;
                r.failed = true;
                r.moved = -2;
            } else if (m.taken && r.moving) { // Another key: not the match it was in
                r.moving = false;
                r.failed = true;
                r.moved = -1;
                printErr(`tide: room ${r.code} has another match now`);
            } else if (m.taken) {
                r.code = newCode();
                send({ host: r.code, key: r.key });
            } else if (m.lost && r.hosting) {
                // Its players took the room over, thinking it gone: it hosts it no more
                r.failed = true;
                r.reachable = false;
                printErr(`tide: room ${m.lost} has another host now`);
            } else if (m.missing && !r.hosting && performance.now() - r.opened < 5000) {
                // A room its host just opened, which the relay may not know
                // yet (it can take a few seconds to wake up): ask again soon
                setTimeout(() => { if (room === r) send({ join: r.code }); }, 500);
            } else if (m.missing || m.full || m.closed) {
                if (!r.connected) {
                    r.failed = true;
                    printErr(m.missing ? `tide: no room has the code ${m.missing}`
                        : m.full ? `tide: too many players are joining room ${m.full} at once`
                        : `tide: room ${m.closed} closed`);
                }
            } else if (m.joined) {
                if (r.moving) { // The room's host is there: this program joins it
                    r.moving = false;
                    r.moved = 2;
                }
                offer(r, send);
            } else if (m.peer) {
                const peer = newPeer(r, r.next++, signal => send({ to: m.peer, signal }));
                r.byRelay.set(m.peer, peer);
            } else if (m.from) {
                const peer = r.byRelay.get(m.from);
                if (peer) onSignal(r, peer, m.signal, false);
            } else if (m.signal) {
                const peer = r.peers.get(0);
                if (peer) onSignal(r, peer, m.signal, true);
            } else if (m.left) {
                const peer = r.byRelay.get(m.left);
                r.byRelay.delete(m.left);
                // It gave up before connecting, rather than hanging up once it had
                if (peer && !peer.open && peer.pc.connectionState !== 'connected') dropPeer(r, peer);
            }
        };
        ws.onclose = () => {
            if (room !== r || r.ws !== ws) return;
            r.ws = null;
            if (!r.hosting) {
                if (!r.connected && !r.failed && !r.peers.size && performance.now() - r.opened < 10000) {
                    // Not introduced yet: the relay may be waking up (it
                    // refuses connections for a moment then), so try again
                    setTimeout(() => { if (room === r && !r.ws) connectRelay(r); }, 1000);
                } else if (!r.connected && !r.failed) {
                    r.failed = true;
                    if (r.moving) r.moved = -1;
                    printErr(`tide: lost the relay at ${relayUrl} while joining room ${r.code}`);
                }
                return;
            }
            if (r.reachable) printErr(`tide: lost the relay at ${relayUrl}; room ${r.code} opens again once it's back`);
            r.reachable = false;
            // Players still connecting can't finish without it, and the relay
            // numbers those who join from scratch when it's back.
            for (const peer of r.byRelay.values()) if (!peer.open) dropPeer(r, peer);
            r.byRelay.clear();
            setTimeout(() => { if (room === r) connectRelay(r); }, 3000);
        };
    }

    // A player on the other end of a data channel: the host, to one who joins,
    // and each one who joins, to the host.
    function newPeer(r, number, sendSignal) {
        // Tide.iceTransportPolicy 'relay' makes every connection go through
        // TURN, to test it: normally they're direct whenever they can be.
        const pc = new RTCPeerConnection({ iceServers: r.ice, iceTransportPolicy: config.iceTransportPolicy || 'all' });
        const peer = { number, pc, channel: null, open: false, sendSignal, chain: Promise.resolve() };
        pc.onicecandidate = event => { if (event.candidate) sendSignal({ candidate: event.candidate.toJSON() }); };
        pc.onconnectionstatechange = () => {
            if (pc.connectionState === 'failed' || pc.connectionState === 'closed') dropPeer(r, peer);
        };
        pc.ondatachannel = event => useChannel(r, peer, event.channel);
        r.peers.set(number, peer);
        return peer;
    }

    function useChannel(r, peer, channel) {
        channel.binaryType = 'arraybuffer';
        const opened = () => {
            if (peer.open) return;
            peer.open = true;
            if (!r.hosting) r.connected = true;
        };
        channel.onopen = opened;
        channel.onmessage = event => {
            // Joined: the relay's part is done once the host sends over the
            // channel, which it only does with its end open. Not before: the
            // relay tells the host this one left, and a host that hasn't
            // caught up with its own end yet takes it for a player who gave up
            if (!r.hosting && r.ws) r.ws.close();
            if (room !== r || !(event.data instanceof ArrayBuffer)) return;
            const arrive = () => {
                if (room === r && r.inbox.length < 4096) r.inbox.push([peer.number, new Uint8Array(event.data)]);
            };
            // The way back's half of what `Tide.lag` adds
            const late = Math.floor(added(r) / 2);
            if (late > 0) setTimeout(arrive, late);
            else arrive();
        };
        channel.onclose = () => dropPeer(r, peer);
        peer.channel = channel;
        // A channel the other end made can be open already by the time the
        // page hears of it (ondatachannel), with no open event to come: it
        // would never be sent to, and its player never let in
        if (channel.readyState === 'open') opened();
    }

    function dropPeer(r, peer) {
        if (r.peers.get(peer.number) !== peer) return;
        r.peers.delete(peer.number);
        peer.pc.close();
        if (!r.hosting && !r.connected && !r.failed) {
            r.failed = true;
            printErr(`tide: couldn't connect to the host of room ${r.code}`);
        }
    }

    // Joining: this end offers, and the host answers.
    async function offer(r, send) {
        const peer = newPeer(r, 0, signal => send({ signal }));
        useChannel(r, peer, peer.pc.createDataChannel('tide', { ordered: false, maxRetransmits: 0 }));
        try {
            await peer.pc.setLocalDescription();
            send({ signal: { description: peer.pc.localDescription.toJSON() } });
        } catch (error) {
            printErr('tide: ' + error.message);
            dropPeer(r, peer);
        }
    }

    // Signals are handled one at a time, in order, since each waits on WebRTC:
    // a candidate only goes in after the description before it.
    function onSignal(r, peer, signal, joining) {
        if (!signal || typeof signal !== 'object') return;
        peer.chain = peer.chain.then(async () => {
            if (room !== r || r.peers.get(peer.number) !== peer) return;
            if (signal.description) {
                await peer.pc.setRemoteDescription(signal.description);
                if (!joining && signal.description.type === 'offer') {
                    await peer.pc.setLocalDescription();
                    peer.sendSignal({ description: peer.pc.localDescription.toJSON() });
                }
            } else if (signal.candidate) {
                await peer.pc.addIceCandidate(signal.candidate);
            }
        }).catch(error => printErr('tide: ' + error.message));
    }

    Object.assign(platform, {
        room_host: () => openRoom(true, newCode()),
        room_join: codePtr => openRoom(false, string(codePtr).replace(/\s+/g, '').toUpperCase()),
        room_migrate: (codePtr, keyPtr) => openRoom(false, string(codePtr), string(keyPtr)),
        room_migrated: number => !room || room.number !== number ? -1 : room.failed && !room.moved ? -1 : room.moved,
        // The key of the room it hosts into `out` (33 bytes), "" for none
        room_key(outPtr) {
            const key = room && room.hosting && !room.failed ? room.key : '';
            u8().set(encoder.encode(key), outPtr);
            u8()[outPtr + key.length] = 0;
        },
        room_close(number) { if (room && room.number === number) closeRoom(); },
        room_end(number) { if (room && room.number === number && room.hosting) sayEnded(room); },
        // The code into `out` (7 bytes), "" while there's none: never a room
        // that failed, nor one the relay can't be told about.
        room_code(outPtr) {
            const code = room && !room.failed && room.reachable ? room.code : '';
            u8().set(encoder.encode(code), outPtr);
            u8()[outPtr + code.length] = 0;
        },
        room_failed: () => room && room.failed ? 1 : 0,
        room_send(number, to, ptr, size) {
            const peer = room && room.number === number ? room.peers.get(to) : null;
            const channel = peer && peer.open ? peer.channel : null;
            // Like UDP, what can't go now is lost, and so is what would queue up
            if (channel && channel.readyState === 'open' && channel.bufferedAmount < 262144) {
                const bytes = u8().slice(ptr, ptr + size);
                // The way there's half of what `Tide.lag` adds
                const late = Math.ceil(added(room) / 2);
                if (late > 0) {
                    setTimeout(() => {
                        if (channel.readyState === 'open' && channel.bufferedAmount < 262144) channel.send(bytes);
                    }, late);
                } else channel.send(bytes);
            }
        },
        room_receive(number, fromPtr, ptr, capacity) {
            if (!room || room.number !== number) return 0;
            for (;;) {
                const next = room.inbox.shift();
                if (!next) return 0;
                const [from, bytes] = next;
                if (bytes.length > capacity || bytes.length === 0) continue; // Longer ones are dropped
                view().setUint32(fromPtr, from, true);
                u8().set(bytes, ptr);
                room.heard = true;
                return bytes.length;
            }
        },
        // The round trip the program measures to its room's host, in
        // milliseconds (0 for none), which it says every frame: what `Tide.lag`
        // is to make it. Less what the page adds, a trip is what one takes by
        // itself, if all of it went with what's added now: it set out after
        // that last changed. The program counts time by its frames, each up to
        // a frame's work behind the page's clock, so a trip that set out
        // within 50 ms of the change doesn't count. Nor does one said again
        // with nothing heard since. Frames come when they come, so trips
        // differ by a frame's length: the page goes by what they take on
        // the whole.
        room_ping(ping) {
            const r = room;
            if (!r) return;
            const add = added(r);
            const heard = r.heard;
            r.heard = false;
            if (!heard || !(ping > 0) || performance.now() - ping - 50 <= r.addedSince) return;
            const rest = Math.max(ping - add, 0);
            r.rest = r.measured ? r.rest + (rest - r.rest) / 8 : rest;
            r.measured = true;
        },
    });

    // -----------------------------------------------------------------------
    // OpenGL ES 3 on WebGL 2: the functions the platform layer's renderer
    // calls (platform/src/gl.h), each by its C name. GL names objects with
    // numbers, WebGL with objects, so each kind has a table from number to
    // object.

    const tables = { buffer: [null], texture: [null], framebuffer: [null], renderbuffer: [null], vertexArray: [null],
        program: [null], shader: [null], uniform: [null], sampler: [null] };
    function add(kind, object) {
        tables[kind].push(object);
        return tables[kind].length - 1;
    }
    const get = (kind, id) => tables[kind][id] || null;
    function generate(kind, create, count, ptr) {
        for (let i = 0; i < count; i++) i32()[(ptr >> 2) + i] = add(kind, create());
    }
    function remove(kind, destroy, count, ptr) {
        for (let i = 0; i < count; i++) {
            const id = i32()[(ptr >> 2) + i];
            if (tables[kind][id]) destroy(tables[kind][id]);
            tables[kind][id] = null;
        }
    }

    // Uniform locations are numbers per program and name.
    const uniformIds = new Map();
    function uniformId(program, name) {
        const key = program + ':' + name;
        if (!uniformIds.has(key)) {
            const location = gl.getUniformLocation(get('program', program), name);
            uniformIds.set(key, location ? add('uniform', location) : -1);
        }
        return uniformIds.get(key);
    }

    // The typed view and element offset for pixel data of `type` at `ptr`.
    function pixels(type, ptr) {
        switch (type) {
        case 0x1406: return [f32(), ptr >> 2]; // FLOAT
        case 0x1403: return [new Uint16Array(memory.buffer), ptr >> 1]; // UNSIGNED_SHORT
        case 0x1405: return [new Uint32Array(memory.buffer), ptr >> 2]; // UNSIGNED_INT
        default: return [u8(), ptr];
        }
    }

    function writeLog(log, max, lengthPtr, ptr) {
        const bytes = encoder.encode(log || '');
        const n = Math.max(0, Math.min(bytes.length, max - 1));
        if (max > 0) {
            u8().set(bytes.subarray(0, n), ptr);
            u8()[ptr + n] = 0;
        }
        if (lengthPtr) i32()[lengthPtr >> 2] = n;
    }

    // A shader's or program's parameter: a number, or whether, as 1 or 0.
    const writeParameter = (value, ptr) => { i32()[ptr >> 2] = Number(value) || 0; };

    // What the browser has to say about a shader that compiled, or shaders
    // that linked: a warning. Test pages stop at one, as at a GL error, with
    // the shader it's about; the others print it.
    function shaderWarning(what, log, source) {
        if (!log || !log.trim()) return;
        if (config.checkGL) throw new Error(`${what}:\n${log}${source ? '\n' + source : ''}`);
        printErr(`tide: ${what}:\n${log}`);
    }

    const glFunctions = {
        glActiveTexture: t => gl.activeTexture(t),
        glAttachShader: (p, s) => gl.attachShader(get('program', p), get('shader', s)),
        glBindBuffer: (target, b) => gl.bindBuffer(target, get('buffer', b)),
        glBindBufferBase: (target, index, b) => gl.bindBufferBase(target, index, get('buffer', b)),
        glBindFramebuffer: (target, f) => gl.bindFramebuffer(target, get('framebuffer', f)),
        glBindRenderbuffer: (target, r) => gl.bindRenderbuffer(target, get('renderbuffer', r)),
        glBindSampler: (unit, s) => gl.bindSampler(unit, get('sampler', s)),
        glBindTexture: (target, t) => gl.bindTexture(target, get('texture', t)),
        glBindVertexArray: v => gl.bindVertexArray(get('vertexArray', v)),
        glBlendFunc: (s, d) => gl.blendFunc(s, d),
        glBlendFuncSeparate: (s, d, sa, da) => gl.blendFuncSeparate(s, d, sa, da),
        glBufferData(target, size, ptr, usage) {
            if (ptr) gl.bufferData(target, u8(), usage, ptr, size);
            else gl.bufferData(target, size, usage);
        },
        glBufferSubData: (target, offset, size, ptr) => gl.bufferSubData(target, offset, u8(), ptr, size),
        glCheckFramebufferStatus: target => gl.checkFramebufferStatus(target),
        glClear: mask => gl.clear(mask),
        glClearColor: (r, g, b, a) => gl.clearColor(r, g, b, a),
        glCompileShader(s) {
            const shader = get('shader', s);
            gl.compileShader(shader);
            if (gl.getShaderParameter(shader, gl.COMPILE_STATUS)) { // The program asks about one that didn't
                shaderWarning('a shader compiled with warnings', gl.getShaderInfoLog(shader), gl.getShaderSource(shader));
            }
        },
        glCreateProgram: () => add('program', gl.createProgram()),
        glCreateShader: type => add('shader', gl.createShader(type)),
        glDeleteBuffers: (n, ptr) => remove('buffer', o => gl.deleteBuffer(o), n, ptr),
        glDeleteFramebuffers: (n, ptr) => remove('framebuffer', o => gl.deleteFramebuffer(o), n, ptr),
        glDeleteProgram(p) {
            gl.deleteProgram(get('program', p));
            tables.program[p] = null;
            for (const [key, id] of uniformIds) { // Its uniforms' locations go with it
                if (!key.startsWith(p + ':')) continue;
                if (id > 0) tables.uniform[id] = null;
                uniformIds.delete(key);
            }
        },
        glDeleteRenderbuffers: (n, ptr) => remove('renderbuffer', o => gl.deleteRenderbuffer(o), n, ptr),
        glDeleteShader(s) {
            gl.deleteShader(get('shader', s));
            tables.shader[s] = null;
        },
        glDeleteTextures: (n, ptr) => remove('texture', o => gl.deleteTexture(o), n, ptr),
        glDeleteVertexArrays: (n, ptr) => remove('vertexArray', o => gl.deleteVertexArray(o), n, ptr),
        glDepthFunc: f => gl.depthFunc(f),
        glDisable: cap => gl.disable(cap),
        glDrawArrays: (mode, first, count) => gl.drawArrays(mode, first, count),
        glDrawArraysInstanced: (mode, first, count, instances) => gl.drawArraysInstanced(mode, first, count, instances),
        glDrawElementsInstanced: (mode, count, type, offset, instances) =>
            gl.drawElementsInstanced(mode, count, type, offset, instances),
        glEnable: cap => gl.enable(cap),
        glEnableVertexAttribArray: i => gl.enableVertexAttribArray(i),
        glFramebufferRenderbuffer: (target, attachment, rbTarget, r) =>
            gl.framebufferRenderbuffer(target, attachment, rbTarget, get('renderbuffer', r)),
        glFramebufferTexture2D: (target, attachment, textureTarget, t, level) =>
            gl.framebufferTexture2D(target, attachment, textureTarget, get('texture', t), level),
        glGenBuffers: (n, ptr) => generate('buffer', () => gl.createBuffer(), n, ptr),
        glGenFramebuffers: (n, ptr) => generate('framebuffer', () => gl.createFramebuffer(), n, ptr),
        glGenRenderbuffers: (n, ptr) => generate('renderbuffer', () => gl.createRenderbuffer(), n, ptr),
        glGenSamplers: (n, ptr) => generate('sampler', () => gl.createSampler(), n, ptr),
        glGenTextures: (n, ptr) => generate('texture', () => gl.createTexture(), n, ptr),
        glGenVertexArrays: (n, ptr) => generate('vertexArray', () => gl.createVertexArray(), n, ptr),
        glGetIntegerv: (pname, ptr) => writeParameter(gl.getParameter(pname), ptr),
        glGetProgramInfoLog: (p, max, lengthPtr, ptr) => writeLog(gl.getProgramInfoLog(get('program', p)), max, lengthPtr, ptr),
        glGetProgramiv: (p, pname, ptr) => writeParameter(gl.getProgramParameter(get('program', p), pname), ptr),
        glGetShaderInfoLog: (s, max, lengthPtr, ptr) => writeLog(gl.getShaderInfoLog(get('shader', s)), max, lengthPtr, ptr),
        glGetShaderiv: (s, pname, ptr) => writeParameter(gl.getShaderParameter(get('shader', s), pname), ptr),
        glGetUniformBlockIndex: (p, name) => gl.getUniformBlockIndex(get('program', p), string(name)),
        glGetUniformLocation: (p, name) => uniformId(p, string(name)),
        glLinkProgram(p) {
            const program = get('program', p);
            gl.linkProgram(program);
            if (gl.getProgramParameter(program, gl.LINK_STATUS)) {
                shaderWarning('shaders linked with warnings', gl.getProgramInfoLog(program));
            }
        },
        glPixelStorei: (pname, param) => gl.pixelStorei(pname, param),
        glReadPixels(x, y, w, h, format, type, ptr) {
            const [array, offset] = pixels(type, ptr);
            gl.readPixels(x, y, w, h, format, type, array, offset);
        },
        glRenderbufferStorage: (target, format, w, h) => gl.renderbufferStorage(target, format, w, h),
        glSamplerParameteri: (s, pname, param) => gl.samplerParameteri(get('sampler', s), pname, param),
        glScissor: (x, y, w, h) => gl.scissor(x, y, w, h),
        glShaderSource(s, count, stringsPtr, lengthsPtr) {
            let source = '';
            for (let i = 0; i < count; i++) {
                const ptr = i32()[(stringsPtr >> 2) + i];
                const len = lengthsPtr ? i32()[(lengthsPtr >> 2) + i] : -1;
                source += len < 0 ? string(ptr) : decoder.decode(u8().slice(ptr, ptr + len));
            }
            gl.shaderSource(get('shader', s), source);
        },
        glTexImage2D(target, level, internal, w, h, border, format, type, ptr) {
            if (!ptr) {
                gl.texImage2D(target, level, internal, w, h, border, format, type, null);
                return;
            }
            const [array, offset] = pixels(type, ptr);
            gl.texImage2D(target, level, internal, w, h, border, format, type, array, offset);
        },
        glTexParameteri: (target, pname, param) => gl.texParameteri(target, pname, param),
        glTexSubImage2D(target, level, x, y, w, h, format, type, ptr) {
            const [array, offset] = pixels(type, ptr);
            gl.texSubImage2D(target, level, x, y, w, h, format, type, array, offset);
        },
        glUniform1i: (l, x) => gl.uniform1i(get('uniform', l), x),
        glUniformBlockBinding: (p, block, binding) => gl.uniformBlockBinding(get('program', p), block, binding),
        glUseProgram: p => gl.useProgram(get('program', p)),
        glVertexAttribDivisor: (i, divisor) => gl.vertexAttribDivisor(i, divisor),
        glVertexAttribIPointer: (i, size, type, stride, offset) => gl.vertexAttribIPointer(i, size, type, stride, offset),
        glVertexAttribPointer: (i, size, type, normalized, stride, offset) =>
            gl.vertexAttribPointer(i, size, type, !!normalized, stride, offset),
        glViewport: (x, y, w, h) => gl.viewport(x, y, w, h),
    };

    // With `checkGL` (test pages), a GL error stops the program, naming the
    // call: WebGL only warns about one in the console, where no test sees it.
    // Each call waits for glGetError, which is too slow for pages that ship.
    if (config.checkGL) {
        const errors = { 0x0500: 'INVALID_ENUM', 0x0501: 'INVALID_VALUE', 0x0502: 'INVALID_OPERATION',
            0x0505: 'OUT_OF_MEMORY', 0x0506: 'INVALID_FRAMEBUFFER_OPERATION', 0x9242: 'CONTEXT_LOST_WEBGL' };
        for (const [name, call] of Object.entries(glFunctions)) {
            glFunctions[name] = (...args) => {
                const result = call(...args);
                const error = gl.getError();
                if (error) throw new Error(`${name}(${args.join(', ')}): GL_${errors[error] || error}`);
                return result;
            };
        }
    }

    // -----------------------------------------------------------------------
    // WebGPU: what the program draws with where the browser has it on a GPU,
    // instead of WebGL 2 above. These are the functions of the platform
    // layer's GPU interface (platform/src/gpu.h), which platform/web/gpu_web.c
    // hands it. Buffers, textures, pipelines and targets are numbers to the
    // program, each kind with a table from number to object.
    //
    // The page asks for the device before the program starts: asking takes a
    // while, and the program can't wait for an answer. `Tide.backend`, or
    // `backend=` in the page's address, picks what draws: 'webgl', or
    // 'webgpu', which also takes the browser's software device.

    const gpuTables = { buffer: [null], texture: [null], pipeline: [null], target: [null] };
    const gpuFree = { buffer: [], texture: [], pipeline: [], target: [] };
    function gpuAdd(kind, object) {
        const id = gpuFree[kind].length ? gpuFree[kind].pop() : gpuTables[kind].length;
        gpuTables[kind][id] = object;
        return id;
    }
    function gpuRemove(kind, id) {
        gpuTables[kind][id] = null;
        gpuFree[kind].push(id);
    }

    // `again`: for a canvas that already draws with WebGPU, which it keeps to.
    async function findDevice(again) {
        if (wantedBackend === 'webgl' || !navigator.gpu) return;
        try {
            const adapter = await navigator.gpu.requestAdapter();
            if (!adapter) return;
            // The browser's software device is slower than WebGL on a GPU
            const software = adapter.info && 'isFallbackAdapter' in adapter.info ? adapter.info.isFallbackAdapter
                : adapter.isFallbackAdapter;
            if (software && wantedBackend !== 'webgpu' && !again) return;
            const found = await adapter.requestDevice();
            // A mistake in how the program uses it: test pages stop at one, as for GL's
            found.onuncapturederror = event => {
                if (config.checkGL) fail(new Error('WebGPU: ' + event.error.message));
                else printErr('tide: WebGPU: ' + event.error.message);
            };
            found.lost.then(info => { if (device === found) deviceLost(info); });
            gpuFormat = navigator.gpu.getPreferredCanvasFormat();
            device = found;
        } catch (error) {
            printErr('tide: no WebGPU: ' + error);
        }
    }
    const deviceAsked = findDevice(false);

    // A device that goes away (its driver started over, or the browser took
    // the GPU back) takes what the program made on it. The page asks for
    // another, and the program, which sees their count change (gpu_epoch),
    // makes everything again from what it keeps (platform/src/platform.c).
    // Meanwhile it draws nothing. The canvas can't go to WebGL: with no device
    // to be had again, it stays as it is.
    let devices = 0; // How many the program has drawn with, this one included
    async function deviceLost(info) {
        printErr('tide: the GPU went away (' + (info.message || info.reason) + '): asking for it again');
        device = null;
        for (const kind in gpuTables) {
            gpuTables[kind] = [null];
            gpuFree[kind] = [];
        }
        bindGroups.clear();
        retired.length = 0;
        uniformBuffer = groupLayouts = pass = commands = null;
        uniformSlots = 0;
        windowTarget.depth = null;
        windowTarget.width = windowTarget.height = 0;
        sharedWrites = null;
        await findDevice(true);
        if (!device) {
            printErr('tide: no GPU to draw with any more: reload the page');
            return;
        }
        windowTarget.context.configure({ device, format: gpuFormat, alphaMode: 'opaque' });
        gpuStart();
        devices++;
    }
    // For tests: loses the device as a GPU going away would, and says how many there have been.
    config.loseDevice = () => { if (device) device.destroy(); };
    config.gpuDevices = () => devices;

    // Whether the device takes bytes straight from memory shared with workers,
    // which the program's is when it runs on threads. Copied out first if not.
    let sharedWrites = null;
    function memoryBytes(ptr, size) {
        const all = u8();
        if (!(all.buffer instanceof ArrayBuffer)) {
            if (sharedWrites === null) {
                const probe = device.createBuffer({ size: 4, usage: GPUBufferUsage.COPY_DST });
                try {
                    device.queue.writeBuffer(probe, 0, all, 0, 4);
                    sharedWrites = true;
                } catch {
                    sharedWrites = false;
                }
                probe.destroy();
            }
            if (!sharedWrites) return [all.slice(ptr, ptr + size), 0];
        }
        return [all, ptr];
    }

    // What every pipeline is bound with: its uniforms, a place in the uniform
    // buffer, and for each texture it samples, in either stage, the texture
    // and how it's sampled. The layouts are by how many textures that is.
    const UNIFORM_SLOT = 256;   // How far apart a pass's uniforms are in the uniform buffer: what WebGPU's offsets go by
    const UNIFORM_BYTES = 4096; // The most a pipeline's uniforms take (TIDE_GPU_MAX_UNIFORMS), which every pipeline is bound
    const MAX_TEXTURES = 8;     // TIDE_GPU_MAX_TEXTURES
    let groupLayouts = null, pipelineLayouts = null, samplers = null;
    function gpuStart() {
        const stages = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
        const entries = [{ binding: 0, visibility: stages, buffer: { type: 'uniform', hasDynamicOffset: true } }];
        groupLayouts = [];
        pipelineLayouts = [];
        for (let count = 0; count <= MAX_TEXTURES; count++) {
            if (count) {
                entries.push({ binding: 2 * count - 1, visibility: stages, texture: { sampleType: 'float' } });
                entries.push({ binding: 2 * count, visibility: stages, sampler: { type: 'filtering' } });
            }
            groupLayouts.push(device.createBindGroupLayout({ entries }));
            pipelineLayouts.push(device.createPipelineLayout({ bindGroupLayouts: [groupLayouts[count]] }));
        }
        samplers = ['nearest', 'linear'].map(filter => device.createSampler({ magFilter: filter, minFilter: filter }));
    }

    // A pass's uniforms go into the uniform buffer one after the other, each
    // taking the slots it needs, written before the pass is handed over. A
    // pass with more of them than it holds gets a bigger one, for its draws
    // from there on. The buffer is longer than its slots by what a pipeline is
    // bound, so that's inside it wherever the uniforms start.
    let uniformBuffer = null, uniformSlots = 0, uniformsUsed = 0, uniformsAt = 0;
    const retired = [];           // Uniform buffers a pass outgrew, which it still draws from
    const bindGroups = new Map(); // By each texture and how it's sampled, or 'plain': for the uniform buffer as it is
    function growUniforms() {
        if (uniformBuffer) retired.push(uniformBuffer);
        uniformSlots = uniformSlots ? uniformSlots * 2 : 64;
        uniformBuffer = device.createBuffer({ size: uniformSlots * UNIFORM_SLOT + UNIFORM_BYTES,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
        uniformsUsed = 0;
        bindGroups.clear();
    }
    function bindGroup(count, textures, blended) {
        let key = 'plain';
        for (let i = 0; i < count; i++) key = (i ? key + ',' : '') + textures[i] + ':' + blended[i];
        let group = bindGroups.get(key);
        if (!group) {
            const entries = [{ binding: 0, resource: { buffer: uniformBuffer, size: UNIFORM_BYTES } }];
            for (let i = 0; i < count; i++) {
                const t = gpuTables.texture[textures[i]];
                entries.push({ binding: 1 + 2 * i, resource: t.view });
                entries.push({ binding: 2 + 2 * i, resource: samplers[blended[i]] });
                t.groups.add(key); // Which go when it does
            }
            group = device.createBindGroup({ layout: groupLayouts[count], entries });
            bindGroups.set(key, group);
        }
        return group;
    }

    // A target: the canvas, or persistent sampleable pixels of its own, each with depth.
    const depthFor = (width, height) => device.createTexture({ size: [width, height], format: 'depth24plus',
        usage: GPUTextureUsage.RENDER_ATTACHMENT });
    const windowTarget = { context: null, depth: null, width: 0, height: 0 };
    function followCanvas() {
        if (windowTarget.width !== canvas.width || windowTarget.height !== canvas.height) {
            if (windowTarget.depth) windowTarget.depth.destroy();
            windowTarget.depth = depthFor(canvas.width, canvas.height);
            windowTarget.width = canvas.width;
            windowTarget.height = canvas.height;
        }
        return windowTarget;
    }
    let reader = null; // A 2D canvas, which a target is drawn into to read it back without waiting

    // tide_gpu_blend: by its alpha, added, what's there times it, or in its place (none)
    const alphaBlend = { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' };
    const blends = [
        { color: alphaBlend, alpha: alphaBlend },
        { color: { srcFactor: 'src-alpha', dstFactor: 'one', operation: 'add' },
          alpha: { srcFactor: 'one', dstFactor: 'one', operation: 'add' } },
        { color: { srcFactor: 'dst', dstFactor: 'zero', operation: 'add' },
          alpha: { srcFactor: 'zero', dstFactor: 'one', operation: 'add' } },
        undefined,
    ];
    const attributeFormats = ['float32', 'float32x2', 'float32x3', 'float32x4', 'unorm8x4',
        'sint32', 'sint32x2', 'sint32x3', 'sint32x4']; // tide_gpu_attribute_format
    let building = null; // The pipeline being described
    let pass = null, commands = null;
    // What the next draw draws with: its pipeline, and each texture it samples, by its slot
    const drawing = { pipeline: null, textures: new Array(MAX_TEXTURES).fill(0), blended: new Array(MAX_TEXTURES).fill(0),
        bound: false };

    function beforeDraw() {
        if (drawing.bound) return;
        pass.setBindGroup(0, bindGroup(drawing.pipeline.textures, drawing.textures, drawing.blended), [uniformsAt]);
        drawing.bound = true;
    }

    // What the browser has to say about a shader comes after it's made: a
    // warning or an error stops a test page, as GL's do (shaderWarning), and
    // the others print it. A test page waits for it before it says the
    // program ended (finish).
    function checkShader(module, name) {
        const check = module.getCompilationInfo().then(info => {
            const said = info.messages.filter(m => m.type !== 'info').map(m => `${m.type}, line ${m.lineNum}: ${m.message}`);
            if (!said.length) return;
            const text = `WebGPU: the shaders of ${name}:\n${said.join('\n')}`;
            if (config.checkGL) fail(new Error(text));
            else printErr('tide: ' + text);
        }, () => {}); // The device went away first: the shaders are made again on the next
        if (config.checkGL) {
            shaderChecks.add(check);
            check.then(() => shaderChecks.delete(check));
        }
    }

    Object.assign(platform, {
        // Which device the program draws with, counted from 1: another once one was lost, and 0 while there's none
        gpu_epoch: () => device && groupLayouts ? devices : 0,
        gpu_buffer(kind, size, ptr) {
            const buffer = device.createBuffer({ size,
                usage: (kind === 0 ? GPUBufferUsage.VERTEX : GPUBufferUsage.INDEX) | GPUBufferUsage.COPY_DST });
            if (ptr) device.queue.writeBuffer(buffer, 0, ...memoryBytes(ptr, size), size);
            return gpuAdd('buffer', buffer);
        },
        gpu_buffer_write: (id, offset, ptr, size) => device.queue.writeBuffer(gpuTables.buffer[id], offset, ...memoryBytes(ptr, size), size),
        gpu_buffer_free(id) {
            gpuTables.buffer[id].destroy();
            gpuRemove('buffer', id);
        },
        gpu_texture(format, width, height, ptr) {
            const texture = device.createTexture({ size: [width, height], format: format === 0 ? 'rgba8unorm' : 'r8unorm',
                usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
            const id = gpuAdd('texture', { texture, view: texture.createView(), width, height, row: width * (format === 0 ? 4 : 1),
                groups: new Set() }); // The bind groups it's in, by their keys
            if (ptr) platform.gpu_texture_write(id, ptr);
            return id;
        },
        gpu_texture_write(id, ptr) {
            const t = gpuTables.texture[id];
            const [data, offset] = memoryBytes(ptr, t.row * t.height);
            device.queue.writeTexture({ texture: t.texture }, data, { offset, bytesPerRow: t.row, rowsPerImage: t.height },
                [t.width, t.height]);
        },
        gpu_texture_free(id) {
            const t = gpuTables.texture[id];
            t.texture.destroy();
            gpuRemove('texture', id);
            for (const key of t.groups) bindGroups.delete(key);
        },
        gpu_pipeline_begin(namePtr, codePtr, uniformSize, textures, blend, depth) {
            building = { name: string(namePtr), code: string(codePtr), textures, blend, depth, slots: [], attributes: [] };
        },
        gpu_pipeline_slot: (stride, perInstance) => { building.slots.push({ stride, perInstance: !!perInstance }); },
        gpu_pipeline_attribute: (slot, format, offset) => { building.attributes.push({ slot, format, offset }); },
        gpu_pipeline_end() {
            const b = building;
            building = null;
            const module = device.createShaderModule({ label: b.name, code: b.code });
            checkShader(module, b.name);
            const buffers = b.slots.map((slot, index) => ({
                arrayStride: slot.stride,
                stepMode: slot.perInstance ? 'instance' : 'vertex',
                attributes: b.attributes.map((a, location) => ({ shaderLocation: location, offset: a.offset,
                    format: attributeFormats[a.format], slot: a.slot })).filter(a => a.slot === index)
                    .map(({ shaderLocation, offset, format }) => ({ shaderLocation, offset, format })),
            }));
            const pipeline = device.createRenderPipeline({
                label: b.name,
                layout: pipelineLayouts[b.textures],
                vertex: { module, entryPoint: 'vertex', buffers },
                fragment: { module, entryPoint: 'fragment', targets: [{ format: gpuFormat,
                    blend: blends[b.blend] }] },
                primitive: { topology: 'triangle-list', cullMode: 'none' }, // Either side of a triangle draws
                depthStencil: { format: 'depth24plus', depthWriteEnabled: b.depth !== 0,
                    depthCompare: b.depth === 1 ? 'less-equal' : 'always' },
            });
            return gpuAdd('pipeline', { pipeline, textures: b.textures });
        },
        gpu_pipeline_free: id => gpuRemove('pipeline', id),
        gpu_target(width, height) {
            const texture = device.createTexture({ size: [width, height], format: gpuFormat,
                usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC });
            const color = gpuAdd('texture', { texture, view: texture.createView(), width, height, row: width * 4,
                groups: new Set() });
            return gpuAdd('target', { color, depth: depthFor(width, height), width, height, canvas: null, context: null });
        },
        gpu_target_limit: () => device.limits.maxTextureDimension2D,
        gpu_target_texture: id => gpuTables.target[id].color,
        gpu_target_free(id) {
            const t = gpuTables.target[id];
            platform.gpu_texture_free(t.color);
            t.depth.destroy();
            if (t.context) t.context.unconfigure();
            gpuRemove('target', id);
        },
        gpu_target_read(id, ptr) {
            const t = gpuTables.target[id];
            // C's readback is synchronous. Copy into a canvas only when it's
            // asked for, then use the browser's existing synchronous canvas read.
            if (!t.context) {
                t.canvas = new OffscreenCanvas(t.width, t.height);
                t.context = t.canvas.getContext('webgpu');
                t.context.configure({ device, format: gpuFormat, alphaMode: 'opaque',
                    usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST });
            }
            const copy = device.createCommandEncoder();
            copy.copyTextureToTexture({ texture: gpuTables.texture[t.color].texture },
                { texture: t.context.getCurrentTexture() }, [t.width, t.height]);
            device.queue.submit([copy.finish()]);
            if (!reader) reader = new OffscreenCanvas(t.width, t.height).getContext('2d', { willReadFrequently: true });
            if (reader.canvas.width !== t.width) reader.canvas.width = t.width;
            if (reader.canvas.height !== t.height) reader.canvas.height = t.height;
            reader.globalCompositeOperation = 'copy';
            reader.drawImage(t.canvas, 0, 0);
            u8().set(reader.getImageData(0, 0, t.width, t.height).data, ptr);
        },
        gpu_begin(id, clear) {
            const t = id ? gpuTables.target[id] : followCanvas();
            if (!uniformBuffer) growUniforms();
            uniformsUsed = 0;
            commands = device.createCommandEncoder();
            pass = commands.beginRenderPass({
                colorAttachments: [{ view: id ? gpuTables.texture[t.color].view : t.context.getCurrentTexture().createView(),
                    loadOp: clear ? 'clear' : 'load',
                    storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } }],
                depthStencilAttachment: { view: t.depth.createView(), depthLoadOp: clear ? 'clear' : 'load',
                    depthStoreOp: 'store', depthClearValue: 1 },
            });
            drawing.pipeline = null;
            drawing.bound = false;
        },
        gpu_end() {
            pass.end();
            device.queue.submit([commands.finish()]);
            pass = commands = null;
            for (const buffer of retired.splice(0)) buffer.destroy();
        },
        gpu_scissor: (x, y, width, height) => pass.setScissorRect(x, y, width, height),
        gpu_use(id) {
            drawing.pipeline = gpuTables.pipeline[id];
            drawing.bound = false;
            pass.setPipeline(drawing.pipeline.pipeline);
        },
        gpu_uniforms(ptr, size) {
            const slots = Math.ceil(size / UNIFORM_SLOT);
            if (uniformsUsed + slots > uniformSlots) growUniforms();
            uniformsAt = uniformsUsed * UNIFORM_SLOT;
            device.queue.writeBuffer(uniformBuffer, uniformsAt, ...memoryBytes(ptr, size), size);
            uniformsUsed += slots;
            drawing.bound = false;
        },
        gpu_sample(slot, id, blended) {
            drawing.textures[slot] = id;
            drawing.blended[slot] = blended ? 1 : 0;
            drawing.bound = false;
        },
        gpu_vertices: (slot, id, offset) => pass.setVertexBuffer(slot, gpuTables.buffer[id], offset),
        gpu_indices: (id, wide) => pass.setIndexBuffer(gpuTables.buffer[id], wide ? 'uint32' : 'uint16'),
        gpu_draw(first, count, instances) {
            beforeDraw();
            pass.draw(count, instances, first, 0);
        },
        gpu_draw_indexed(count, instances) {
            beforeDraw();
            pass.drawIndexed(count, instances, 0, 0, 0);
        },
    });

    // A function the program imports but this file lacks fails when called,
    // with its name, rather than stopping the page from loading.
    const lenient = (module, functions, missing) => new Proxy(functions, {
        get: (target, name) => name in target ? target[name] : (...unused) => missing(module, String(name)),
    });

    // -----------------------------------------------------------------------

    function fail(error) {
        stopped = true;
        failed = true;
        endCursorLock();
        endThreads();
        printErr('tide: ' + (error && error.stack || error));
        broken(error);
        if (config.onAbort) config.onAbort(error);
    }

    // -----------------------------------------------------------------------
    // Threads (wasi-threads). Programs import their memory, which they're built
    // to share between threads. Where the page is cross-origin isolated, it is
    // shared, and each thread the program starts (pthread_create, through
    // thread-spawn) is a worker running the same program on it. Elsewhere,
    // browsers won't share memory: the program gets memory of its own, and runs
    // on one thread (tide_web_threads says 1), with the same results.

    const canShare = typeof SharedArrayBuffer !== 'undefined' && globalThis.crossOriginIsolated === true;
    let module = null;   // The running program, compiled: its threads instantiate it too
    let shared = false;  // Its memory is shared
    let threads = [];    // Its workers
    let nextThread = 1;

    function endThreads() {
        for (const worker of threads) worker.terminate();
        threads = [];
    }

    // The memory a program imports (env.memory): its limits, and where their
    // flags are in its bytes. Read from its import section, since browsers
    // don't say.
    function memoryImport(bytes) {
        let at = 8; // After the magic number and version
        const leb = () => {
            let value = 0, scale = 1, byte;
            do {
                byte = bytes[at++];
                value += (byte & 0x7f) * scale;
                scale *= 128;
            } while (byte & 0x80);
            return value;
        };
        const name = () => {
            const length = leb();
            at += length;
            return decoder.decode(bytes.subarray(at - length, at));
        };
        while (at < bytes.length) {
            const id = bytes[at++];
            const end = leb() + at;
            if (id !== 2) { // Not the imports
                at = end;
                continue;
            }
            for (let count = leb(); count > 0; count--) {
                const from = name(), field = name(), kind = bytes[at++];
                if (kind === 0) leb(); // A function: its type
                else if (kind === 3) at += 2; // A global: its type, and whether it changes
                else if (kind === 4) { at++; leb(); } // A tag
                else { // A table's or a memory's limits, after a table's element type
                    if (kind === 1) at++;
                    const flagsAt = at, flags = bytes[at++];
                    const initial = leb(), maximum = flags & 1 ? leb() : undefined;
                    if (kind === 2 && from === 'env' && field === 'memory') {
                        return { flagsAt, shared: (flags & 2) !== 0, initial, maximum };
                    }
                }
            }
            return null;
        }
        return null;
    }

    // What each thread's worker runs: the program on the page's memory, from
    // wasi_thread_start, with WASI's output, clock and random numbers. What
    // only the page has (WebGL, the canvas, input) fails on a thread.
    function threadMain() {
        onmessage = ({ data: { module, memory, tid, startArg, origin } }) => {
            const decoder = new TextDecoder();
            const view = () => new DataView(memory.buffer);
            const u8 = () => new Uint8Array(memory.buffer);
            const wasi = {
                fd_write(fd, iovs, count, writtenPtr) {
                    let text = '', written = 0;
                    for (let i = 0; i < count; i++) {
                        const ptr = view().getUint32(iovs + i * 8, true), len = view().getUint32(iovs + i * 8 + 4, true);
                        text += decoder.decode(u8().slice(ptr, ptr + len));
                        written += len;
                    }
                    if (fd === 1 || fd === 2) postMessage({ fd, text });
                    view().setUint32(writtenPtr, written, true);
                    return fd === 1 || fd === 2 ? 0 : 8;
                },
                clock_time_get(id, precision, timePtr) {
                    const ms = id === 0 ? Date.now() : performance.timeOrigin + performance.now() - origin; // The page's clock
                    view().setBigUint64(timePtr, BigInt(Math.round(ms * 1e6)), true);
                    return 0;
                },
                random_get(ptr, len) {
                    const bytes = new Uint8Array(len);
                    for (let at = 0; at < len; at += 65536) crypto.getRandomValues(bytes.subarray(at, Math.min(len, at + 65536)));
                    u8().set(bytes, ptr);
                    return 0;
                },
                sched_yield: () => 0,
                proc_exit(code) {
                    postMessage({ exit: code });
                    throw new Error('exit');
                },
            };
            const imports = {};
            for (const { module: from, name, kind } of WebAssembly.Module.imports(module)) {
                imports[from] = imports[from] || {};
                if (kind === 'memory') imports[from][name] = memory;
                else if (from === 'wasi_snapshot_preview1') imports[from][name] = wasi[name] || (() => 52); // ENOSYS
                else imports[from][name] = () => { throw new Error(`${name} only works on the page's own thread`); };
            }
            new WebAssembly.Instance(module, imports).exports.wasi_thread_start(tid, startArg);
        };
    }
    let threadURL = null;

    // thread-spawn: a thread of the running program, as a worker. Its ID, or
    // a negative number when there's none to be had.
    function spawnThread(startArg) {
        if (!shared) return -1;
        try {
            threadURL = threadURL || URL.createObjectURL(new Blob([`(${threadMain})()`], { type: 'text/javascript' }));
            const worker = new Worker(threadURL);
            const tid = nextThread++ & 0x1fffffff;
            worker.onmessage = ({ data }) => {
                if ('exit' in data) {
                    finish(data.exit);
                } else {
                    output(data.fd, data.text);
                }
            };
            worker.onerror = event => fail(event.message || 'a thread failed');
            worker.postMessage({ module, memory, tid, startArg, origin: performance.timeOrigin });
            threads.push(worker);
            return tid;
        } catch (error) {
            printErr('tide: no thread: ' + error);
            return -1;
        }
    }
    // As many as the browser says the machine has processors: both threads of a
    // core that runs two, as it says nothing else. The pool's workers sleep
    // when they have nothing to do (platform/src/jobs.c), so that's no more
    // than the machine can run.
    platform.threads = () => shared ? Math.max(1, Math.min(navigator.hardwareConcurrency || 1, 32)) : 1;

    // Local files live in the page, including while a new build takes over.
    // Load them before main: IndexedDB is asynchronous, whereas a game's
    // whole-file reads and directory queries operate on this memory tree.
    const ioError = { notFound: 1, exists: 2, denied: 3, path: 4, notDirectory: 5,
        notEmpty: 6, space: 7, unavailable: 8, data: 9, failed: 10 };
    const fileTrees = new Map();
    const flushTickets = new Map();
    let filesDB = null, filesError = 0, files = null, nextFlush = 0;
    function storageError(error) {
        return error && error.name === 'QuotaExceededError' ? ioError.space
            : error && error.name === 'SecurityError' ? ioError.denied : ioError.unavailable;
    }
    function fileTree(id) {
        if (!fileTrees.has(id)) {
            const persistent = '/persistent/' + id, temporary = '/temp/' + id;
            fileTrees.set(id, { id, persistent, temporary,
                entries: new Map([[persistent, { directory: true }], [temporary, { directory: true }]]),
                dirty: new Map(), queued: false, chain: Promise.resolve(0) });
        }
        return fileTrees.get(id);
    }
    async function loadFiles() {
        try {
            await new Promise((resolve, reject) => {
                const request = indexedDB.open('tide-files', 1);
                request.onupgradeneeded = () => request.result.createObjectStore('files', { keyPath: ['appId', 'path'] });
                request.onerror = () => reject(request.error);
                request.onblocked = () => reject(new Error('file database is blocked'));
                request.onsuccess = () => {
                    filesDB = request.result;
                    filesDB.onversionchange = () => { filesDB.close(); filesDB = null; filesError = ioError.unavailable; };
                    try {
                        const transaction = filesDB.transaction('files', 'readonly');
                        const cursor = transaction.objectStore('files').openCursor();
                        cursor.onsuccess = () => {
                            const at = cursor.result;
                            if (!at) return;
                            const row = at.value;
                            if (typeof row.appId === 'string' && typeof row.path === 'string') {
                                const tree = fileTree(row.appId);
                                if (row.path.startsWith(tree.persistent + '/'))
                                    tree.entries.set(row.path, row.directory ? { directory: true }
                                        : { directory: false, bytes: new Uint8Array(row.data) });
                            }
                            at.continue();
                        };
                        transaction.oncomplete = resolve;
                        transaction.onabort = () => reject(transaction.error);
                    } catch (error) { reject(error); }
                };
            });
        } catch (error) { filesError = storageError(error); }
    }
    let filesReady = null;
    function saveFiles(tree) {
        const changes = [...tree.dirty];
        tree.chain = tree.chain.then(() => {
            if (!filesDB || filesError) return filesError || ioError.unavailable;
            if (!changes.length) return 0;
            return new Promise(resolve => {
                let transaction;
                try {
                    // The strict hint asks the browser to flush a save before
                    // completing its transaction, where it supports the hint.
                    try { transaction = filesDB.transaction('files', 'readwrite', { durability: 'strict' }); }
                    catch (error) {
                        if (!(error instanceof TypeError)) throw error;
                        transaction = filesDB.transaction('files', 'readwrite');
                    }
                    transaction.oncomplete = () => {
                        for (const [path, change] of changes)
                            if (tree.dirty.get(path) === change) tree.dirty.delete(path);
                        resolve(0);
                    };
                    transaction.onabort = () => resolve(storageError(transaction.error));
                    const store = transaction.objectStore('files');
                    for (const [path, change] of changes) {
                        if (!change.node) store.delete([tree.id, path]);
                        else store.put({ appId: tree.id, path, directory: change.node.directory,
                            data: change.node.directory ? null : change.node.bytes.buffer });
                    }
                } catch (error) {
                    if (transaction) transaction.abort();
                    resolve(storageError(error));
                }
            });
        });
        return tree.chain;
    }
    function changedFile(tree, path, node) {
        if (node) tree.entries.set(path, node); else tree.entries.delete(path);
        if (path !== tree.persistent && !path.startsWith(tree.persistent + '/')) return;
        tree.dirty.set(path, { node });
        if (!tree.queued) {
            tree.queued = true;
            queueMicrotask(() => { tree.queued = false; saveFiles(tree); });
        }
    }
    function filePath(ptr) {
        if (!files) return { error: ioError.unavailable };
        let path = string(ptr).replaceAll('\\', '/');
        if (!path || /^[A-Za-z]:/.test(path)) return { error: ioError.path };
        if (!path.startsWith('/')) path = files.persistent + '/' + path;
        const parts = [];
        for (const part of path.split('/')) {
            if (!part || part === '.') continue;
            if (part === '..') {
                if (!parts.length) return { error: ioError.denied };
                parts.pop();
            } else parts.push(part);
        }
        path = '/' + parts.join('/');
        const persistent = path === files.persistent || path.startsWith(files.persistent + '/');
        if (!persistent && path !== files.temporary && !path.startsWith(files.temporary + '/'))
            return { error: ioError.denied };
        if (persistent && (filesError || !filesDB)) return { error: filesError || ioError.unavailable };
        return { path, error: 0 };
    }
    function fileParent(path) { return path.substring(0, path.lastIndexOf('/')); }
    function checkParent(path) {
        const parent = files.entries.get(fileParent(path));
        return !parent ? ioError.notFound : !parent.directory ? ioError.notDirectory : 0;
    }
    function fileList(ptr, directories, recursive) {
        const found = filePath(ptr);
        if (found.error) return found;
        const node = files.entries.get(found.path);
        if (!node) return { error: ioError.notFound };
        if (!node.directory) return { error: ioError.notDirectory };
        const prefix = found.path + '/';
        const paths = [...files.entries].filter(([path, entry]) => path.startsWith(prefix)
            && !!entry.directory === !!directories && (recursive || !path.slice(prefix.length).includes('/')))
            .map(([path]) => path).sort();
        return { error: 0, bytes: encoder.encode(paths.map(path => path + '\0').join('')) };
    }
    Object.assign(platform, {
        io_init(id) { files = fileTree(string(id)); },
        io_path(kind, out, capacity) {
            if (!files || (kind !== 1 && kind !== 2)) return -1;
            const bytes = encoder.encode(kind === 1 ? files.persistent : files.temporary);
            if (out && capacity > bytes.length) { u8().set(bytes, out); u8()[out + bytes.length] = 0; }
            return bytes.length;
        },
        io_exists(ptr, directory) {
            const found = filePath(ptr);
            const node = !found.error && files.entries.get(found.path);
            return !!node && !!node.directory === !!directory;
        },
        io_create(ptr, directory) {
            const found = filePath(ptr);
            if (found.error) return found.error;
            const old = files.entries.get(found.path);
            if (directory) {
                if (old) return old.directory ? 0 : ioError.notDirectory;
                const parts = found.path.split('/');
                for (let n = 3; n <= parts.length; ++n) {
                    const path = parts.slice(0, n).join('/');
                    const node = files.entries.get(path);
                    if (node && !node.directory) return ioError.notDirectory;
                }
                for (let n = 3; n <= parts.length; ++n) {
                    const path = parts.slice(0, n).join('/');
                    if (!files.entries.has(path)) changedFile(files, path, { directory: true });
                }
                return 0;
            }
            if (old && old.directory) return ioError.failed;
            const error = checkParent(found.path);
            if (!error) changedFile(files, found.path, { directory: false, bytes: new Uint8Array() });
            return error;
        },
        io_read_size(ptr, size) {
            const found = filePath(ptr);
            if (found.error) return found.error;
            const node = files.entries.get(found.path);
            if (!node) return ioError.notFound;
            if (node.directory) return ioError.failed;
            view().setUint32(size, node.bytes.length, true);
            return 0;
        },
        io_read(ptr, out, capacity) {
            const found = filePath(ptr);
            if (found.error) return found.error;
            const node = files.entries.get(found.path);
            if (!node) return ioError.notFound;
            if (node.directory || capacity < node.bytes.length) return ioError.failed;
            u8().set(node.bytes, out);
            return 0;
        },
        io_write(ptr, data, size, append) {
            const found = filePath(ptr);
            if (found.error) return found.error;
            const old = files.entries.get(found.path);
            if (old && old.directory) return ioError.failed;
            const error = checkParent(found.path);
            if (error) return error;
            const bytes = u8().slice(data, data + size);
            let content = bytes;
            if (append && old) {
                content = new Uint8Array(old.bytes.length + bytes.length);
                content.set(old.bytes); content.set(bytes, old.bytes.length);
            }
            changedFile(files, found.path, { directory: false, bytes: content });
            return 0;
        },
        io_copy(source, destination, overwrite) {
            const from = filePath(source), to = filePath(destination);
            if (from.error || to.error) return from.error || to.error;
            const node = files.entries.get(from.path), old = files.entries.get(to.path);
            if (!node) return ioError.notFound;
            if (node.directory || (old && old.directory)) return ioError.failed;
            if (from.path === to.path) return ioError.path;
            if (old && !overwrite) return ioError.exists;
            const error = checkParent(to.path);
            if (!error) changedFile(files, to.path, { directory: false, bytes: node.bytes.slice() });
            return error;
        },
        io_move(source, destination, overwrite, directory) {
            const from = filePath(source), to = filePath(destination);
            if (from.error || to.error) return from.error || to.error;
            const node = files.entries.get(from.path), old = files.entries.get(to.path);
            if (!node) return ioError.notFound;
            if (!!node.directory !== !!directory) return directory ? ioError.notDirectory : ioError.failed;
            if (from.path === files.persistent || from.path === files.temporary) return ioError.denied;
            if (from.path === to.path || to.path.startsWith(from.path + '/')) return ioError.path;
            if (old && (directory || !overwrite)) return ioError.exists;
            if (old && old.directory) return ioError.failed;
            const error = checkParent(to.path);
            if (error) return error;
            const moved = [...files.entries].filter(([path]) => path === from.path || path.startsWith(from.path + '/'));
            for (const [path, entry] of moved) {
                changedFile(files, to.path + path.slice(from.path.length), entry);
                changedFile(files, path, null);
            }
            return 0;
        },
        io_delete(ptr, directory, recursive) {
            const found = filePath(ptr);
            if (found.error) return found.error;
            const node = files.entries.get(found.path);
            if (!node) return directory ? ioError.notFound : 0;
            if (!!node.directory !== !!directory) return directory ? ioError.notDirectory : ioError.failed;
            if (found.path === files.persistent || found.path === files.temporary) return ioError.denied;
            const children = [...files.entries.keys()].filter(path => path.startsWith(found.path + '/'));
            if (children.length && !recursive) return ioError.notEmpty;
            for (const path of children) changedFile(files, path, null);
            changedFile(files, found.path, null);
            return 0;
        },
        io_list_size(ptr, directories, recursive, size) {
            const list = fileList(ptr, directories, recursive);
            if (!list.error) view().setUint32(size, list.bytes.length, true);
            return list.error;
        },
        io_list(ptr, directories, recursive, out, capacity) {
            const list = fileList(ptr, directories, recursive);
            if (list.error) return list.error;
            if (capacity < list.bytes.length) return ioError.failed;
            u8().set(list.bytes, out);
            return 0;
        },
        io_flush_begin() {
            do { nextFlush = (nextFlush + 1) >>> 0; } while (!nextFlush || flushTickets.has(nextFlush));
            const result = { done: false, error: 0 }, ticket = nextFlush;
            flushTickets.set(ticket, result);
            (files ? saveFiles(files) : Promise.resolve(ioError.unavailable)).then(error => {
                result.error = error; result.done = true;
            });
            return ticket;
        },
        io_flush_poll(ticket, error) {
            const result = flushTickets.get(ticket);
            if (!result) { view().setInt32(error, ioError.failed, true); return true; }
            if (!result.done) return false;
            flushTickets.delete(ticket);
            view().setInt32(error, result.error, true);
            return true;
        },
        io_flush_forget(ticket) { flushTickets.delete(ticket); },
    });

    const imports = {
        wasi_snapshot_preview1: lenient('WASI', wasi, () => ENOSYS),
        wasi: { 'thread-spawn': spawnThread },
        tide: platform,
        env: lenient('env', glFunctions, (module, name) => { throw new Error(`tide.js has no ${name}`); }),
    };

    // A program and its memory: shared where the page can share it, or else
    // its own, with the program's import of it changed to match. Compiles on
    // the spot where the browser allows it (Chrome: up to 8 MB on the main
    // thread). The page has nothing else to do meanwhile, and headless tests
    // run on virtual time, which keeps running while a compile happens in the
    // background, so it can run out before main.
    async function instantiate(bytes) {
        const wanted = memoryImport(bytes);
        let made = null, isShared = false;
        if (wanted) {
            const limits = { initial: wanted.initial, maximum: wanted.maximum };
            if (wanted.shared && canShare) {
                try {
                    made = new WebAssembly.Memory({ ...limits, shared: true });
                    isShared = true;
                } catch (error) { // No room for it, as on some phones: one thread
                    printErr('tide: running on one thread: ' + error);
                }
            }
            if (!isShared && wanted.shared) {
                bytes = bytes.slice();
                bytes[wanted.flagsAt] &= ~2;
            }
            made = made || new WebAssembly.Memory(limits);
        }
        const env = lenient('env', { ...glFunctions, memory: made }, (from, name) => { throw new Error(`tide.js has no ${name}`); });
        const all = { ...imports, env };
        let compiled;
        try {
            compiled = new WebAssembly.Module(bytes);
        } catch (error) {
            if (!(error instanceof RangeError)) throw error;
            compiled = await WebAssembly.compile(bytes);
        }
        let instance;
        try {
            instance = new WebAssembly.Instance(compiled, all);
        } catch (error) {
            if (!(error instanceof RangeError)) throw error;
            instance = await WebAssembly.instantiate(compiled, all);
        }
        // Games without file access start without opening browser storage.
        // Standalone C hosts have no generated predicate: their imports say
        // whether they use the filesystem provider.
        const needsFiles = instance.exports.tide_uses_io ? instance.exports.tide_uses_io()
            : WebAssembly.Module.imports(compiled).some(i => i.module === 'tide' && i.name === 'io_init');
        if (needsFiles) {
            filesReady = filesReady || loadFiles();
            await filesReady;
        }
        return { instance, module: compiled, shared: isShared };
    }

    // Runs a program's main, which returns only through exit(), or by starting
    // the frame loop.
    function begin(program) {
        endThreads();
        module = program.module;
        shared = program.shared;
        const instance = program.instance;
        exports = instance.exports;
        memory = exports.memory;
        try {
            exports._start();
        } catch (error) {
            if (error !== unwind && !(error instanceof Exit)) fail(error);
        }
    }

    // The program's bytes, from the base64 the page holds them as.
    function programBytes(text) {
        if (Uint8Array.fromBase64) return Uint8Array.fromBase64(text);
        const binary = atob(text);
        const bytes = new Uint8Array(binary.length);
        for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
        return bytes;
    }

    // The program loads while the page waits to hear about its GPU, and starts
    // once it knows; instantiate loads its saved files too when the program
    // uses them.
    const load = async () => instantiate(programBytes(TIDE_PROGRAM));
    const loaded = typeof TIDE_PROGRAM === 'string' && !config.reload
        ? load().then(program => ({ program }), error => ({ error })) : null;
    await deviceAsked;

    // <tide run>
    // Hot reloading: tide run --web serves the page, and says which build is
    // the newest. Each new one starts in the running one's place, carrying
    // over what the running one leaves it (platform/src/reload.c).
    let resumeWith = null;
    platform.resume_size = () => resumeWith ? resumeWith.length : 0;
    platform.resume_copy = ptr => u8().set(resumeWith, ptr);

    // What the running program made in WebGL goes with it.
    function forgetGL() {
        const destroy = {
            buffer: o => gl.deleteBuffer(o), texture: o => gl.deleteTexture(o),
            framebuffer: o => gl.deleteFramebuffer(o), renderbuffer: o => gl.deleteRenderbuffer(o),
            vertexArray: o => gl.deleteVertexArray(o), program: o => gl.deleteProgram(o),
            shader: o => gl.deleteShader(o), uniform: () => {}, sampler: o => gl.deleteSampler(o),
        };
        for (const kind in tables) {
            for (const object of tables[kind]) if (object) destroy[kind](object);
            tables[kind] = [null];
        }
        uniformIds.clear();
    }

    // ...and what it made in WebGPU.
    function forgetGPU() {
        for (const buffer of gpuTables.buffer) if (buffer) buffer.destroy();
        for (const t of gpuTables.texture) if (t) t.texture.destroy();
        for (const t of gpuTables.target) if (t) t.depth.destroy();
        for (const kind in gpuTables) {
            gpuTables[kind] = [null];
            gpuFree[kind] = [];
        }
        bindGroups.clear();
    }

    // Starts `bytes` in the running program's place: fresh, or going on from
    // where it is.
    async function replace(bytes, fresh) {
        const program = await instantiate(bytes);
        resumeWith = null;
        if (!fresh && exports && exports.tide_reload_save && !stopped) {
            try {
                const at = exports.tide_reload_save();
                if (at) resumeWith = u8().slice(at, at + view().getUint32(at, true));
            } catch (error) {
                printErr('tide: the running build could not hand over its state: ' + error);
            }
        }
        if (gl) forgetGL();
        if (device) forgetGPU();
        closeRoom(); // The new build plays on alone, like any web game for now
        stopped = failed = false;
        begin(program);
        resumeWith = null;
    }

    // Asks tide for the newest build, and whether to start over, 4 times a
    // second. When tide has stopped, the page stays as it is.
    if (config.reload) {
        let build = 0, restarts = 0, bytes = null;
        const poll = async () => {
            try {
                const [newest, restart] = (await (await fetch('build', { cache: 'no-store' })).text())
                    .trim().split(' ').map(Number);
                if (newest && newest !== build) {
                    const response = await fetch(`game-${newest}.wasm`, { cache: 'no-store' });
                    if (response.ok) {
                        const next = new Uint8Array(await response.arrayBuffer());
                        const fresh = !bytes;
                        build = newest;
                        restarts = restart;
                        bytes = next;
                        await replace(bytes, fresh);
                    }
                } else if (bytes && restart !== restarts) {
                    restarts = restart;
                    await replace(bytes, true);
                }
            } catch (error) {
                if (!(error instanceof TypeError)) printErr('tide: ' + (error && error.stack || error)); // TypeError: tide isn't there
            }
            setTimeout(poll, 250);
        };
        poll();
        return;
    }
    // </tide run>

    const { program, error } = await loaded || { error: new Error('the page has no program (TIDE_PROGRAM)') };
    if (error) {
        fail(error);
        return;
    }
    await released; // At once, unless the page said to wait for it
    begin(program);
})();
