import { spawn } from 'node:child_process';

// A child process can exit before its stdio closes: Chrome's helpers may
// inherit stderr. Wait for the process, then release our pipe, rather than
// mistake an inherited pipe for a browser that ignored Browser.close.
export async function stopBrowser(browser, requestClose, milliseconds = 2000) {
    const hasExited = child => !child.pid || child.exitCode !== null || child.signalCode !== null;
    const stopped = () => hasExited(browser);
    let exited;
    const exit = new Promise(resolve => { exited = resolve; });
    browser.once('exit', exited);
    const within = async promise => {
        let timer;
        try {
            await Promise.race([promise, new Promise(resolve => { timer = setTimeout(resolve, milliseconds); })]);
        } finally {
            clearTimeout(timer);
        }
    };
    const waitForExit = async () => {
        if (!stopped()) await within(exit);
    };
    try {
        if (!stopped()) {
            // The exit deadline includes sending the close request, which
            // may never reply even if the browser has already stopped.
            void Promise.resolve().then(requestClose).catch(() => {});
            await waitForExit();
        }
        if (!stopped()) {
            if (process.platform === 'win32') {
                const kill = spawn('taskkill', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
                const killed = new Promise(resolve => {
                    kill.once('error', error => { console.error(`Could not stop browser tree: ${error}`); resolve(); });
                    kill.once('exit', resolve);
                });
                await within(killed);
                // taskkill itself can stall. Stop it before falling back so
                // it cannot outlive cleanup and later target a reused PID.
                if (!hasExited(kill)) kill.kill('SIGKILL');
                if (!stopped()) browser.kill('SIGKILL');
                await within(Promise.all([exit, killed]));
                if (!hasExited(kill)) {
                    console.error(`Browser tree killer did not stop: ${kill.pid}`);
                    kill.unref();
                    return false;
                }
            } else {
                browser.kill('SIGKILL');
                await waitForExit();
            }
        }
        if (stopped()) browser.stderr?.destroy();
        return stopped();
    } finally {
        browser.removeListener('exit', exited);
    }
}
