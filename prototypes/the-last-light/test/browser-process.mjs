import { spawn } from 'node:child_process';

// A child process can exit before its stdio closes: Chrome's helpers may
// inherit stderr. Wait for the process, then release our pipe, rather than
// mistake an inherited pipe for a browser that ignored Browser.close.
export async function stopBrowser(browser, requestClose, milliseconds = 2000) {
    const stopped = () => !browser.pid || browser.exitCode !== null || browser.signalCode !== null;
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
            await within(Promise.resolve().then(requestClose).catch(() => {}));
            await waitForExit();
        }
        if (!stopped()) {
            if (process.platform === 'win32') {
                const kill = spawn('taskkill', ['/PID', String(browser.pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
                kill.on('error', error => { console.error(`Could not stop browser tree: ${error}`); browser.kill('SIGKILL'); });
                kill.on('exit', code => { if (code !== 0 && !stopped()) browser.kill('SIGKILL'); });
                kill.unref();
            } else browser.kill('SIGKILL');
            await waitForExit();
        }
        if (stopped()) browser.stderr?.destroy();
        return stopped();
    } finally {
        browser.removeListener('exit', exited);
    }
}
