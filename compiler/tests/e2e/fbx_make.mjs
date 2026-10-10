// Small specification-written FBX 7.4 files. ASCII and uncompressed binary
// encode the same node trees, independently of an installed modelling tool.
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "fbx");
const node = (name, props = [], children = []) => ({ name, props, children });
const array = (type, values) => ({ type, values });
const prop = (name, type, values) => node("P", [name, type, "", "A", ...values]);
const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
const translation = x => [...identity.slice(0, 12), x, 0, 0, 1];
const tick = 46186158000n;
const connect = (child, parent, property) => node("C", property ? ["OP", child, parent, property] : ["OO", child, parent]);
function model(id, name, type, position, scale = [1, 1, 1]) {
    return node("Model", [id, `Model::${name}`, type], [node("Version", [232]), node("Properties70", [], [
        prop("Lcl Translation", "Lcl Translation", position), prop("Lcl Rotation", "Lcl Rotation", [0, 0, 0]),
        prop("Lcl Scaling", "Lcl Scaling", scale),
    ]), node("Shading", [true]), node("Culling", ["CullingOff"])]);
}
function geometry() {
    return node("Geometry", [1000n, "Geometry::Triangle", "Mesh"], [
        node("GeometryVersion", [124]), node("Vertices", [array("d", [0, 0, 0, 2, 0, 0, 0, 2, 0])]),
        node("PolygonVertexIndex", [array("i", [0, 1, -3])]),
        node("LayerElementNormal", [0], [node("Version", [101]), node("Name", [""]),
            node("MappingInformationType", ["ByPolygonVertex"]), node("ReferenceInformationType", ["Direct"]),
            node("Normals", [array("d", [0, 0, 1, 0, 0, 1, 0, 0, 1])])]),
        node("LayerElementUV", [0], [node("Version", [101]), node("Name", ["UVMap"]),
            node("MappingInformationType", ["ByPolygonVertex"]), node("ReferenceInformationType", ["IndexToDirect"]),
            node("UV", [array("d", [0, 0, 1, 0, 0, 1])]), node("UVIndex", [array("i", [0, 1, 2])])]),
        node("LayerElementMaterial", [0], [node("Version", [101]), node("Name", [""]),
            node("MappingInformationType", ["AllSame"]), node("ReferenceInformationType", ["IndexToDirect"]),
            node("Materials", [array("i", [0])])]),
        node("Layer", [0], [node("Version", [100]), ...["Normal", "UV", "Material"].map(kind =>
            node("LayerElement", [], [node("Type", [`LayerElement${kind}`]), node("TypedIndex", [0])]))]),
    ]);
}
function document(skinned) {
    const objects = [geometry(), model(2000n, "Triangle", "Mesh", skinned ? [0, 0, 0] : [3, 4, 0], skinned ? [1, 1, 1] : [-1, 2, 1]),
        node("Material", [3000n, "Material::Blue", ""], [node("Version", [102]), node("ShadingModel", ["phong"]),
            node("MultiLayer", [0]), node("Properties70", [], [prop("DiffuseColor", "Color", [0.25, 0.5, 1]),
                prop("DiffuseFactor", "Number", [1])])])];
    const connections = [connect(1000n, 2000n), connect(2000n, 0n), connect(3000n, 2000n)];
    if (skinned) {
        objects.push(model(4000n, "Root", "Null", [2, 0, 0]), node("Deformer", [5000n, "Deformer::Skin", "Skin"], [
            node("Version", [101]), node("Link_DeformAcuracy", [50]), node("SkinningType", ["Linear"])]));
        connections.push(connect(4000n, 0n), connect(5000n, 1000n));
        for (let i = 0; i < 5; i++) {
            const bone = 4100n + BigInt(i), cluster = 5100n + BigInt(i);
            objects.push(model(bone, `Bone${i}`, "LimbNode", [i + 1, 0, 0]),
                node("Deformer", [cluster, `SubDeformer::Bone${i}`, "Cluster"], [node("Version", [100]),
                    node("Indexes", [array("i", [0, 1, 2])]), node("Weights", [array("d", [i + 1, i + 1, i + 1])]),
                    node("Transform", [array("d", translation(-(i + 3)))]), node("TransformLink", [array("d", translation(i + 3))]) ]));
            connections.push(connect(bone, 4000n), connect(cluster, 5000n), connect(bone, cluster));
        }
        objects.push(node("AnimationStack", [6000n, "AnimStack::Move", ""], [node("Properties70", [], [
            prop("LocalStart", "KTime", [0n]), prop("LocalStop", "KTime", [tick]),
            prop("ReferenceStart", "KTime", [0n]), prop("ReferenceStop", "KTime", [tick])])]),
            node("AnimationLayer", [6100n, "AnimLayer::BaseLayer", ""]),
            node("AnimationCurveNode", [6200n, "AnimCurveNode::T", ""], [node("Properties70", [], [
                prop("d|X", "Number", [1]), prop("d|Y", "Number", [0]), prop("d|Z", "Number", [0])])]),
            node("AnimationCurve", [6300n, "AnimCurve::X", ""], [node("Default", [1]), node("KeyVer", [4008]),
                node("KeyTime", [array("l", [0n, tick])]), node("KeyValueFloat", [array("f", [1, 5])]),
                node("KeyAttrFlags", [array("i", [4, 4])]), node("KeyAttrDataFloat", [array("f", [0, 0, 0, 0, 0, 0, 0, 0])]),
                node("KeyAttrRefCount", [array("i", [1, 1])]) ]));
        connections.push(connect(6100n, 6000n), connect(6200n, 6100n), connect(6200n, 4100n, "Lcl Translation"), connect(6300n, 6200n, "d|X"));
    }
    return [node("FBXHeaderExtension", [], [node("FBXHeaderVersion", [1003]), node("FBXVersion", [7400]), node("Creator", ["Tide FBX test fixture"])]),
        node("GlobalSettings", [], [node("Version", [1000]), node("Properties70", [], [
            ...[["UpAxis", 1], ["UpAxisSign", 1], ["FrontAxis", 2], ["FrontAxisSign", 1], ["CoordAxis", 0], ["CoordAxisSign", 1]]
                .map(([name, value]) => prop(name, "int", [value])), prop("UnitScaleFactor", "double", [100]),
            prop("OriginalUnitScaleFactor", "double", [100]), prop("TimeMode", "enum", [3])])]),
        node("Objects", [], objects), node("Connections", [], connections),
        node("Takes", [], [node("Current", [skinned ? "Move" : ""])])];
}
function ascii(n, depth = 0) {
    const prefix = "  ".repeat(depth);
    if (n.props.length === 1 && n.props[0]?.values) {
        const values = n.props[0].values;
        return `${prefix}${n.name}: *${values.length} {\n${prefix}  a: ${values.join(",")}\n${prefix}}\n`;
    }
    const values = n.props.map(value => typeof value === "string" ? JSON.stringify(value) : typeof value === "boolean" ? (value ? "T" : "F") : String(value));
    return `${prefix}${n.name}: ${values.join(", ")}${n.children.length ? ` {\n${n.children.map(child => ascii(child, depth + 1)).join("")}${prefix}}` : ""}\n`;
}
const word = value => { const b = Buffer.alloc(4); b.writeUInt32LE(value); return b; };
function property(value) {
    if (value?.values) {
        const width = { i: 4, l: 8, f: 4, d: 8 }[value.type], data = Buffer.alloc(value.values.length * width);
        value.values.forEach((v, i) => ({ i: () => data.writeInt32LE(v, i * width), l: () => data.writeBigInt64LE(v, i * width),
            f: () => data.writeFloatLE(v, i * width), d: () => data.writeDoubleLE(v, i * width) })[value.type]());
        return Buffer.concat([Buffer.from(value.type), word(value.values.length), word(0), word(data.length), data]);
    }
    if (typeof value === "string") { const data = Buffer.from(value); return Buffer.concat([Buffer.from("S"), word(data.length), data]); }
    if (typeof value === "boolean") return Buffer.from([67, value ? 1 : 0]);
    if (typeof value === "bigint") { const data = Buffer.alloc(9); data[0] = 76; data.writeBigInt64LE(value, 1); return data; }
    const data = Buffer.alloc(9); data[0] = 68; data.writeDoubleLE(value, 1); return data;
}
function binary(n, offset) {
    const name = Buffer.from(n.name), properties = Buffer.concat(n.props.map(value =>
        property(typeof value === "string" && value.includes("::") ? value.split("::").reverse().join("\0\x01") : value)));
    let end = offset + 13 + name.length + properties.length;
    const children = n.children.map(child => { const data = binary(child, end); end += data.length; return data; });
    if (children.length) { children.push(Buffer.alloc(13)); end += 13; }
    return Buffer.concat([word(end), word(n.props.length), word(properties.length), Buffer.from([name.length]), name, properties, ...children]);
}
function writePair(folder, name, tree) {
    mkdirSync(join(root, folder), { recursive: true });
    writeFileSync(join(root, folder, `${name}_ascii.fbx`), "; FBX 7.4.0 project file\n" + tree.map(n => ascii(n)).join(""));
    const header = Buffer.concat([Buffer.from("Kaydara FBX Binary  \0\x1a\0", "binary"), word(7400)]);
    let offset = header.length;
    const nodes = tree.map(n => { const data = binary(n, offset); offset += data.length; return data; });
    writeFileSync(join(root, folder, `${name}_binary.fbx`), Buffer.concat([header, ...nodes, Buffer.alloc(13)]));
}
for (const [folder, skinned] of [["static", false], ["models", true]])
    writePair(folder, skinned ? "rig" : "triangle", document(skinned));

