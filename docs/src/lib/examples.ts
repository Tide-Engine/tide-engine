// What the game's page tells the page around it (platform/web/tide.js).
interface GamePage {
    /** The code of the room the game hosts or joined, "" for none. */
    room?: string;
    /** The ping its player is to have: the network's round trip to the match's server, in milliseconds. */
    lag?: number;
    /** The least that can be: the ping while nothing was added, once the game measured it so. */
    minLag?: number;
    /** Starts a program that waited for it. Done once its first frame is drawn. */
    start?: () => Promise<void>;
}

function game(frame: HTMLIFrameElement): GamePage | undefined {
    try {
        return (frame.contentWindow as (Window & { Tide?: GamePage }) | null)?.Tide;
    } catch {
        return undefined; // Not this site's page, so not a game of ours
    }
}

// An example starts by itself once it's in view, and stops once it's well out
// of it, so a page runs what its reader is looking at and nothing else. A
// reader who asked for less motion or less data starts each one themselves.
function eager() {
    const saving = (navigator as Navigator & { connection?: { saveData?: boolean } }).connection?.saveData;
    return !saving && !matchMedia('(prefers-reduced-motion: reduce)').matches;
}

// A frame shows when its game has the focus (app.css), which is when the
// keyboard is the game's, and a touchscreen's swipes on it.
function showsFocus(frame: HTMLIFrameElement) {
    try {
        const inner = frame.contentWindow!;
        inner.addEventListener('focus', () => frame.classList.add('focused'));
        inner.addEventListener('blur', () => frame.classList.remove('focused'));
    } catch {
        // Not this site's page
    }
}

const unavailable = 'This example is unavailable in this build of the docs. You can still read and copy its source.';

