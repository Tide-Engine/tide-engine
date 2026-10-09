export function mountRoom({ projectId, clientId, frame, video, overlay, onStatus }) {
    let detail, currentRoom, ready = false, destroyed = false, claiming = false, peerConnection, iceServers = [];
    let currentSource = '', captureId = 0, movementAt = 0, cursorAt = 0;
    let inputQueue = Promise.resolve(), signalQueue = Promise.resolve(), signalSession = null;
    const pendingCandidates = new Map();
    const captures = new Map(), history = [], offered = new Set();
    const cursors = document.createElement('div');
    cursors.id = 'live-cursors';
    Object.assign(cursors.style, { position: 'absolute', inset: '0', pointerEvents: 'none', overflow: 'hidden' });
    overlay.append(cursors);
    const stage = overlay.parentElement;
    const cleanups = [];
    video.muted = true;
    video.autoplay = true;
    video.playsInline = true;
    video.tabIndex = 0;
    frame.allow = 'autoplay; fullscreen; cross-origin-isolated';
    frame.setAttribute('sandbox', 'allow-scripts allow-same-origin allow-pointer-lock');
    const api = async (path, body) => {
        const response = await fetch(`/api/projects/${projectId}/${path}`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
        const data = await response.json();
        if (!response.ok) throw new Error(data.error || 'The playtest connection was interrupted.');
        return data;
    };
    fetch('/rtc-config').then(r => r.json()).then(data => { iceServers = data.iceServers || []; }).catch(() => {});
    const isHost = () => currentRoom?.hostClientId === clientId;
    const isController = () => currentRoom?.controllerClientId === clientId;
    // The authenticated URL redirects to the preview origin. Use the observed origin
    // after the ready handshake rather than reading a cross-origin frame's location.
    let previewOrigin = null;
    function bridge(data) { if (frame.contentWindow && previewOrigin) frame.contentWindow.postMessage(data, previewOrigin); }
    function configure() { bridge({ type: 'mockup:configure', clientId, controllerClientId: currentRoom?.controllerClientId, epoch: currentRoom?.epoch, iceServers }); }
    function renderCursors() {
        cursors.replaceChildren();
        const rect = contentRect();
        const stageRect = stage.getBoundingClientRect();
        for (const member of currentRoom?.presence || []) {
            if (member.clientId === clientId || !member.cursor || member.cursor.surface !== 'game') continue;
            const cursor = document.createElement('div');
            const hue = [...member.userId].reduce((n, c) => n + c.charCodeAt(0), 0) % 360;
            Object.assign(cursor.style, { position: 'absolute', left: `${rect.left - stageRect.left + member.cursor.x * rect.width}px`,
                top: `${rect.top - stageRect.top + member.cursor.y * rect.height}px`, color: `hsl(${hue} 80% 70%)`, font: '600 12px system-ui', whiteSpace: 'nowrap', transform: 'translate(-2px,-2px)' });
            const arrow = document.createElement('span'); arrow.textContent = '↖'; arrow.style.fontSize = '24px';
            const name = document.createElement('span'); name.textContent = member.name;
            Object.assign(name.style, { background: '#12141e', padding: '4px 7px', borderRadius: '5px', marginLeft: '2px' });
            cursor.append(arrow, name); cursors.append(cursor);
        }
    }
    function status(text, extra) { onStatus?.({ text, ...extra }); }
    async function update(next) {
        if (destroyed) return;
        detail = next;
        const prior = currentRoom;
        currentRoom = next.room;
        if (!currentRoom) return;
        renderCursors();
        const active = next.revisions.find(r => r.id === (currentRoom.revisionId || next.project.activeRevisionId));
        if (!currentRoom.hostClientId && active && !claiming && currentRoom.presence.some(member => member.clientId === clientId)) {
            claiming = true;
            try { await api('host', { clientId, revisionId: active.id }); }
            catch (error) { status(error.message); }
            finally { claiming = false; }
        }
        if (isHost() && active) {
            frame.hidden = false; video.hidden = true;
            if (currentSource !== active.previewUrl) {
                currentSource = active.previewUrl; ready = false; previewOrigin = null; offered.clear();
                frame.src = currentSource;
            }
            if (ready) {
                configure();
                for (const member of currentRoom.presence) if (member.clientId !== clientId && !offered.has(member.clientId)) {
                    offered.add(member.clientId); bridge({ type: 'mockup:peer', peerId: member.clientId });
                }
                for (const peerId of offered) if (!currentRoom.presence.some(p => p.clientId === peerId)) {
                    bridge({ type: 'mockup:drop-peer', peerId }); offered.delete(peerId);
                }
            }
        } else {
            frame.hidden = true; video.hidden = false;
            if (currentSource) { frame.removeAttribute('src'); currentSource = ''; ready = false; previewOrigin = null; offered.clear(); }
            if (prior?.hostClientId !== currentRoom.hostClientId) { peerConnection?.close(); peerConnection = null; signalSession = null; pendingCandidates.clear(); video.srcObject = null; }
        }
        const controller = currentRoom.presence.find(p => p.clientId === currentRoom.controllerClientId);
        status(isController() ? 'You have the controller' : controller ? `${controller.name} has the controller` : 'Take control to play',
            { controller: controller?.name, mine: isController(), host: isHost(), revisionId: active?.id, streaming: !!video.srcObject });
    }
    function contentRect() {
        const rect = stage.getBoundingClientRect();
        if (isHost() || !video.videoWidth) return rect;
        const ratio = video.videoWidth / video.videoHeight;
        const width = Math.min(rect.width, rect.height * ratio), height = width / ratio;
        return { left: rect.left + (rect.width - width) / 2, top: rect.top + (rect.height - height) / 2, width, height };
    }
    function point(event) {
        const rect = contentRect();
        return { x: Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width)), y: Math.max(0, Math.min(1, (event.clientY - rect.top) / rect.height)) };
    }
    function presence(cursor) {
        const now = performance.now();
        if (now - cursorAt < 65 && cursor) return;
        cursorAt = now;
        api('presence', { clientId, cursor: cursor ? { ...cursor, surface: 'game' } : null }).catch(() => {});
    }
    function sendInput(event) {
        if (!isController() || destroyed) return;
        const epoch = currentRoom.epoch;
        if (['mousemove', 'pointermove'].includes(event.type)) {
            if (performance.now() - movementAt < 25) return;
            movementAt = performance.now();
        }
        inputQueue = inputQueue.then(() => {
            if (currentRoom.epoch !== epoch || destroyed) return;
            return api('input', { clientId, epoch, event });
        }).catch(() => {});
    }
    function record(data) {
        history.push({ at: Date.now(), epoch: data.epoch, event: data.event });
        while (history.length > 1200 || history[0]?.at < Date.now() - 15000) history.shift();
    }
    async function handleEvent(type, data) {
        if (destroyed) return;
        if (type === 'room') await update({ ...detail, room: data.room || data });
        else if (type === 'presence') {
            const member = currentRoom?.presence.find(p => p.clientId === data.clientId);
            if (member) { Object.assign(member, data); renderCursors(); }
        } else if (type === 'input') { record(data); if (isHost()) bridge({ type: 'mockup:input', epoch: data.epoch, event: data.event }); }
        else if (type === 'signal') {
            if (data.to !== clientId) return;
            signalQueue = signalQueue.then(() => receiveSignal(data)).catch(error => status(`Playtest connection: ${error.message}`));
            await signalQueue;
        }
    }
    async function receiveSignal(data) {
        if (destroyed) return;
            try {
                if (isHost()) bridge({ type: 'mockup:signal', from: data.from, signal: data.signal });
                else if (data.from === currentRoom.hostClientId) {
                    const sessionId = data.signal.sessionId;
                    if (data.signal.description?.type === 'offer' && signalSession !== sessionId) {
                        peerConnection?.close(); peerConnection = null; signalSession = sessionId;
                    }
                    if (!peerConnection && data.signal.description) {
                        peerConnection = new RTCPeerConnection({ iceServers });
                        peerConnection.onicecandidate = event => { if (event.candidate) api('signal', { from: clientId, to: data.from, signal: { sessionId, candidate: event.candidate.toJSON() } }).catch(() => {}); };
                        peerConnection.ontrack = event => {
                            video.srcObject = event.streams[0]; video.play().catch(() => {});
                            status(isController() ? 'You have the controller' : 'Watching the live playtest', { streaming: true, mine: isController() });
                        };
                        peerConnection.onconnectionstatechange = () => { if (peerConnection?.connectionState === 'failed') status('Could not connect the live video. A TURN server may be needed for this network.'); };
                    }
                    if (data.signal.description) {
                        await peerConnection.setRemoteDescription(data.signal.description);
                        if (data.signal.description.type === 'offer') {
                            await peerConnection.setLocalDescription(await peerConnection.createAnswer());
                            await api('signal', { from: clientId, to: data.from, signal: { sessionId, description: peerConnection.localDescription.toJSON() } });
                        }
                        for (const candidate of pendingCandidates.get(sessionId) || []) await peerConnection.addIceCandidate(candidate);
                        pendingCandidates.delete(sessionId);
                    }
                    if (data.signal.candidate) {
                        if (peerConnection?.remoteDescription && signalSession === sessionId) await peerConnection.addIceCandidate(data.signal.candidate);
                        else {
                            if (pendingCandidates.size > 8) pendingCandidates.clear();
                            const candidates = pendingCandidates.get(sessionId) || [];
                            if (candidates.length < 64) candidates.push(data.signal.candidate);
                            pendingCandidates.set(sessionId, candidates);
                        }
                    }
                }
            } catch (error) { status(`Playtest connection: ${error.message}`); }
    }
    const message = event => {
        if (event.source !== frame.contentWindow || !event.data?.type?.startsWith('mockup:')) return;
        // Only a loaded, scoped preview may complete the bridge handshake.
        if (event.data.type === 'mockup:ready' && isHost() && currentSource) {
            previewOrigin = event.origin; ready = true; offered.clear(); update(detail); return;
        }
        if (event.origin !== previewOrigin) return;
        const data = event.data;
        if (data.type === 'mockup:local-input') {
            if (Number.isFinite(data.event.x)) presence({ x: data.event.x, y: data.event.y });
            sendInput(data.event);
        } else if (data.type === 'mockup:signal') api('signal', { from: clientId, to: data.to, signal: data.signal }).catch(error => status(error.message));
        else if (data.type === 'mockup:capture-result') { captures.get(data.requestId)?.(data); captures.delete(data.requestId); }
        else if (data.type === 'mockup:error') status(data.message);
    };
    addEventListener('message', message); cleanups.push(() => removeEventListener('message', message));
    const listen = (target, type, handler, options) => { target.addEventListener(type, handler, options); cleanups.push(() => target.removeEventListener(type, handler, options)); };
    listen(stage, 'pointermove', event => presence(point(event)));
    listen(stage, 'pointerleave', () => presence(null));
    for (const type of ['keydown', 'keyup']) listen(document, type, event => {
        if (!isController() || !stage.contains(document.activeElement) || /^(INPUT|TEXTAREA|BUTTON)$/.test(document.activeElement?.tagName)) return;
        event.preventDefault(); sendInput({ type, key: event.key, code: event.code, repeat: event.repeat });
    });
    for (const type of ['mousemove', 'mousedown', 'mouseup', 'pointerdown', 'pointermove', 'pointerup', 'pointercancel', 'wheel']) listen(video, type, event => {
        if (type.startsWith('pointer') && event.pointerType !== 'touch') return;
        if (type === 'mousedown' || type === 'pointerdown') video.focus({ preventScroll: true });
        if (type === 'pointerdown') video.setPointerCapture(event.pointerId);
        if (event.cancelable && isController()) event.preventDefault();
        sendInput({ type, ...point(event), button: event.button, buttons: event.buttons, pointerId: event.pointerId,
            pointerType: event.pointerType, deltaX: event.deltaX, deltaY: event.deltaY });
    }, { passive: false });
    listen(window, 'blur', () => sendInput({ type: 'blur' }));
    return {
        update, handleEvent, point,
        async takeControl() { await api('control', { clientId }); video.focus({ preventScroll: true }); },
        async switchRevision(revisionId) {
            if (!isHost()) throw new Error('The playtest host can switch versions.');
            await api('host', { clientId, revisionId });
        },
        async capture() {
            let visual = {};
            if (isHost() && ready) visual = await new Promise(resolve => {
                const requestId = ++captureId;
                const timer = setTimeout(() => { captures.delete(requestId); resolve({}); }, 2500);
                captures.set(requestId, data => { clearTimeout(timer); resolve(data); });
                bridge({ type: 'mockup:capture', requestId });
            });
            else if (video.videoWidth) {
                const surface = document.createElement('canvas'); surface.width = video.videoWidth; surface.height = video.videoHeight;
                surface.getContext('2d').drawImage(video, 0, 0);
                visual = { screenshot: surface.toDataURL('image/jpeg', 0.8), viewport: { width: surface.width, height: surface.height } };
            }
            return { kind: 'visual-and-input', capturedAt: new Date().toISOString(), revisionId: currentRoom?.revisionId,
                inputs: history.slice(-600), ...visual, worldSnapshot: null };
        },
        destroy() { destroyed = true; cleanups.forEach(fn => fn()); peerConnection?.close(); frame.removeAttribute('src'); video.srcObject = null; cursors.remove(); },
    };
}
