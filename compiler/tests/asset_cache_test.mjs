// Real, separate compiler processes: persistent reuse must not depend on globals.
import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, statSync, utimesSync, writeFileSync } from "node:fs";
import { dirname, isAbsolute, join, relative, resolve, sep } from "node:path";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";

const [tidec, scratch] = process.argv.slice(2).map(p=>resolve(p));
assert(tidec && scratch, "pass tidec and a scratch directory");
const fixtures = join(dirname(fileURLToPath(import.meta.url)), "e2e");
function remove(path) {
    const rel = relative(scratch, resolve(path));
    assert(rel && !isAbsolute(rel) && rel !== ".." && !rel.startsWith(`..${sep}`), `unsafe scratch removal: ${path}`);
    rmSync(path, { recursive: true, force: true });
}
mkdirSync(scratch, { recursive: true });
const game = join(scratch, "game"), out = join(scratch, "out");
if (existsSync(game)) remove(game);
mkdirSync(game); mkdirSync(out, { recursive: true });
copyFileSync(join(fixtures, "texture_compression_assets", "authored.dds"), join(game, "image.dds"));
copyFileSync(join(fixtures, "assets", "hero.png"), join(game, "picture.png"));
copyFileSync(join(fixtures, "assets", "hero.png"), join(game, "solo.png"));
copyFileSync(join(fixtures, "assets", "images", "rle_top_left.tga"), join(game, "suffix.tga"));
copyFileSync(join(fixtures, "audio_import", "stereo_cbr.mp3"), join(game, "audio.mp3"));
copyFileSync(join(fixtures, "fbx", "parts", "parts_ascii.fbx"), join(game, "parts.fbx"));
copyFileSync(join(fixtures, "fbx", "models", "rig_ascii.fbx"), join(game, "rig.fbx"));
const obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n";
writeFileSync(join(game, "mesh1.obj"), obj);
const positions = Buffer.alloc(80);
[0,0,0, 1,0,0, 0,1,0, 0,0,1, 0,0,1, 0,0,1].forEach((n,i)=>positions.writeFloatLE(n,i*4));
[0,1,2,0].forEach((n,i)=>positions.writeUInt16LE(n,72+i*2));
writeFileSync(join(game, "shape.bin"), positions);
writeFileSync(join(game, "shape.gltf"), JSON.stringify({asset:{version:"2.0"},scene:0,scenes:[{nodes:[0]}],nodes:[{mesh:0}],
    meshes:[{primitives:[{attributes:{POSITION:0,NORMAL:1},indices:2}]}],buffers:[{uri:"shape.bin",byteLength:80}],
    bufferViews:[{buffer:0,byteLength:36},{buffer:0,byteOffset:36,byteLength:36},{buffer:0,byteOffset:72,byteLength:6}],
    accessors:[{bufferView:0,componentType:5126,count:3,type:"VEC3"},{bufferView:1,componentType:5126,count:3,type:"VEC3"},
        {bufferView:2,componentType:5123,count:3,type:"SCALAR"}]}));
