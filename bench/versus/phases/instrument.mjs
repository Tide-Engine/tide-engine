// Only diagnostic targets use this copy; ordinary generated C stays untouched.
import { readFile, writeFile } from 'node:fs/promises';
import { instrumentLife } from './life.mjs';
import { instrumentEvents } from './events.mjs';

const [concept, input, output] = process.argv.slice(2);
if (!['life', 'events'].includes(concept) || !input || !output) {
    throw new Error('Usage: instrument.mjs life|events input.c output.c');
}
const source = await readFile(input, 'utf8');
await writeFile(output, concept === 'life' ? instrumentLife(source) : instrumentEvents(source));
