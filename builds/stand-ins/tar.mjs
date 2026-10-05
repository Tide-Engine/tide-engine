// Archives for the build service's tests: a tar as `tide publish` writes one
// (compiler/cli/publish.c), and what it never writes, to check that unpacking
// refuses it.

import { gzipSync } from 'node:zlib';

// `files` are {name, data}; with `type`, an entry of another kind (a link is
// '2', with `link` where it points), and with `folder`, each name is inside
// one, as a git host's archive of a repository has it.
export function tar(files, { folder = '' } = {}) {
    const blocks = [];
    for (const file of files) {
        const data = Buffer.from(file.data ?? '');
        const header = Buffer.alloc(512);
        let name = Buffer.from(folder ? `${folder}/${file.name}` : file.name);
        if (name.length > 100) {
            // What goes before a '/' that leaves a name of 100 at most
            let slash = -1;
            for (let i = 1; i < name.length && slash < 0; i++) if (name[i] === 0x2f && name.length - i - 1 <= 100) slash = i;
            if (slash < 0 || slash > 155) throw new Error(`${file.name} is too long for a tar`);
            name.copy(header, 345, 0, slash);
            name = name.subarray(slash + 1);
        }
        name.copy(header, 0);
        const octal = (value, at, size) => header.write(value.toString(8).padStart(size - 1, '0') + '\0', at, 'latin1');
        octal(0o644, 100, 8);
        octal(0, 108, 8);
        octal(0, 116, 8);
        octal(data.length, 124, 12);
        octal(0, 136, 12);
        header.fill(' ', 148, 156);
        header.write(file.type ?? '0', 156, 'latin1');
        if (file.link) header.write(file.link, 157, 'latin1');
        header.write('ustar\0' + '00', 257, 'latin1');
        let sum = 0;
        for (const byte of header) sum += byte;
        header.write(sum.toString(8).padStart(6, '0') + '\0 ', 148, 'latin1');
        blocks.push(header, data, Buffer.alloc((512 - data.length % 512) % 512));
    }
    blocks.push(Buffer.alloc(1024));
    return Buffer.concat(blocks);
}

// A commit as a git host serves it: a tar of the repository in one folder, gzipped.
export function archive(files, folder = 'repository') {
    return gzipSync(tar(files, { folder }));
}
