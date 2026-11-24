// Builds a Unity iOS player's Xcode project without a Mac, on GitHub's (the iOS app workflow, .github/workflows/
// ios-app.yml):
//
//   node unity/ios-app.mjs <the Xcode project's folder> [--simulator] [--out <folder>] [--repo <owner/name>]
//                          [--ref <branch>] [--keep]
//
// The folder is what Unity's iOS build made (File > Build Profiles > iOS > Build, on any system): Tide's package
// has put the game in it as sources. This packs it, uploads it as a draft release's files (which only the
// repository's own people can read), starts the workflow, waits for it, and downloads what it made into --out
// (the folder beside the project, by default):
// - app-unsigned.ipa, the app for an iPhone. It isn't signed: install it with a tool that signs it with your Apple
//   ID as it installs (the package's README, iOS).
// - with --simulator (a project built with Player Settings > Target SDK > Simulator SDK), app.log and said.txt: what
//   the app said when the workflow ran it in a simulator. The run fails where the app's check of itself did.
// The draft release is deleted at the end unless --keep. Needs the `gh` command, signed in (gh auth login), and
// tar, which Windows 10 and later have. --repo: where the workflow is (this checkout's by default); a private
// repository's runs on a Mac are billed to its account. --ref: the branch whose workflow runs.
import { execFileSync, spawnSync } from 'node:child_process';
import { closeSync, existsSync, mkdirSync, openSync, readSync, rmSync, statSync, writeSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
function taken(name) {
    const at = args.indexOf(name);
    if (at < 0) return null;
    const [, value] = args.splice(at, 2);
    return value;
}
function flag(name) {
    const at = args.indexOf(name);
    if (at >= 0) args.splice(at, 1);
    return at >= 0;
}
const out = taken('--out'), repo = taken('--repo'), ref = taken('--ref');
const simulator = flag('--simulator'), keep = flag('--keep');
const project = args[0] && resolve(args[0]);
if (!project || args.length !== 1) {
    console.error('usage: node unity/ios-app.mjs <the Xcode project\'s folder> [--simulator] [--out <folder>] [--repo <owner/name>] [--ref <branch>] [--keep]');
    process.exit(2);
}
if (!existsSync(join(project, 'Unity-iPhone.xcodeproj'))) {
    console.error(`${project} has no Unity-iPhone.xcodeproj: give the folder Unity's iOS build made`);
    process.exit(2);
}
if (!existsSync(join(project, 'Libraries', 'Tide', 'sources.txt'))) {
    console.error(`${project} has no Libraries/Tide: build the iOS player with Tide's package in the project, which puts the game there`);
    process.exit(2);
}
const where = repo ? ['--repo', repo] : [];
// gh works out the repository from the folder it runs in: this file's, in Tide's checkout, unless --repo says
const here = dirname(fileURLToPath(import.meta.url));
const gh = (list, options = {}) => execFileSync('gh', list, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'inherit'], cwd: here, ...options });

// The project as one archive, cut into parts a release takes (2 GiB a file at most)
const work = join(tmpdir(), `tide-ios-app-${process.pid}`);
rmSync(work, { recursive: true, force: true });
mkdirSync(work, { recursive: true });
const archive = join(work, 'xcode.tar.gz');
console.log('packing the project...');
const packed = spawnSync('tar', ['-czf', archive, '-C', project, '.'], { stdio: 'inherit' });
if (packed.status !== 0) {
    console.error('tar couldn\'t pack the project');
    process.exit(1);
}
const part = 1024 * 1024 * 1024, size = statSync(archive).size, parts = [];
const from = openSync(archive, 'r'), buffer = Buffer.alloc(16 * 1024 * 1024);
for (let at = 0, index = 0; at < size; index++) {
    const path = join(work, `xcode.tar.gz.${String(index).padStart(3, '0')}`);
    const to = openSync(path, 'w');
    for (let left = Math.min(part, size - at); left > 0;) {
        const read = readSync(from, buffer, 0, Math.min(buffer.length, left), at);
        writeSync(to, buffer, 0, read);
        at += read;
        left -= read;
    }
    closeSync(to);
    parts.push(path);
}
closeSync(from);
rmSync(archive);
console.log(`${(size / 1048576).toFixed(0)} MB in ${parts.length} file${parts.length === 1 ? '' : 's'}`);

const tag = `ios-app-${new Date().toISOString().replace(/[-:T]/g, '').slice(0, 14)}-${process.pid}`;
let code = 1;
try {
    console.log(`uploading as the draft release ${tag}...`);
    gh(['release', 'create', tag, ...parts, '--draft', '--title', `iOS app ${tag}`,
        '--notes', 'A Unity iOS player\'s Xcode project, for the iOS app workflow to build. unity/ios-app.mjs deletes it.', ...where]);
    const began = new Date(Date.now() - 60000).toISOString();
    gh(['workflow', 'run', 'ios-app.yml', '-f', `release=${tag}`, '-f', `simulator=${simulator}`, ...(ref ? ['--ref', ref] : []), ...where]);
    // The run this started: the newest one of the workflow named after the release
    let run = null;
    for (let tries = 0; tries < 30 && !run; tries++) {
        await new Promise((done) => setTimeout(done, 4000));
        const runs = JSON.parse(gh(['run', 'list', '--workflow', 'ios-app.yml', '--created', `>${began}`, '--json', 'databaseId,displayTitle,url', '--limit', '20', ...where]));
        run = runs.find((one) => one.displayTitle.includes(tag)) ?? null;
    }
    if (!run) throw new Error('the workflow\'s run didn\'t appear: look at the repository\'s Actions');
    console.log(`building on a Mac: ${run.url}`);
    let state = null;
    for (;;) {
        await new Promise((done) => setTimeout(done, 30000));
        state = JSON.parse(gh(['run', 'view', String(run.databaseId), '--json', 'status,conclusion', ...where]));
        if (state.status === 'completed') break;
    }
    const into = resolve(out ?? join(dirname(project), 'ios-app'));
    mkdirSync(into, { recursive: true });
    spawnSync('gh', ['run', 'download', String(run.databaseId), '--dir', into, ...where], { stdio: 'inherit', cwd: here });
    console.log(state.conclusion === 'success' ? `built: what it made is in ${into}` : `the build ${state.conclusion}: its log is in ${into}, and at ${run.url}`);
    code = state.conclusion === 'success' ? 0 : 1;
} catch (error) {
    console.error(String(error.message ?? error));
} finally {
    if (!keep) spawnSync('gh', ['release', 'delete', tag, '--yes', ...where], { stdio: 'ignore', cwd: here });
    rmSync(work, { recursive: true, force: true });
}
process.exit(code);
