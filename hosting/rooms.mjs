import rtc from 'node-datachannel';
import { randomBytes } from 'node:crypto';
import { Router } from './transport.mjs';
import { requireValue } from './store.mjs';

function iceServers(items) {
    const result = [];
    for (const server of items.slice(0, 8)) {
        for (const url of (Array.isArray(server.urls) ? server.urls : [server.urls]).slice(0, 8)) {
            if (typeof url !== 'string' || url.length > 2048) continue;
            if (url.startsWith('stun:')) result.push(url);
            else {
                const match = /^(turns?):([^?]+)(?:\?transport=(udp|tcp))?$/.exec(url);
                if (!match) continue;
                const address = new URL(`https://${match[2]}`);
                result.push({ hostname: address.hostname, port: Number(address.port) || (match[1] === 'turns' ? 5349 : 3478),
                    username: server.username, password: server.credential,
                    relayType: match[1] === 'turns' ? 'TurnTls' : match[3] === 'tcp' ? 'TurnTcp' : 'TurnUdp' });
            }
        }
    }
    return result;
}
export class Rooms {
    constructor(store, matches, config) {
        Object.assign(this, { store, config });
        this.router = new Router(store, matches, config);
        this.rooms = new Map(); this.closed = false;
        this.sweep = setInterval(() => this.router.sweep(), 1000);
        store.db.exec(`CREATE TABLE IF NOT EXISTS rooms (
            project TEXT PRIMARY KEY REFERENCES projects(id), code TEXT NOT NULL UNIQUE, key TEXT NOT NULL
        )`);
    }
    endpoint(project, match = '0') {
        const room = this.store.db.prepare('SELECT code FROM rooms WHERE project=?').get(project);
        return room ? `${room.code}/${match}` : null;
    }
    async ensure(project) {
        let room = this.rooms.get(project.id);
        if (!room) {
            let saved = this.store.db.prepare('SELECT * FROM rooms WHERE project=?').get(project.id);
            if (!saved) {
                let code;
                do { code = [...randomBytes(6)].map(byte => '23456789ABCDEFGHJKLMNPQRSTUVWXYZ'[byte & 31]).join(''); }
                while (this.store.db.prepare('SELECT 1 FROM rooms WHERE code=?').get(code));
                saved = { project: project.id, code, key: randomBytes(16).toString('hex') };
                this.store.db.prepare('INSERT INTO rooms VALUES (?,?,?)').run(saved.project, saved.code, saved.key);
            }
            room = { ...saved, peers: new Map(), ice: [], reachable: false, socket: null, timer: null };
            this.rooms.set(project.id, room);
            this.connect(room);
        }
        requireValue(!room.failed, 503, 'The project room is unavailable.');
        if (room.reachable) return;
        await new Promise((accept, reject) => {
            const deadline = setTimeout(() => { clearInterval(check); reject(new Error('The match relay is unavailable.')); }, 5000);
            const check = setInterval(() => {
                if (room.reachable) { clearTimeout(deadline); clearInterval(check); accept(); }
            }, 20);
        });
    }
    connect(room) {
        if (this.closed) return;
        clearTimeout(room.refreshTimer);
        const ws = new WebSocket(this.config.relay); room.socket = ws;
        const send = message => { if (ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(message)); };
        ws.onmessage = event => {
            if (room.socket !== ws || typeof event.data !== 'string' || event.data.length > 65536) return;
            try {
                const message = JSON.parse(event.data);
                if (message.relay) {
                    room.ice = this.config.rtcLocal ? [] : iceServers(message.ice || []);
                    // The existing relay issues two-day TURN credentials. Renew
                    // signaling after one day; established data channels survive.
                    room.refreshTimer = setTimeout(() => { if (room.socket === ws) ws.close(); }, 86400_000);
                    send({ host: room.code, key: room.key });
                } else if (message.hosting) room.reachable = true;
                else if (message.taken || message.lost) {
                    room.failed = true; room.reachable = false; ws.close();
                    const project = this.store.db.prepare('SELECT account FROM projects WHERE id=?').get(room.project);
                    this.store.incident(project.account, null, 'connection', 'Project room ownership could not be restored.');
                } else if (message.peer) this.peer(room, message.peer, signal => send({ to: message.peer, signal }));
                else if (message.from) this.signal(room.peers.get(message.from), message.signal);
                else if (message.left) {
                    const peer = room.peers.get(message.left);
                    if (peer && !peer.channel?.isOpen()) this.drop(room, peer);
                }
            } catch { /* Malformed signals cannot interrupt other rooms. */ }
        };
        ws.onerror = () => {};
        ws.onclose = () => {
            if (room.socket !== ws || this.closed || room.failed) return;
            room.reachable = false;
            for (const peer of room.peers.values()) if (!peer.channel?.isOpen()) this.drop(room, peer);
            room.timer = setTimeout(() => this.connect(room), 3000);
        };
    }
    peer(room, remote, signal) {
        if (!Number.isInteger(remote) || room.peers.has(remote) || room.failed) return;
        if (this.router.peers.size >= this.config.peers) return;
        const pc = new rtc.PeerConnection('tide-hosting', { iceServers: room.ice,
            ...(this.config.rtcLocal ? { bindAddress: '127.0.0.1' } : {}), maxMessageSize: 1200 });
        const peer = { pc, remote, channel: null, signal, timer: null, route: null };
        const route = this.router.add(room.project, data => {
            try { if (peer.channel?.isOpen() && peer.channel.bufferedAmount() <= 65536) peer.channel.sendMessageBinary(data); }
            catch { this.drop(room, peer); }
        });
        if (!route) { pc.close(); return; }
        peer.route = route; room.peers.set(remote, peer);
        route.disconnect = () => this.drop(room, peer);
        peer.timer = setTimeout(() => { if (!peer.channel?.isOpen()) this.drop(room, peer); }, 20000);
        pc.onLocalDescription((sdp, type) => signal({ description: { type, sdp } }));
        pc.onLocalCandidate((candidate, mid) => signal({ candidate: { candidate, sdpMid: mid } }));
        pc.onStateChange(state => { if (['failed', 'closed', 'disconnected'].includes(state)) this.drop(room, peer); });
        pc.onDataChannel(channel => {
            if (peer.channel || channel.getLabel() !== 'tide') { channel.close(); return; }
            peer.channel = channel;
            channel.onMessage(data => {
                if (Buffer.isBuffer(data)) this.router.receive(route, data);
            });
            channel.onClosed(() => this.drop(room, peer));
            channel.onError(() => this.drop(room, peer));
        });
    }
    signal(peer, signal) {
        if (!peer || !signal || typeof signal !== 'object') return;
        try {
            const { description, candidate } = signal;
            if (description && ['offer', 'answer'].includes(description.type) && typeof description.sdp === 'string'
                && description.sdp.length <= 16384) peer.pc.setRemoteDescription(description.sdp, description.type);
            else if (candidate && typeof candidate.candidate === 'string' && candidate.candidate.length <= 2048
                && typeof candidate.sdpMid === 'string' && candidate.sdpMid.length <= 64)
                peer.pc.addRemoteCandidate(candidate.candidate, candidate.sdpMid);
        } catch { /* A bad peer is bounded by its connection timeout. */ }
    }
    drop(room, peer) {
        if (room.peers.get(peer.remote) !== peer) return;
        room.peers.delete(peer.remote); this.router.remove(peer.route);
        clearTimeout(peer.timer); peer.channel?.close(); peer.pc.close();
    }
    async close() {
        this.closed = true;
        clearInterval(this.sweep);
        for (const room of this.rooms.values()) {
            clearTimeout(room.timer); room.socket?.close();
            clearTimeout(room.refreshTimer);
            for (const peer of room.peers.values()) this.drop(room, peer);
        }
        this.router.close(); this.rooms.clear();
        rtc.cleanup();
    }
}