let source = `struct CacheVertex { float3 position; float3 normal; float2 uv; float4 tangent; }
asset Mesh<CacheVertex> mesh = "shape.gltf";
asset List<Mesh<CacheVertex>> meshes = "mesh*.obj";
asset SkinnedMesh<Vertex3> skinned = "rig.fbx";
asset AnimationCatalog animations = "rig.fbx";
[Parts("Hat")] asset SkinnedMesh<Vertex3> parts = "parts.fbx";
asset Texture color = "image.dds";
[Raw] asset Texture raw = "image.dds";
asset Clip clip = "audio.mp3";
asset Grid2<Color> pixels = "picture.png";
asset Texture picture = "picture.png";
asset Texture solo = "solo.png";
asset Texture suffix = "suffix.tga";
scene Main { }
singleton State { int value; }
system Tick(mut State state) { state.value += 1; }
`;
const input = join(game, "game.tide");
writeFileSync(input, source);
async function build(directory=out, success=true, flags=[]) {
    mkdirSync(directory, { recursive: true });
    const child = spawn(tidec, [input,"--game",game,"-o",directory,"--depfile",join(directory,"game.d"),...flags],
        { env:{...process.env,TIDE_HIDDEN:"1"}, windowsHide:true });
    let said="";
    child.stdout.on("data",b=>said+=b); child.stderr.on("data",b=>said+=b);
    const code=await new Promise((accept,reject)=>{child.on("error",reject);child.on("close",accept);});
    assert(!/AddressSanitizer|runtime error:/.test(said),said);
    assert(success ? code===0 : code!==0,said);
    return said;
}
const digest = file=>createHash("sha256").update(readFileSync(file)).digest("hex");
const outputs = ()=>["game.c","game.h","game.pack"].map(n=>digest(join(out,n)));
const root = join(game,".tide","assets","imports");
// The folder of this compiler's importers: the only one a fresh game's cache has
function ownFolder() { const folders=readdirSync(root).filter(n=>statSync(join(root,n)).isDirectory()); assert.equal(folders.length,1,`${folders}`); return folders[0]; }
let imports;
const cacheFiles = ()=>readdirSync(imports).filter(n=>n.endsWith(".bin")).sort();
// An hour ago: recent enough that a read doesn't move it (a day's apart at the most), so a write would show
const old = new Date(Math.floor(Date.now()/1000)*1000-3600e3);
function pinCache() { for(const n of cacheFiles()) utimesSync(join(imports,n),old,old); }
function unchangedCache() { assert(cacheFiles().every(n=>statSync(join(imports,n)).mtimeMs===old.getTime()),"warm build rewrote an import cache"); }
await build();
imports=join(root,ownFolder());
assert(/^[0-9a-f]{16}$/.test(ownFolder()),"importer folder name");
assert(existsSync(join(imports,"used")),"tidy left no used marker");
const cold=outputs(); assert(cacheFiles().length>=7,"missing import/texture/audio/tangent entries");
pinCache(); await build(); assert.deepEqual(outputs(),cold,"warm output differs"); unchangedCache();
assert(readFileSync(join(out,"game.d"),"utf8").includes("shape.bin"),"cache hit lost its external buffer dependency");
source=source.replace("+= 1","+= 2"); writeFileSync(input,source);
await build(); unchangedCache(); assert.equal(outputs()[2],cold[2],"code-only edit repacked different asset bytes");
source=source.replace('asset Texture solo =','[Raw] asset Texture solo =');writeFileSync(input,source);
await build();const rawVariant=outputs();
// Force a full import and compare with the newly requested variant's lazy path.
const savedEarly=join(game,".tide","assets","early-imports");renameSync(root,savedEarly);writeFileSync(root,"blocked");
await build();assert.deepEqual(outputs(),rawVariant,"new Raw variant changed when fully decoded");remove(root);renameSync(savedEarly,root);
const beforeImage=outputs()[2], imageTime=statSync(join(game,"picture.png"));
copyFileSync(join(fixtures,"assets","run","1.png"),join(game,"picture.png"));utimesSync(join(game,"picture.png"),imageTime.atime,imageTime.mtime);
await build();assert.notEqual(outputs()[2],beforeImage,"image edit reused stale pixels");
const beforeSide=outputs()[2], sideTime=statSync(join(game,"shape.bin"));
positions.writeFloatLE(2,12);writeFileSync(join(game,"shape.bin"),positions);utimesSync(join(game,"shape.bin"),sideTime.atime,sideTime.mtime);
await build();assert.notEqual(outputs()[2],beforeSide,"same-size/time side-buffer edit reused stale geometry");
const beforeParts=outputs()[2];source=source.replace('[Parts("Hat")]','[Parts("Tri*")]');writeFileSync(input,source);
await build();assert.notEqual(outputs()[2],beforeParts,"part selection reused another selection's model");
pinCache(); const beforeLayout=outputs()[2]; source=source.replace("float4 tangent;","float4 tangent; float extra = 7;");writeFileSync(input,source);
await build();unchangedCache();assert.notEqual(outputs()[2],beforeLayout,"typed layout did not repack cached source geometry");
writeFileSync(join(game,"mesh2.obj"),obj.replace("v 1 0 0","v 3 0 0"));
await build();const added=outputs()[2];remove(join(game,"mesh2.obj"));await build();assert.notEqual(outputs()[2],added,"glob removal was ignored");
const valid=outputs();
for(const n of cacheFiles()) {const p=join(imports,n),b=readFileSync(p);b[b.length-1]^=1;writeFileSync(p,b);}
await build();assert.deepEqual(outputs(),valid,"damaged caches changed output");
for(const n of cacheFiles()) writeFileSync(join(imports,n),Buffer.alloc(3));
await build();assert.deepEqual(outputs(),valid,"truncated caches changed output");
// Corrupt the payload's counts while retaining a valid envelope checksum.
for(const n of cacheFiles()) {const p=join(imports,n),b=readFileSync(p);b.fill(255,64,Math.min(80,b.length));createHash("sha256").update(b.subarray(64)).digest().copy(b,32);writeFileSync(p,b);}
await build();assert.deepEqual(outputs(),valid,"malformed decoded caches changed output");
// An unusable cache destination must never make a valid game fail to build.
const backup=join(game,".tide","assets","saved-imports");renameSync(root,backup);writeFileSync(root,"not a directory");
await build();assert.deepEqual(outputs(),valid);remove(root);renameSync(backup,root);
// Separate output folders isolate generated files while sharing import writers.
remove(root);
await Promise.all([build(join(scratch,"parallel1")),build(join(scratch,"parallel2"))]);
assert.equal(digest(join(scratch,"parallel1","game.pack")),valid[2]);assert.equal(digest(join(scratch,"parallel2","game.pack")),valid[2]);
assert(readdirSync(imports).every(n=>n.endsWith(".bin")||n==="used"),"temporary cache entries leaked");
pinCache();await build();unchangedCache();assert.deepEqual(outputs(),valid);
// A read keeps an entry: one last read days ago is moved to now, its bytes as they were
const entryBytes=new Map(cacheFiles().map(n=>[n,digest(join(imports,n))])),daysAgo=new Date(Date.now()-3*86400e3);
for(const n of cacheFiles()) utimesSync(join(imports,n),daysAgo,daysAgo);
await build();assert.deepEqual(outputs(),valid);
const touched=cacheFiles().filter(n=>statSync(join(imports,n)).mtimeMs>Date.now()-3600e3);
assert(touched.length>=7,`read entries weren't marked used: ${touched.length}`);
assert(cacheFiles().every(n=>digest(join(imports,n))===entryBytes.get(n)),"marking an entry used changed it");
// Tidying, once a day: another importer's folder unused for weeks goes, a recent one stays, as do this importer's
// entries read lately; files of the old flat layout and entries unread for a month go
const seconds=Math.floor(Date.now()/1000);
const otherOld=join(root,"0000000000000000"),otherNew=join(root,"1111111111111111"),noMarker=join(root,"2222222222222222");
for(const [f,used] of [[otherOld,seconds-20*86400],[otherNew,seconds-86400],[noMarker,null]]) {
    mkdirSync(f);writeFileSync(join(f,"aa.bin"),"x");if(used!==null) writeFileSync(join(f,"used"),`${used}\n`);
}
writeFileSync(join(root,"flat.bin"),"x");
const unread=join(imports,"f".repeat(64)+".bin"),stray=join(imports,"e".repeat(64)+".bin.1-1.new");
writeFileSync(unread,"x");writeFileSync(stray,"x");
const monthAgo=new Date(Date.now()-40*86400e3),twoDays=new Date(Date.now()-2*86400e3);
utimesSync(unread,monthAgo,monthAgo);utimesSync(stray,twoDays,twoDays);
const kept=cacheFiles().filter(n=>!n.startsWith("f".repeat(8)));
await build();assert.deepEqual(outputs(),valid);
assert(existsSync(otherOld)&&existsSync(unread)&&existsSync(join(root,"flat.bin")),"tidied twice in one day");
writeFileSync(join(imports,"used"),`${seconds-2*86400}\n`);
await build();assert.deepEqual(outputs(),valid);
assert(!existsSync(otherOld),"a long-unused importer's folder was kept");
assert(!existsSync(noMarker),"a folder without a used marker was kept");
assert(existsSync(join(otherNew,"aa.bin")),"a recently used importer's folder was removed");
assert(!existsSync(join(root,"flat.bin")),"an old-layout entry was kept");
assert(!existsSync(unread),"an entry unread for a month was kept");
assert(!existsSync(stray),"a stopped compiler's temporary file was kept");
assert(kept.every(n=>existsSync(join(imports,n))),"tidying removed an entry in use");
assert(Number(readFileSync(join(imports,"used"),"utf8"))>=seconds,"tidying didn't mark the day");
remove(otherNew);
copyFileSync(join(game,"suffix.tga"),join(game,"suffix.bin"));
writeFileSync(input,source.replace('"suffix.tga"','"suffix.bin"'));
assert((await build(out,false)).includes("unrecognized image format"),"prepared texture bypassed written-suffix validation");
writeFileSync(input,source);
remove(join(game,"shape.gltf"));assert((await build(out,false)).includes("shape.gltf"),"removed source accepted from cache");
console.log("Persistent asset cache: cold/warm bytes, code/layout/parts/globs, external buffers, corruption, unwritable cache and concurrent writers pass.");
