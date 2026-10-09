(() => {
    'use strict';
    const appOrigin = document.currentScript.dataset.appOrigin;
    const send = value => parent.postMessage(value, appOrigin);
    let config = { epoch: -1 };
    const peers = new Map();
    const held = new Map();
    const touches = new Map();
    let signalQueue = Promise.resolve();
    const diagnostics = [];
    const originalContext = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function (kind, options) {
        return originalContext.call(this, kind, kind === 'webgl2' || kind === 'webgl' ? { ...options, preserveDrawingBuffer: true } : options);
    };
    const canvas = () => document.querySelector('canvas');
    const types = ['keydown', 'keyup', 'mousemove', 'mousedown', 'mouseup', 'pointerdown', 'pointermove', 'pointerup', 'pointercancel', 'wheel'];
    for (const type of types) addEventListener(type, event => {
        if (!event.isTrusted) return;
        if (event.target !== canvas() && !type.startsWith('key') && type !== 'mouseup') return;
        if (event.cancelable) event.preventDefault();
        event.stopImmediatePropagation();
        if (type.startsWith('pointer') && event.pointerType !== 'touch') return;
        const rect = canvas()?.getBoundingClientRect();
        if (!rect) return;
        send({ type: 'mockup:local-input', event: { type, key: event.key, code: event.code, repeat: event.repeat,
            ...(Number.isFinite(event.clientX) ? { x: Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width)), y: Math.max(0, Math.min(1, (event.clientY - rect.top) / rect.height)) } : {}),
            button: event.button, buttons: event.buttons, pointerId: event.pointerId, pointerType: event.pointerType,
            deltaX: event.deltaX, deltaY: event.deltaY } });
    }, { capture: true, passive: false });
    function inject(event) {
        const target = canvas();
        if (!target) return;
        if (event.type === 'blur' || event.type === 'clear') { release(); return; }
        if (event.type.startsWith('key')) {
            const init = { key: event.key, code: event.code, repeat: !!event.repeat, bubbles: true, cancelable: true };
            if (event.type === 'keydown') held.set(event.code, init); else held.delete(event.code);
            dispatchEvent(new KeyboardEvent(event.type, init));
        } else {
            const rect = target.getBoundingClientRect();
            const init = { ...event, clientX: rect.left + (event.x || 0) * rect.width,
                clientY: rect.top + (event.y || 0) * rect.height, bubbles: true, cancelable: true };
            const Constructor = event.type.startsWith('pointer') ? PointerEvent : event.type === 'wheel' ? WheelEvent : MouseEvent;
            if (event.type === 'pointerdown') touches.set(event.pointerId, init);
            if (event.type === 'pointerup' || event.type === 'pointercancel') touches.delete(event.pointerId);
            target.dispatchEvent(new Constructor(event.type, init));
        }
    }
    function release() {
        for (const init of held.values()) dispatchEvent(new KeyboardEvent('keyup', init));
        held.clear();
        dispatchEvent(new Event('blur'));
        for (const init of touches.values()) canvas()?.dispatchEvent(new PointerEvent('pointercancel', init));
        touches.clear();
        for (const button of [0, 1, 2]) canvas()?.dispatchEvent(new MouseEvent('mouseup', { button, buttons: 0, bubbles: true }));
    }
    async function peer(id) {
        if (peers.has(id)) return peers.get(id);
        const pc = new RTCPeerConnection({ iceServers: config.iceServers || [] });
        pc.mockupSessionId = crypto.randomUUID();
        pc.mockupCandidates = [];
        peers.set(id, pc);
        const stream = canvas().captureStream(30);
        for (const track of stream.getTracks()) pc.addTrack(track, stream);
        pc.onicecandidate = event => { if (event.candidate) send({ type: 'mockup:signal', to: id, signal: { sessionId: pc.mockupSessionId, candidate: event.candidate.toJSON() } }); };
        pc.onconnectionstatechange = () => {
            if (['failed', 'closed'].includes(pc.connectionState)) { pc.close(); stream.getTracks().forEach(track => track.stop()); peers.delete(id); }
        };
        await pc.setLocalDescription(await pc.createOffer());
        send({ type: 'mockup:signal', to: id, signal: { sessionId: pc.mockupSessionId, description: pc.localDescription.toJSON() } });
        return pc;
    }
    addEventListener('message', async ({ origin, source, data }) => {
        if (origin !== appOrigin || source !== parent || !data?.type?.startsWith('mockup:')) return;
        try {
            if (data.type === 'mockup:configure') {
                if (data.epoch !== config.epoch) release();
                config = data;
            } else if (data.type === 'mockup:input' && data.epoch === config.epoch) inject(data.event);
            else if (data.type === 'mockup:peer') await peer(data.peerId);
            else if (data.type === 'mockup:drop-peer') { peers.get(data.peerId)?.close(); peers.delete(data.peerId); }
            else if (data.type === 'mockup:signal') {
                signalQueue = signalQueue.then(async () => {
                    const pc = peers.get(data.from);
                    if (!pc || pc.mockupSessionId !== data.signal.sessionId) return;
                    if (data.signal.description) {
                        await pc.setRemoteDescription(data.signal.description);
                        for (const candidate of pc.mockupCandidates) await pc.addIceCandidate(candidate);
                        pc.mockupCandidates.length = 0;
                    }
                    if (data.signal.candidate) {
                        if (pc.remoteDescription) await pc.addIceCandidate(data.signal.candidate);
                        else if (pc.mockupCandidates.length < 64) pc.mockupCandidates.push(data.signal.candidate);
                    }
                }).catch(error => send({ type: 'mockup:error', message: error.message }));
            } else if (data.type === 'mockup:capture') {
                send({ type: 'mockup:capture-result', requestId: data.requestId,
                    screenshot: canvas()?.toDataURL('image/jpeg', 0.8), viewport: { width: innerWidth, height: innerHeight }, diagnostics });
            }
        } catch (error) { send({ type: 'mockup:error', message: error.message }); }
    });
    addEventListener('error', event => { diagnostics.push(String(event.message).slice(0, 500)); if (diagnostics.length > 20) diagnostics.shift(); });
    addEventListener('DOMContentLoaded', () => {
        document.body.style.margin = '0';
        document.body.style.overflow = 'hidden';
        const target = canvas();
        if (target) target.tabIndex = 0;
        send({ type: 'mockup:ready' });
    });
    addEventListener('pagehide', () => { for (const pc of peers.values()) pc.close(); });
    addEventListener('blur', event => { if (event.isTrusted) send({ type: 'mockup:local-input', event: { type: 'blur' } }); });
    addEventListener('visibilitychange', () => { if (document.hidden) send({ type: 'mockup:local-input', event: { type: 'blur' } }); });
})();
