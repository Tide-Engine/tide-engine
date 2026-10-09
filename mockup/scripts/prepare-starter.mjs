import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { access, mkdir, readFile, readdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const mockupDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const starterDirectory = path.join(mockupDirectory, 'starter');

const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

async function exists(file) {
  try { await access(file); return true; } catch { return false; }
}

export async function resolveTideBinary(explicit = process.env.TIDE_BIN) {
  if (explicit) return explicit;
  // PATH is the normal installation contract; this also supports a fresh local
  // Windows install whose terminal has not inherited the changed PATH yet.
  if (process.platform === 'win32' && process.env.LOCALAPPDATA) {
    const installed = path.join(process.env.LOCALAPPDATA, 'Tide', 'bin', 'tide.exe');
    if (await exists(installed)) return installed;
  }
  return 'tide';
}

async function command(executable, args, { cwd, log } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, {
      cwd,
      shell: false,
      windowsHide: true,
      env: { ...process.env, TIDE_HIDDEN: '1', TIDE_NO_UPDATE_CHECK: '1' },
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let stdout = '';
    let stderr = '';
    child.stdout.setEncoding('utf8');
    child.stderr.setEncoding('utf8');
    child.stdout.on('data', value => { stdout += value; log?.(value); });
    child.stderr.on('data', value => { stderr += value; log?.(value); });
    child.on('error', error => reject(new Error(
      `Cannot start Tide at ${executable}. Install Tide or set TIDE_BIN to its executable. ${error.message}`,
      { cause: error },
    )));
    child.on('close', (code, signal) => {
      if (code === 0) resolve({ stdout, stderr });
      else reject(new Error(`Tide ${args[0]} failed (${signal || code}).\n${stderr || stdout}`));
    });
  });
}

async function sourceManifest(directory) {
  const sources = [];
  async function visit(folder) {
    const entries = await readdir(folder, { withFileTypes: true });
    entries.sort((a, b) => a.name.localeCompare(b.name, 'en'));
    for (const entry of entries) {
      if (entry.name.startsWith('.') || entry.name === 'build' || entry.name === 'node_modules') continue;
      const file = path.join(folder, entry.name);
      if (entry.isDirectory()) await visit(file);
      else if (entry.isFile() && (entry.name.endsWith('.tide') || entry.name === 'tide.packages')) {
        const bytes = await readFile(file);
        sources.push({ path: path.relative(directory, file).split(path.sep).join('/'), bytes: bytes.length, sha256: sha256(bytes) });
      }
    }
  }
  await visit(directory);
  if (!sources.some(source => source.path.endsWith('.tide'))) throw new Error(`No Tide source in ${directory}`);
  return sources;
}

/** Build a real Tide browser artifact. Missing or failed tooling rejects. */
export async function prepareStarter({
  sourceDirectory = starterDirectory,
  outputDirectory = path.join(starterDirectory, 'build'),
  tideBinary,
  log,
} = {}) {
  const source = path.resolve(sourceDirectory);
  const output = path.resolve(outputDirectory);
  const executable = await resolveTideBinary(tideBinary);
  const version = await command(executable, ['--version']);
  const sources = await sourceManifest(source);
  await mkdir(output, { recursive: true });
  const entryPath = path.join(output, 'index.html');
  await command(executable, ['build', source, '--web', '--release', '-o', entryPath], { cwd: mockupDirectory, log });
  const bytes = await readFile(entryPath);
  if (bytes.length < 1024 || !bytes.includes(Buffer.from('WebAssembly'))) {
    throw new Error('Tide completed without producing a playable WebAssembly HTML artifact.');
  }
  const manifest = {
    schemaVersion: 1,
    id: 'moonwake',
    title: 'Moonwake',
    engine: 'Tide',
    toolchain: version.stdout.split(/\r?\n/)[0].trim(),
    builtAt: new Date().toISOString(),
    sourceHash: sha256(JSON.stringify(sources)),
    entry: 'index.html',
    artifacts: [{ path: 'index.html', bytes: bytes.length, sha256: sha256(bytes) }],
    sources,
  };
  const manifestPath = path.join(output, 'manifest.json');
  await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  return { ...manifest, entryPath, manifestPath };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const options = {};
  const flags = { '--source': 'sourceDirectory', '--output': 'outputDirectory', '--tide': 'tideBinary' };
  try {
    for (let i = 2; i < process.argv.length; i++) {
      const key = flags[process.argv[i]];
      const value = process.argv[++i];
      if (!key || !value) throw new Error('Usage: node scripts/prepare-starter.mjs [--source folder] [--output folder] [--tide executable]');
      options[key] = value;
    }
    const result = await prepareStarter({ ...options, log: value => process.stderr.write(value) });
    process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
  } catch (error) {
    process.stderr.write(`${error.message}\n`);
    process.exitCode = 1;
  }
}