const stepped = document(true);
const curve = stepped.find(n => n.name === "Objects").children.find(n => n.name === "AnimationCurve");
for (const child of curve.children) {
    if (child.name === "KeyTime") child.props = [array("l", [0n, tick / 2n, tick])];
    if (child.name === "KeyValueFloat") child.props = [array("f", [1, 3, 5])];
    if (child.name === "KeyAttrFlags") child.props = [array("i", [2, 2, 2])];
    if (child.name === "KeyAttrDataFloat") child.props = [array("f", Array(12).fill(0))];
    if (child.name === "KeyAttrRefCount") child.props = [array("i", [1, 1, 1])];
}
writePair("animation", "stepped", stepped);

const negative = document(true);
const negativeObjects = negative.find(n => n.name === "Objects").children;
const stackProps = negativeObjects.find(n => n.name === "AnimationStack").children[0].children;
for (const p of stackProps) p.props[4] = p.props[0].endsWith("Start") ? -tick : 0n;
negativeObjects.find(n => n.name === "AnimationCurve").children.find(n => n.name === "KeyTime").props = [array("l", [-tick, 0n])];
writePair("animation", "negative", negative);

mkdirSync(join(root, "bad"), { recursive: true });
writeFileSync(join(root, "bad", "malformed.fbx"), "; FBX 7.4.0 project file\nObjects: { Geometry: 1, \"Geometry::Broken\", \"Mesh\" { Vertices: *9 { a: 0,0\n");
