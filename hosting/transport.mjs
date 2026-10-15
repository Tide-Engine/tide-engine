import { randomBytes } from 'node:crypto';
import { RateLimits } from './http.mjs';

const MAGIC = 0x48534454, VERSION = 2, MTU = 1200, PAYLOAD = MTU - 24;
export const Admission = { pending: 0, accepted: 1, denied: 2, full: 3,
    missing: 4, timeout: 5, invalid: 6, version: 7 };
function header(kind, route, length) {
    const data = Buffer.alloc(length);
    data.writeUInt32LE(MAGIC); data[4] = VERSION; data[5] = kind;
    data.writeBigUInt64LE(BigInt(route), 8); return data;
}
export function admissionRequest(nonce, match, simulation, parameters = Buffer.alloc(0)) {
    const data = header(1, nonce, 60 + parameters.length);
    data.writeBigUInt64LE(BigInt(match), 16); Buffer.from(simulation, 'hex').copy(data, 24);
    data.writeUInt16LE(parameters.length, 58); parameters.copy(data, 60); return data;
}
export function dataPackets(route, sequence, data) {
    const result = [];
    for (let at = 0; at < data.length; at += PAYLOAD) {
        const n = Math.min(PAYLOAD, data.length - at), packet = header(3, route, 24 + n);
        packet.writeUInt32LE(sequence >>> 0, 16); packet.writeUInt16LE(data.length, 20); packet.writeUInt16LE(at, 22);
        data.copy(packet, 24, at, at + n); result.push(packet);
    }
    return result;
}
export class Router {
    constructor(store, matches, limits) {
        Object.assign(this, { store, matches, limits });
        this.peers = new Map(); this.next = 0; this.rate = new RateLimits(store.now);
        matches.onPacket = (match, peer, data) => {
            const connection = this.peers.get(peer);
            if (!connection || connection.match !== match || connection.result !== Admission.accepted) return;
            try {
                if (connection.allowance < data.length + 48) {
                    this.store.reserveTransfer(connection.account, 65536, this.limits);
                    connection.allowance += 65536;
                }
                connection.allowance -= data.length + 48;
            } catch {
                this.store.setMatchState(match, 'suspending', 'suspended');
                return;
            }
            for (const packet of dataPackets(connection.route, ++connection.sequence, Buffer.from(data))) connection.send(packet);
        };
    }
    add(project, send) {
        if (this.peers.size >= this.limits.peers || this.next >= 0xffffffff
            || [...this.peers.values()].filter(p => p.project === project).length >= this.limits.peersPerProject) return null;
        const peer = { id: ++this.next, project, send, route: String(randomBytes(6).readUIntBE(0, 6) || 1),
            nonce: 0n, match: null, result: Admission.pending, parts: new Map(), sequence: 0, requested: false,
            touched: this.store.now(), created: this.store.now(), allowance: 0 };
        this.peers.set(peer.id, peer); return peer;
    }
    remove(peer) { this.peers.delete(peer.id); this.rate.entries.delete(`packets/${peer.id}`); }
    reply(peer) {
        const data = header(2, peer.nonce, 33);
        data[16] = peer.result; data.writeBigUInt64LE(BigInt(peer.match || '0'), 17);
        data.writeBigUInt64LE(BigInt(peer.route), 25); peer.send(data);
    }
    receive(peer, data) {
        if (this.peers.get(peer.id) !== peer || !Buffer.isBuffer(data) || data.length < 16 || data.length > MTU
            || data.readUInt32LE() !== MAGIC || data[4] !== VERSION || data.readUInt16LE(6)) return;
        try { this.rate.take(`packets/${peer.id}`, 6000); } catch { return; }
        peer.touched = this.store.now();
        const route = data.readBigUInt64LE(8);
        if (data[5] === 1) {
            if (!route || data.length < 60) return;
            if (peer.requested) { if (peer.nonce === route) this.reply(peer); return; }
            const credential = data.readUInt16LE(56), parameters = data.readUInt16LE(58);
            if (credential > 512 || parameters > 512 || data.length !== 60 + credential + parameters) return;
            const target = data.readBigUInt64LE(16);
            if (target >= 2n ** 48n || (target && parameters)) return;
            peer.nonce = route; peer.requested = true;
            this.admit(peer, String(target), data.subarray(24, 56).toString('hex'),
                data.subarray(60, 60 + credential), data.subarray(60 + credential))
                .catch(() => { peer.result = Admission.denied; this.reply(peer); });
        } else if (data[5] === 3 && peer.result === Admission.accepted && route === BigInt(peer.route)
            && data.length >= 24) {
            const sequence = data.readUInt32LE(16), total = data.readUInt16LE(20), at = data.readUInt16LE(22);
            if (!total || total > MTU || at >= total || at % PAYLOAD || data.length !== 24 + Math.min(PAYLOAD, total - at)) return;
            if (total <= PAYLOAD) this.matches.receive(peer.match, peer.id, data.subarray(24));
            else {
                const slot = sequence % 8;
                let parts = peer.parts.get(slot);
                if (!parts || parts.sequence !== sequence || parts.total !== total)
                    peer.parts.set(slot, parts = { sequence, total, mask: 0, data: Buffer.alloc(total) });
                const bit = 1 << (at / PAYLOAD);
                if (parts.mask & bit) return;
                data.copy(parts.data, at, 24); parts.mask |= bit;
                if (parts.mask === 3) this.matches.receive(peer.match, peer.id, parts.data);
            }
        }
    }
    async admit(peer, target, simulation, credential, parameters) {
        const project = this.store.db.prepare('SELECT * FROM projects WHERE id=?').get(peer.project);
        const match = target === '0' ? null : this.store.match(target);
        const release = project && this.store.release(project.id, match?.release || project.release);
        if (!project || !release || (target !== '0' && (!match || match.project !== project.id
            || !['starting', 'running', 'recovering'].includes(match.state)))) peer.result = Admission.missing;
        else if (simulation !== release.simulation) peer.result = Admission.version;
        else if (credential.length || this.store.account(project.account).disabled
            || (target === '0' && !this.store.clientCreate(project.id))) peer.result = Admission.denied;
        else {
            try {
                peer.account = project.account;
                if (target === '0') {
                    this.rate.take(`create/${project.id}`, this.limits.createsPerMinute);
                    const created = await this.matches.create(project.account, project.id, parameters);
                    peer.match = created.id;
                } else peer.match = match.id;
                peer.result = Admission.accepted;
            } catch (error) { peer.result = error.status === 409 || error.status === 429 ? Admission.full : Admission.denied; }
        }
        if (this.peers.get(peer.id) === peer) this.reply(peer);
    }
    sweep() {
        for (const peer of this.peers.values()) {
            if ((peer.result !== Admission.accepted && this.store.now() - peer.created > 20000)
                || this.store.now() - peer.touched > 30000
                || (peer.result === Admission.accepted && !this.matches.running.has(peer.match))) {
                if (peer.requested) { peer.result = Admission.missing; this.reply(peer); }
                peer.disconnect?.(); this.remove(peer);
            }
        }
    }
    close() { this.peers.clear(); }
}
