/** Attach controls to the live examples rendered from Markdown. */
export function liveExamples(article: HTMLElement) {
    const cleanups = [...article.querySelectorAll<HTMLElement>('.tide-example')].map((example) => {
        const run = example.querySelector<HTMLButtonElement>('[data-example-run]')!;
        const stop = example.querySelector<HTMLButtonElement>('[data-example-stop]')!;
        const viewport = example.querySelector<HTMLElement>('.example-viewport')!;
        const status = example.querySelector<HTMLElement>('.example-status')!;
        let request: AbortController | undefined;

        function reset(message: string) {
            request?.abort();
            request = undefined;
            // Removing the document ends its frame loop and workers too.
            viewport.replaceChildren();
            status.hidden = false;
            status.textContent = message;
            run.textContent = 'Run example';
            run.disabled = false;
            stop.disabled = true;
        }

        async function start() {
            reset('Loading example…');
            const current = new AbortController();
            request = current;
            run.disabled = true;
            stop.disabled = false;
            const src = example.dataset.exampleSrc!;
            try {
                // A docs-only checkout may have no compiled web games. Report
                // that here instead of showing a 404 page inside the frame.
                const response = await fetch(src, { method: 'HEAD', signal: current.signal });
                if (!response.ok) throw new Error('Example unavailable');
                if (current.signal.aborted) return;
                const frame = document.createElement('iframe');
                frame.title = example.getAttribute('aria-label')!;
                frame.src = src;
                frame.allow = 'fullscreen';
                frame.addEventListener('load', () => {
                    if (current.signal.aborted) return;
                    status.hidden = true;
                    run.textContent = 'Restart';
                    run.disabled = false;
                }, { once: true });
                viewport.replaceChildren(frame);
            } catch {
                if (current.signal.aborted) return;
                reset('This example is unavailable in this build of the docs. You can still read and copy its source.');
            }
        }

        const halt = () => reset('Example stopped. Run it again to start over.');
        run.addEventListener('click', start);
        stop.addEventListener('click', halt);
        return () => {
            reset('');
            run.removeEventListener('click', start);
            stop.removeEventListener('click', halt);
        };
    });
    return () => cleanups.forEach((cleanup) => cleanup());
}