/** Attach controls to the live examples rendered from Markdown. */
export function liveExamples(article: HTMLElement) {
    // A game has the keyboard, and a touchscreen's swipes, from a press on it
    // until one on the page around it, which takes the focus back from its
    // frame (tide.js: `embedded`). Browsers don't all do that by themselves
    // for a press on text.
    const elsewhere = () => {
        const focused = document.activeElement;
        if (focused instanceof HTMLIFrameElement && article.contains(focused)) focused.blur();
    };
    document.addEventListener('pointerdown', elsewhere, true);

    // The whole games a page shows in frames of its own (Markdown's iframes).
    for (const frame of article.querySelectorAll<HTMLIFrameElement>('iframe.demo-frame')) {
        frame.addEventListener('load', () => showsFocus(frame));
        if (game(frame)) showsFocus(frame); // It loaded before this ran
    }

    const examples = [...article.querySelectorAll<HTMLElement>('.tide-example')].map((example) => {
        const run = example.querySelector<HTMLButtonElement>('[data-example-run]')!;
        const stop = example.querySelector<HTMLButtonElement>('[data-example-stop]')!;
        const viewports = [...example.querySelectorAll<HTMLElement>('.example-viewport')];
        const status = example.querySelector<HTMLElement>('.example-status')!;
        const lag = example.querySelector<HTMLInputElement>('[data-example-lag]');
        const lagText = lag?.parentElement?.querySelector('output');
        const lagMin = example.querySelector<HTMLElement>('.example-lag-min');
        // One frame is a game of its own. Two are two machines: in a match
        // (the first hosts, the second joins its room), or each as it starts.
        const match = example.dataset.exampleMatch !== undefined;
        const pair = viewports.length > 1;
        const begin = pair ? 'Run both' : 'Run example';
        // An example goes through three states: loaded, its programs waiting
        // (`request` and `ready`), then started (`started`), then shown, each
        // frame once its game drew. It loads before it's in view, so that
        // starting it there takes no time anyone sees.
        let request: AbortController | undefined;
        let ready: Promise<GamePage[] | null> | undefined;
        let started = false;
        let frames: HTMLIFrameElement[] = [];
        // The reader stopped it, or it can't run: it waits for the button.
        let held = false;
        // The least ping written beside the slider, what the last look said
        // it was, and what looks for it.
        let least: number | undefined;
        let other: number | undefined;
        let looking: ReturnType<typeof setInterval> | undefined;

        // What the reader is told, where the game will be. What's only
        // `loading` shows after a moment (app.css), so an example that comes
        // up at once says nothing.
        function say(message: string, loading = false) {
            status.textContent = message;
            status.hidden = !message;
            status.classList.toggle('loading', loading);
        }

        function reset(message: string) {
            request?.abort();
            request = undefined;
            ready = undefined;
            started = false;
            // Removing the document ends its frame loop and workers too.
            for (const viewport of viewports) viewport.replaceChildren();
            frames = [];
            clearInterval(looking);
            least = undefined;
            other = undefined;
            if (lagMin) lagMin.hidden = true;
            say(message);
            run.textContent = begin;
            run.disabled = false;
            stop.disabled = true;
        }

        function latency() {
            return lag ? Number(lag.value) : 0;
        }

        // A game's page, with what this player's frame says in its address:
        // it's part of this page, which scrolls. `backend=` in this page's own
        // address goes on to its games.
        function page(options: Record<string, string>) {
            const url = new URL(example.dataset.exampleSrc!, location.href);
            const backend = new URLSearchParams(location.search).get('backend');
            if (backend) url.searchParams.set('backend', backend);
            url.searchParams.set('embedded', '');
            for (const [name, value] of Object.entries(options)) url.searchParams.set(name, value);
            return url.href;
        }

        // A player's frame, which stays unseen until its game has drawn. Gives
        // the game's page once it loaded, or nothing if the example was
        // stopped meanwhile. A docs-only checkout may have no compiled web
        // games: what loads then isn't a game, and nobody sees it.
        function open(viewport: HTMLElement, player: number, src: string, current: AbortController) {
            const frame = document.createElement('iframe');
            const title = example.getAttribute('aria-label')!;
            frame.title = pair ? `${title}: player ${player + 1}` : title;
            frame.src = src;
            frame.allow = 'fullscreen';
            const loaded = new Promise<void>((done) => frame.addEventListener('load', () => done(), { once: true }));
            viewport.replaceChildren(frame);
            frames[player] = frame;
            return loaded.then(() => {
                if (current.signal.aborted) return null;
                const page = game(frame);
                if (typeof page?.start !== 'function') throw new Error('Not a game');
                showsFocus(frame);
                return page;
            });
        }

        // Starts a frame's program, and shows the frame once it has drawn.
        async function show(player: number, page: GamePage, current: AbortController) {
            await page.start!();
            if (!current.signal.aborted) frames[player].classList.add('shown');
        }

        // The code of the room the first player's game hosts, once it has one.
        function hosted(frame: HTMLIFrameElement, current: AbortController) {
            return new Promise<string>((done) => {
                const began = performance.now();
                const look = () => {
                    if (current.signal.aborted) return done('');
                    const code = game(frame)?.room;
                    if (code) return done(code);
                    if (performance.now() - began > 8000) {
                        say('Player 1 can\'t reach the relay that introduces players. Still trying…');
                    }
                    setTimeout(look, 100);
                };
                look();
            });
        }

        // The page the frames that load first show: a match's first player
        // hosts it.
        const address = () => page(match ? { wait: '', host: '' } : { wait: '' });

        // Loads the frames, their programs waiting to be started. In a match
        // that's the first player's: the second's address has the room's code.
        function load() {
            if (request) return;
            const current = new AbortController();
            request = current;
            const first = match ? viewports.slice(0, 1) : viewports;
            ready = Promise.all(first.map((viewport, i) => open(viewport, i, address(), current)))
                .then((pages) => (pages.includes(null) ? null : (pages as GamePage[])))
                .catch(() => {
                    if (current.signal.aborted) return null;
                    held = true; // Not one to try again each time it scrolls by
                    reset(unavailable);
                    return null;
                });
        }

        async function start() {
            if (started) return;
            held = false;
            load();
            const current = request!;
            started = true;
            run.disabled = true;
            stop.disabled = false;
            if (lag) looking = setInterval(floor, 500);
            say(match ? 'Player 1 is opening a room…' : pair ? 'Loading both players…' : 'Loading example…', true);
            try {
                const pages = await ready!;
                if (!pages || current.signal.aborted) return;
                if (match) {
                    await show(0, pages[0], current);
                    const code = await hosted(frames[0], current);
                    if (!code) return;
                    say(`Player 2 is joining room ${code}…`, true);
                    const joining = await open(viewports[1], 1, page({ join: code }), current);
                    if (!joining) return;
                    await show(1, joining, current);
                } else {
                    await Promise.all(pages.map((page, i) => show(i, page, current)));
                }
                if (current.signal.aborted) return;
                say('');
                run.textContent = 'Restart';
                run.disabled = false;
            } catch {
                if (current.signal.aborted) return;
                held = true;
                reset(unavailable);
            }
        }

        function restart() {
            if (started) reset('');
            start();
        }

        // The slider changes what's running. Each page is told the same ping:
        // the one that hosts has no trip to the server and adds nothing, and
        // the one that joined makes its round trip that. A match starts with
        // none, until the least there can be is known (below).
        function delay() {
            if (lagText) lagText.textContent = `${latency()} ms`;
            for (const frame of frames) {
                const page = frame && game(frame);
                if (page) page.lag = least === undefined ? 0 : latency();
            }
            lagMin?.classList.toggle('limits', least !== undefined && latency() < least);
        }

        // The least the slider can make the ping, written beside it: the ping
        // the game that joined measured while its page added nothing, which
        // is the one its player has while the slider is under it. So a match's
        // first moments are without the slider's ping, which starts once
        // that's known. It can differ by a millisecond from one look to the
        // next, so what's written changes once two looks in a row say the
        // same other number.
        function floor() {
            const known = frames.map((frame) => frame && game(frame)?.minLag).filter((min) => typeof min === 'number');
            const min = known.length ? Math.max(...known) : undefined;
            const said = other;
            other = min;
            if (min === undefined || min === least || (least !== undefined && min !== said)) return;
            least = min;
            if (lagMin) {
                lagMin.textContent = `min ${min} ms`;
                lagMin.hidden = false;
            }
            delay();
        }

        function halt() {
            held = true;
            reset('Example stopped. Run it again to start over.');
        }

        // Near: within a screen of where it draws, where it loads, and past
        // which it stops, so reading the code beside it doesn't end it. In
        // view: half of where it draws is on screen, and still is a moment
        // later, so scrolling past starts nothing.
        const where = viewports[0].parentElement!;
        let seen = false;
        let dwell: ReturnType<typeof setTimeout> | undefined;
        const near = new IntersectionObserver(([entry]) => {
            if (entry.isIntersecting) {
                if (!held && eager()) load();
            } else if (request) {
                reset(eager() ? '' : 'It stopped while it was off screen. Run it again to start over.');
            }
        }, { rootMargin: '100% 0px' });
        const arrive = new IntersectionObserver(([entry]) => {
            seen = entry.isIntersecting;
            clearTimeout(dwell);
            if (seen && !started && !held && eager()) {
                dwell = setTimeout(() => {
                    if (seen && !started && !held) start();
                }, 300);
            }
        }, { threshold: 0.5 });
        near.observe(where);
        arrive.observe(where);
        if (!eager()) say(pair ? 'Run the example to start both players here.' : 'Run the example to try it here.');

        run.addEventListener('click', restart);
        stop.addEventListener('click', halt);
        lag?.addEventListener('input', delay);
        return {
            where,
            // The page to download ahead (below), unless the example is
            // loading it itself already, or waits for its button.
            ahead: () => (request || held || !eager() ? null : address()),
            // Done once the frames it's loading itself have loaded.
            loading: () => ready,
            cleanup() {
                near.disconnect();
                arrive.disconnect();
                clearTimeout(dwell);
                reset('');
                run.removeEventListener('click', restart);
                stop.removeEventListener('click', halt);
                lag?.removeEventListener('input', delay);
            },
        };
    });

    // Once this page has loaded and has nothing else to do, the browser
    // downloads its examples' pages, nearest the view first and one at a time,
    // each after the examples near the view have loaded theirs: those are what
    // the reader is waiting for. So an example the reader gets to, however
    // fast, has no download left: its frame asks the server whether its page
    // changed, and has it from the browser's cache.
    //
    // Only a frame's own load puts a page where a frame finds it: browsers
    // keep a frame's page apart from what a script fetches or a link
    // prefetches. So each downloads in a frame nobody sees, where nothing
    // runs: `#download` tells the page so (tide.js), and what's after the `#`
    // is no part of the address the cache goes by.
    let downloading: HTMLIFrameElement | undefined;
    let over = false;
    async function download() {
        const middle = innerHeight / 2;
        const far = new Map(examples.map((example) => {
            const box = example.where.getBoundingClientRect();
            return [example, Math.abs((box.top + box.bottom) / 2 - middle)];
        }));
        const got = new Set<string>();
        for (const example of [...examples].sort((a, b) => far.get(a)! - far.get(b)!)) {
            await Promise.all(examples.map((near) => near.loading()));
            if (over) return;
            const url = example.ahead();
            if (!url || got.has(url)) continue;
            got.add(url);
            const frame = document.createElement('iframe');
            frame.hidden = true;
            const loaded = new Promise<void>((done) => frame.addEventListener('load', () => done(), { once: true }));
            frame.src = `${url}#download`;
            downloading = frame;
            document.body.append(frame);
            await loaded;
            const found = game(frame) !== undefined;
            frame.remove();
            downloading = undefined;
            if (!found) return; // A build of the docs without its games
        }
    }
    const idle = window.requestIdleCallback?.bind(window) ?? ((run: () => void) => setTimeout(run, 200));
    const loaded = () => idle(() => void download());
    if (document.readyState === 'complete') loaded();
    else addEventListener('load', loaded, { once: true });

    return () => {
        over = true;
        downloading?.remove();
        removeEventListener('load', loaded);
        document.removeEventListener('pointerdown', elsewhere, true);
        examples.forEach((example) => example.cleanup());
    };
}
