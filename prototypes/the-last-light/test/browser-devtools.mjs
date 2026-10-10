// This fixture uses Chrome's full-duplex DevTools pipe, as the served web
// tests do. Each player's flattened session shares globally unique command
// IDs; events stay with that player's session, including across navigation.
export function connectDevTools(browser) {
    const input = browser.stdio[3], output = browser.stdio[4];
    const pending = new Map(), sessions = new Map(), detached = new Map(), disconnects = new Set();
    let next = 0, unread = '', closed;
    const fail = error => {
        if (closed) return;
        closed = error;
        for (const entry of pending.values()) {
            clearTimeout(entry.timer);
            entry.reject(error);
        }
        pending.clear();
        for (const listener of disconnects) listener(error);
    };
    const exited = (code, signal) => fail(new Error(`Browser stopped (${signal ?? code})`));
    const ended = () => fail(new Error('DevTools pipe closed'));
    const failed = error => fail(error);
    const received = data => {
        if (closed) return;
        unread += data;
        for (let end; !closed && (end = unread.indexOf('\0')) >= 0;) {
            const text = unread.slice(0, end);
            unread = unread.slice(end + 1);
            let message;
            try { message = JSON.parse(text); }
            catch (error) { fail(new Error(`Invalid DevTools message: ${error.message}`)); return; }
            if (message.id) {
                const entry = pending.get(message.id);
                if (!entry) continue; // A reply after its command timed out.
                pending.delete(message.id);
                clearTimeout(entry.timer);
                if (message.error) entry.reject(new Error(JSON.stringify(message.error)));
                else entry.resolve(message.result);
            } else if (message.method === 'Target.detachedFromTarget') {
                const sessionId = message.params.sessionId;
                const error = new Error(`DevTools session detached (${sessionId})`);
                detached.set(sessionId, error);
                for (const [id, entry] of pending) if (entry.sessionId === sessionId) {
                    pending.delete(id);
                    clearTimeout(entry.timer);
                    entry.reject(error);
                }
                for (const listener of disconnects) listener(error);
            } else {
                for (const listener of sessions.get(message.sessionId) ?? []) listener(message);
            }
        }
    };
    // The stream decoder retains partial UTF-8 characters between reads.
    output.setEncoding('utf8');
    output.on('data', received);
    output.on('end', ended);
    output.on('close', ended);
    output.on('error', failed);
    input.on('error', failed);
    browser.on('error', failed);
    browser.on('exit', exited);
    return {
        command(method, params = {}, { sessionId, timeout = 20_000 } = {}) {
            return new Promise((resolve, reject) => {
                if (closed) { reject(closed); return; }
                if (detached.has(sessionId)) { reject(detached.get(sessionId)); return; }
                const id = ++next;
                const timer = setTimeout(() => {
                    pending.delete(id);
                    const error = new Error(`Timed out ${method}`);
                    error.code = 'ETIMEDOUT';
                    reject(error);
                }, timeout);
                pending.set(id, { resolve, reject, timer, sessionId });
                try {
                    input.write(JSON.stringify({ id, method, params, ...(sessionId ? { sessionId } : {}) }) + '\0', error => {
                        if (error) fail(error);
                    });
                } catch (error) { fail(error); }
            });
        },
        onEvent(sessionId, listener) {
            const listeners = sessions.get(sessionId) ?? new Set();
            sessions.set(sessionId, listeners);
            listeners.add(listener);
            return () => {
                listeners.delete(listener);
                if (!listeners.size) sessions.delete(sessionId);
            };
        },
        onDisconnect(listener) {
            if (closed) listener(closed);
            else disconnects.add(listener);
        },
        dispose() {
            fail(new Error('DevTools pipe disposed'));
            sessions.clear(); disconnects.clear();
            browser.removeListener('error', failed);
            browser.removeListener('exit', exited);
            output.removeListener('data', received);
            // Helpers can inherit a pipe after the browser process exits.
            input.destroy(); output.destroy();
        },
    };
}
