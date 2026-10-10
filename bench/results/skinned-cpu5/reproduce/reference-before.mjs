import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
const directory=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(directory,'../../..');
const snapshot=path.join(root,'build/release/skinned-cpu4/after-snapshot');
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
const assert=(ok,message)=>{if(!ok)throw new Error(message);};
const identity=file=>{const bytes=fs.readFileSync(file);return {path:path.relative(root,file).replaceAll('\\','/'),bytes:bytes.length,sha256:hash(bytes)};};
const metadata=JSON.parse(fs.readFileSync(path.join(snapshot,'metadata.json'),'utf8').replace(/^\uFEFF/,''));
assert(metadata.source_revision==='711af03879d9bd687edc4f977ec92f80544e0c22','Unexpected CPU5 baseline revision');
assert(metadata.files.find(file=>file.path==='bench_skinned.exe')?.sha256==='786d7c0a0f19d036d11240e0917223bcf8d383de299f8822a9f55307a0c514c1','Unexpected CPU5 baseline executable');
for(const file of metadata.files){const found=identity(path.join(snapshot,file.path));assert(found.sha256===file.sha256 && found.bytes===file.bytes,'Preserved CPU4 baseline changed: '+file.path);}
const references=['metadata.json','source-hashes.json'].map(name=>identity(path.join(root,'bench/results/skinned-cpu4/snapshot-after',name)));
assert(references[0].sha256==='910a4aea8c7603f54eaaf52ef6a110246f130189c7b3fa0e8bf5491d25adf2de','CPU4 sealed baseline metadata changed');
assert(references[1].sha256==='86919159353f73974d0b28104a147e0f4b0a3a8a01b45036023639e714f61cba','CPU4 sealed baseline source hashes changed');
const normalize=bytes=>Buffer.from(bytes.toString('utf8').replace(/^\uFEFF/,'').replace(/\r\n?/g,'\n').replace(/[ \t]+$/gm,'').replace(/(?<!\n)$/,'\n'));
for(let i=0;i<references.length;i++){const raw=fs.readFileSync(path.join(snapshot,i?'source-hashes.json':'metadata.json'));assert(hash(normalize(raw))===references[i].sha256,'Sealed CPU4 reference differs from retained raw artifact');}
const ledger=identity(path.join(root,'bench/results/skinned.jsonl'));
assert(ledger.sha256==='e60cf4d74b6256fc4a8a02ea44ba9936dbef00f02e1e96946d3290c1203bea44','456-row history ledger changed');
const output={purpose:'CPU5 reuses the immutable CPU4 candidate. Referenced committed baseline artifacts are hash pinned; binaries and full generated/source inputs remain in this checkout scratch and are not duplicated in the CPU5 export.',source_revision:metadata.source_revision,executable:identity(path.join(snapshot,'bench_skinned.exe')),raw_snapshot_metadata:identity(path.join(snapshot,'metadata.json')),raw_source_hashes:identity(path.join(snapshot,'source-hashes.json')),committed_references:references,fixture_and_generated:metadata.files.filter(file=>file.path.startsWith('fixture/')||file.path.startsWith('generated/')),ledger_before:ledger,ledger_before_rows:456};
const destination=path.join(directory,'baseline-reference.json');
if(fs.existsSync(destination)){assert(fs.readFileSync(destination,'utf8')===JSON.stringify(output,null,2)+'\n','Baseline reference already exists with different identities');}
else fs.writeFileSync(destination,JSON.stringify(output,null,2)+'\n',{flag:'wx'});
console.log('Verified immutable CPU4 baseline and wrote hash-pinned CPU5 references; no builds or measurements.');
