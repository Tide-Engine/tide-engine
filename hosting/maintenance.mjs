import { readdir, stat, unlink } from 'node:fs/promises';
import { join } from 'node:path';
import { requireValue } from './store.mjs';

export class Maintenance {
    constructor(store, bundles, saves, limits) { Object.assign(this, { store, bundles, saves, limits }); }
    async discard(account, id) {
        requireValue(this.store.matchProject(id)?.account === account, 404, 'Match not found.');
        const match = this.store.match(id);
        requireValue(['suspended', 'failed', 'ended'].includes(match.state), 409, 'Suspend or end the match before deleting its save.');
        this.store.db.prepare("UPDATE matches SET checkpoint=NULL,checkpoint_time=NULL,state='ended',desired='ended',updated=? WHERE id=?")
            .run(this.store.now(), id);
        if (match.checkpoint) await this.remove(join(this.saves, `${match.id}.${match.checkpoint}.save`));
    }
    async remove(path) { await unlink(path).catch(error => { if (error.code !== 'ENOENT') throw error; }); }
    async sweep() {
        const now = this.store.now(), retention = this.limits.retentionDays * 86400_000;
        const historyCutoff = Math.floor((now - 30 * 86400_000) / 86400_000) * 86400_000;
        this.store.transaction(() => {
            const summaries = this.store.db.prepare(`SELECT account,CAST(created/86400000 AS INTEGER) AS day,SUM(amount) AS amount
                FROM ledger WHERE created<? AND (reference LIKE 'lease/%' OR reference LIKE 'settle/%' OR reference LIKE 'resume/%') GROUP BY account,day`).all(historyCutoff);
            for (const row of summaries) this.store.db.prepare(`INSERT INTO ledger(account,amount,reference,created) VALUES (?,?,?,?)
                ON CONFLICT(reference) DO UPDATE SET amount=ledger.amount+excluded.amount`)
                .run(row.account, row.amount, `usage/${row.account}/${row.day}`, row.day * 86400_000);
            this.store.db.prepare("DELETE FROM ledger WHERE created<? AND (reference LIKE 'lease/%' OR reference LIKE 'settle/%' OR reference LIKE 'resume/%')").run(historyCutoff);
            this.store.db.prepare('DELETE FROM creations WHERE day<?').run(Math.floor(historyCutoff / 86400_000));
            this.store.db.prepare(`DELETE FROM match_policy WHERE match_id IN (SELECT id FROM matches
                WHERE checkpoint IS NULL AND state IN ('ended','failed','suspended') AND updated<?)`).run(historyCutoff);
            this.store.db.prepare("DELETE FROM matches WHERE checkpoint IS NULL AND state IN ('ended','failed','suspended') AND updated<?").run(historyCutoff);
        });
        const rows = this.store.db.prepare(`SELECT m.*,p.account FROM matches m JOIN projects p ON p.id=m.project
            WHERE m.checkpoint IS NOT NULL AND m.state IN ('suspended','failed','ended')`).all();
        for (const match of rows) {
            if (now - match.updated >= retention) {
                await this.discard(match.account, match.id);
                this.store.incident(match.account, match.id, 'retention-expired', 'The save retention period ended; the suspended world save was deleted.');
            } else if (retention - (now - match.updated) <= 86400_000 && match.incident !== 'retention-warning') {
                this.store.incident(match.account, match.id, 'retention-warning', 'This inactive save expires within 24 hours. Resume or download it to retain your world.');
                this.store.db.prepare("UPDATE matches SET incident='retention-warning' WHERE id=?").run(match.id);
            }
        }
        const saves = new Set(this.store.db.prepare('SELECT id,checkpoint FROM matches WHERE checkpoint IS NOT NULL').all()
            .map(row => `${row.id}.${row.checkpoint}.save`));
        const releases = new Set(this.store.db.prepare('SELECT id FROM releases').all().map(row => `${row.id}.tide-deploy`));
        for (const [folder, keep, pattern] of [[this.saves, saves, /^\d{1,15}\.[a-f0-9]{64}\.save(?:\.[a-f0-9]{16}\.tmp)?$/],
            [this.bundles.folder, releases, /^[a-f0-9]{64}\.tide-deploy(?:\.[a-f0-9]{16}\.tmp)?$/]]) {
            const files = await readdir(folder).catch(error => { if (error.code === 'ENOENT') return []; throw error; });
            for (const name of files) {
                if (!pattern.test(name) || keep.has(name)) continue;
                const path = join(folder, name);
                if (now - (await stat(path)).mtimeMs > 3600_000) await this.remove(path);
            }
        }
    }
    async deleteRelease(account, project, id) {
        const p = this.store.project(account, project);
        requireValue(p.release !== id, 409, 'Deploy another release before deleting the current release.');
        const needed = this.store.db.prepare(`SELECT 1 FROM matches WHERE project=? AND release=?
            AND (checkpoint IS NOT NULL OR state IN ('starting','running','recovering','suspending'))`).get(project, id);
        requireValue(!needed, 409, 'This release is still required by a running or saved world.');
        this.store.db.prepare('DELETE FROM releases WHERE project=? AND id=?').run(project, id);
        if (!this.store.db.prepare('SELECT 1 FROM releases WHERE id=?').get(id)) await this.bundles.remove(id);
    }
}
