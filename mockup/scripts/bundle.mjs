import { build } from 'esbuild';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('..', import.meta.url));
await build({ absWorkingDir: root, stdin: { contents: "export * from 'yjs';", resolveDir: root },
    bundle: true, format: 'esm', platform: 'browser', minify: true, outfile: `${root}/client/vendor/yjs.js` });
