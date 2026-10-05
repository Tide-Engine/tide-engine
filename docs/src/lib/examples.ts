// What the game's page tells the page around it (platform/web/tide.js).
interface GamePage {
    /** The code of the room the game hosts or joined, "" for none. */
    room?: string;
    /** Milliseconds its packets to the room's players leave late. */
    lag?: number;
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

/** Attach controls to the live examples rendered from Markdown. */
export function liveExamples(article: HTMLElement) {
    const cleanups = [...article.querySelectorAll<HTMLElement>('.tide-example')].map((example) => {
        const run = example.querySelector<HTMLButtonElement>('[data-example-run]')!;
        const stop = example.querySelector<HTMLButtonElement>('[data-example-stop]')!;
        const viewports = [...example.querySelectorAll<HTMLElement>('.example-viewport')];
        const status = example.querySelector<HTMLElement>('.example-status')!;
        const lag = example.querySelector<HTMLInputElement>('[data-example-lag]');
        const lagText = lag?.parentElement?.querySelector('output');
        // One frame is a game of its own. Two are two machines: in a match
        // (the first hosts, the second joins its room), or each as it starts.
        const match = example.dataset.exampleMatch !== undefined;
        const begin = viewports.length > 1 ? 'Run both' : 'Run example';
        let request: AbortController | undefined;
        let frames: HTMLIFrameElement[] = [];
        // The reader stopped it, or it can't run: it waits for the button.
        let held = false;

        function reset(message: string) {
            request?.abort();
            request = undefined;
            // Removing the document ends its frame loop and workers too.
            for (const viewport of viewports) viewport.replaceChildren();
            frames = [];
            status.hidden = false;
            status.textContent = message;
            run.textContent = begin;
            run.disabled = false;
            stop.disabled = true;
        }

        function latency() {
            return lag ? Number(lag.value) : 0;
        }

        // A game's page, with what this player's frame says in its address.
        // `backend=` in this page's own address goes on to its games.
        function page(options: Record<string, string>) {
            const url = new URL(example.dataset.exampleSrc!, location.href);
            const backend = new URLSearchParams(location.search).get('backend');
            if (backend) url.searchParams.set('backend', backend);
            for (const [name, value] of Object.entries(options)) url.searchParams.set(name, value);
            if (latency()) url.searchParams.set('lag', String(latency()));
            return url.href;
        }

        function open(viewport: HTMLElement, player: number, src: string, current: AbortController) {
            const frame = document.createElement('iframe');
            const title = example.getAttribute('aria-label')!;
            frame.title = viewports.length > 1 ? `${title}: player ${player + 1}` : title;
            frame.src = src;
            frame.allow = 'fullscreen';
            const loaded = new Promise<void>((done) => frame.addEventListener('load', () => done(), { once: true }));
            viewport.replaceChildren(frame);
            frames.push(frame);
            return loaded.then(() => !current.signal.aborted);
        }

        // The code of the room the first player's game hosts, once it has one.
        function hosted(frame: HTMLIFrameElement, current: AbortController) {
            return new Promise<string>((done) => {
                const started = performance.now();
                const look = () => {
                    if (current.signal.aborted) return done('');
                    const code = game(frame)?.room;
                    if (code) return done(code);
                    if (performance.now() - started > 8000) {
                        status.textContent = 'Player 1 can\'t reach the relay that introduces players. Still trying…';
                    }
                    setTimeout(look, 100);
                };
                look();
            });
        }

        async function start() {
            held = false;
            reset(viewports.length > 1 ? 'Loading both players…' : 'Loading example…');
            const current = new AbortController();
            request = current;
            run.disabled = true;
            stop.disabled = false;
            try {
                // A docs-only checkout may have no compiled web games. Report
                // that here instead of showing a 404 page inside the frame.
                const response = await fetch(page({}), { method: 'HEAD', signal: current.signal });
                if (!response.ok) throw new Error('Example unavailable');
                if (current.signal.aborted) return;
                if (match) {
                    status.textContent = 'Player 1 is opening a room…';
                    if (!(await open(viewports[0], 0, page({ host: '' }), current))) return;
                    const code = await hosted(frames[0], current);
                    if (!code) return;
                    status.textContent = `Player 2 is joining room ${code}…`;
                    if (!(await open(viewports[1], 1, page({ join: code }), current))) return;
                } else {
                    const loaded = await Promise.all(viewports.map((viewport, i) => open(viewport, i, page({}), current)));
                    if (loaded.includes(false)) return;
                }
                status.hidden = true;
                run.textContent = 'Restart';
                run.disabled = false;
            } catch {
                if (current.signal.aborted) return;
                held = true; // Not one to try again each time it scrolls by
                reset('This example is unavailable in this build of the docs. You can still read and copy its source.');
            }
        }

        // The slider changes what's running, and what starts next.
        function delay() {
            if (lagText) lagText.textContent = `${latency()} ms`;
            for (const frame of frames) {
                const page = game(frame);
                if (page) page.lag = latency();
            }
        }

        function halt() {
            held = true;
            reset('Example stopped. Run it again to start over.');
        }

        // In view: half of where it draws is on screen, and still is a moment
        // later, so scrolling past starts nothing. Out of it: more than a
        // screen away, so reading the code beside it doesn't end it.
        const where = viewports[0].parentElement!;
        let seen = false;
        let dwell: ReturnType<typeof setTimeout> | undefined;
        const arrive = new IntersectionObserver(([entry]) => {
            seen = entry.isIntersecting;
            clearTimeout(dwell);
            if (seen && !request && !held && eager()) {
                dwell = setTimeout(() => {
                    if (seen && !request && !held) start();
                }, 300);
            }
        }, { threshold: 0.5 });
        const leave = new IntersectionObserver(([entry]) => {
            if (!entry.isIntersecting && request) reset('It stopped while it was off screen, and starts again here.');
        }, { rootMargin: '100% 0px' });
        arrive.observe(where);
        leave.observe(where);

        run.addEventListener('click', start);
        stop.addEventListener('click', halt);
        lag?.addEventListener('input', delay);
        return () => {
            arrive.disconnect();
            leave.disconnect();
            clearTimeout(dwell);
            reset('');
            run.removeEventListener('click', start);
            stop.removeEventListener('click', halt);
            lag?.removeEventListener('input', delay);
        };
    });
    return () => cleanups.forEach((cleanup) => cleanup());
}
