// Checks the TextMate grammar (../syntaxes/tide.tmLanguage.json) through
// Shiki, which the docs site highlights Tide with, as editors do: the scopes
// that matter in probe.tide's cases, and in every program of
// compiler/tests/e2e, that no text runs past its line and that declarations
// are scoped as declarations.
//
// It uses the docs site's Shiki: `npm ci`, then `npm run test:grammar` in docs/.

import assert from 'node:assert/strict';
import fs from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..', '..', '..');
const require = createRequire(path.join(repo, 'docs', 'package.json'));
const { createHighlighter } = await import(pathToFileURL(require.resolve('shiki')).href);

const grammar = JSON.parse(fs.readFileSync(path.join(here, '..', 'syntaxes', 'tide.tmLanguage.json'), 'utf8'));
const theme = 'github-dark';
const highlighter = await createHighlighter({ themes: [theme], langs: [{ ...grammar, name: 'tide' }] });

// Each line of `code`: its text, and its pieces with their scopes, outermost first.
function tokenize(code) {
    const texts = code.split(/\r?\n/);
    const { tokens } = highlighter.codeToTokens(code, { lang: 'tide', theme, includeExplanation: 'scopeName' });
    return tokens.map((line, i) => {
        const pieces = [];
        let at = 0;
        for (const token of line) {
            for (const part of token.explanation) {
                pieces.push({ start: at, end: at + part.content.length, text: part.content, scopes: part.scopes.map(s => s.scopeName) });
                at += part.content.length;
            }
        }
        assert.equal(pieces.map(p => p.text).join(''), texts[i].replace(/\r$/, ''), `line ${i + 1}'s pieces are its text`);
        return { number: i + 1, text: texts[i], pieces };
    });
}

// Whether `scopes` has `scope`, or a scope inside it (string has string.quoted.double.tide).
function has(scopes, scope) {
    return scopes.some(s => s === scope || s.startsWith(scope + '.'));
}

// The pieces of `line` that make up the `nth` `text` in it: a whole word, when
// it's a word (the 'in' that isn't in 'int').
function piecesOf(line, text, nth) {
    const word = /^\w+$/.test(text);
    const isWord = i => !word || (!/\w/.test(line.text[i - 1] ?? '') && !/\w/.test(line.text[i + text.length] ?? ''));
    let index = -1;
    for (let i = 0; i < nth; i++) {
        do index = line.text.indexOf(text, index + 1);
        while (index !== -1 && !isWord(index));
        assert.notEqual(index, -1, `line ${line.number} has "${text}" ${nth} times: ${line.text}`);
    }
    const end = index + text.length;
    return line.pieces.filter(p => p.start < end && p.end > index);
}

// Checks [text, scope, nth?]: every piece of the text has the scope, or with
// '!' before the scope, none has it.
function check(line, [text, scope, nth = 1]) {
    const not = scope.startsWith('!');
    const wanted = not ? scope.slice(1) : scope;
    for (const piece of piecesOf(line, text, nth)) {
        const found = has(piece.scopes, wanted);
        assert.ok(not ? !found : found,
            `line ${line.number}: "${text}"${nth > 1 ? ` (${nth})` : ''} ${not ? 'has' : "doesn't have"} ${wanted}: "${piece.text}" is ${piece.scopes.join(' ')}\n  ${line.text}`);
    }
}

// Text is one line (lexer.c), so no line starts inside a string.
function checkLinesEndText(file, lines) {
    for (const line of lines) {
        const first = line.pieces[0];
        if (!first) continue;
        assert.ok(!has(first.scopes, 'string'), `${file}:${line.number} starts inside text: ${line.text}`);
    }
}

const probe = tokenize(fs.readFileSync(path.join(here, 'probe.tide'), 'utf8'));

test('authored prefab and child declarations', () => {
    const lines = tokenize('prefab Enemy\n{\n    entity Weapon : Pistol\n    {\n        Transform;\n    }\n}');
    check(lines[0], ['prefab', 'storage.type']);
    check(lines[0], ['Enemy', 'entity.name.type']);
    check(lines[2], ['entity', 'storage.type']);
    check(lines[2], ['Weapon', 'entity.name.type']);
});

test('switch expression arm arrows', () => {
    const lines = tokenize('int result;\nresult = mode switch { 0 => 1, _ => 2 };');
    check(lines[1], ['switch', 'keyword.control']);
    check(lines[1], ['=>', 'keyword.operator.switch-arm', 1]);
    check(lines[1], ['=>', 'keyword.operator.switch-arm', 2]);
});

// The probe's line that starts with `start`, after its indentation.
function probeLine(start) {
    const found = probe.filter(l => l.text.trimStart().startsWith(start));
    assert.equal(found.length, 1, `probe.tide has one line that starts with "${start}"`);
    return found[0];
}

// Lines of probe.tide, and what their pieces' scopes have to be.
const cases = {
    'unclosed text ends with its line': [
        ['return $"score {score} and more;', [['$"score', 'string.quoted.double.interpolated'], ['score', 'meta.interpolation', 2]]],
        ['component AfterUnclosed {', [['component', 'storage.type'], ['AfterUnclosed', 'entity.name.type'], ['int', '!string']]],
        ['return $"score {score;', [['score;', 'meta.interpolation']]],
        ['component AfterUnclosedValue {', [['component', 'storage.type'], ['AfterUnclosedValue', 'entity.name.type']]],
        ['return "no end;', [['no end;', 'string.quoted.double']]],
        ['component AfterUnclosedPlain {', [['component', 'storage.type'], ['AfterUnclosedPlain', 'entity.name.type']]],
    ],
    'functions that return generic types and T?': [
        ['List<int> Doubled(', [
            ['List', 'support.class'], ['<', 'punctuation.definition.typeparameters.begin'], ['int', 'storage.type.primitive'],
            ['>', 'punctuation.definition.typeparameters.end'], ['Doubled', 'entity.name.function'], ['Doubled', '!entity.name.function.call'],
            ['List', 'support.class', 2], ['<', 'punctuation.definition.typeparameters.begin', 2], ['<', '!keyword.operator', 2],
        ]],
        ['mut List<int> doubled', [['mut', 'storage.modifier'], ['<', '!keyword.operator'], ['>', '!keyword.operator']]],
        ['List<Combat.Stats>? MaybeStats(', [
            ['Combat', 'entity.name.type'], ['Stats', 'entity.name.type'], ['?', 'keyword.operator.optional'],
            ['MaybeStats', 'entity.name.function'], ['MaybeStats', '!entity.name.function.call'],
        ]],
        ['int? Find(', [['int', 'storage.type.primitive'], ['?', 'keyword.operator.optional'], ['Find', 'entity.name.function'], ['Find', '!entity.name.function.call']]],
        ['extern List<int> Primes(', [['extern', 'storage.modifier'], ['List', 'support.class'], ['Primes', 'entity.name.function'], ['Primes', '!entity.name.function.call']]],
        ['extern float Noise(', [['extern', 'storage.modifier'], ['float', 'storage.type.primitive'], ['Noise', 'entity.name.function']]],
        ['extern void Fill(', [['in', 'storage.modifier'], ['in', '!keyword.control'], ['Stats', 'entity.name.type'], ['mut', 'storage.modifier']]],
        ['extern int Write(', [['out', 'storage.modifier'], ['List', 'support.class'], ['byte', 'storage.type.primitive'], ['out', '!storage.modifier', 2]]],
        ['extern system MoveInC(', [
            ['extern', 'storage.modifier'], ['system', 'storage.type'], ['system', '!entity.name.type'], ['MoveInC', 'entity.name.function'],
            ['mut', 'storage.modifier'], ['without', 'storage.modifier'],
        ]],
        ['const List<int> NONE', [['const', 'storage.modifier'], ['List', 'support.class'], ['NONE', 'variable.other.constant']]],
        ['const int MAX_HEALTH', [['const', 'storage.modifier'], ['MAX_HEALTH', 'variable.other.constant']]],
        ['asset Texture logo', [['asset', 'storage.modifier'], ['Texture', 'storage.type.primitive'], ['logo', 'variable.other'], ['logo', '!variable.other.constant']]],
        ['[Filter(Filter.Point)] asset Grid2<Color> tiles', [['asset', 'storage.modifier'], ['Grid2', 'support.class'], ['tiles', 'variable.other'], ['tiles', '!variable.other.constant']]],
        ['asset Map<string, Texture> frames', [['asset', 'storage.modifier'], ['Map', 'support.class'], ['frames', 'variable.other'], ['frames', '!variable.other.constant']]],
        ['audio Echo(', [['audio', 'storage.type'], ['Echo', 'entity.name.function'], ['mut', 'storage.modifier']]],
        ['Grid2<int> cells;', [['Grid2', 'support.class'], ['<', 'punctuation.definition.typeparameters.begin'], ['>', 'punctuation.definition.typeparameters.end']]],
        ['asset Mesh<Corner> tree = "art/tree.glb";', [['asset', 'storage.modifier'], ['Mesh', 'support.class'], ['<', 'punctuation.definition.typeparameters.begin'], ['tree', 'variable.other']]],
        ['asset SkinnedMesh<Corner, Locomotion> hero = "art/hero.glb";', [['asset', 'storage.modifier'], ['SkinnedMesh', 'support.class'], ['<', 'punctuation.definition.typeparameters.begin'], ['Locomotion', 'entity.name.type'], ['hero', 'variable.other']]],
        ['List<float> Weights()', [['Weights', 'entity.name.function'], ['Weights', '!entity.name.function.call']]],
        ['mut void Heal(', [['mut', 'storage.modifier'], ['Heal', 'entity.name.function']]],
        ['Stats operator +(', [['operator', 'keyword.other.operator'], ['+', 'keyword.operator']]],
    ],
    'declaration words are keywords only where a declaration starts': [
        ['input PlayerInput', [['input', 'storage.type'], ['PlayerInput', 'entity.name.type']]],
        ['system Steer(', [['system', 'storage.type'], ['Steer', 'entity.name.function'], ['input', '!storage'], ['events', '!storage']]],
        ['if (input is PlayerInput other)', [['input', '!storage'], ['input', '!keyword'], ['is', 'keyword.operator.expression.is'], ['is', '!entity.name.type']]],
        ['foreach (var event in events)', [['event', '!storage'], ['event', '!keyword'], ['in', 'keyword.control'], ['in', '!entity.name.type']]],
        ['var scene = 1;', [['scene', '!storage']]],
    ],
    'commands identify their payload, sender and receiver': [
        ['command Rename', [['command', 'storage.type'], ['Rename', 'entity.name.type']]],
        ['command(Rename rename, PlayerID sender) RenamePlayer(', [
            ['command', 'storage.type'], ['Rename', 'entity.name.type'], ['rename', 'variable.parameter'],
            ['PlayerID', 'storage.type.primitive'], ['sender', 'variable.parameter'], ['RenamePlayer', 'entity.name.function'],
            ['RenamePlayer', '!entity.name.function.call'],
        ]],
    ],
    'local and async, in either order': [
        ['local event Shown', [['local', 'storage.modifier'], ['event', 'storage.type'], ['Shown', 'entity.name.type']]],
        ['component Toast', [['component', 'storage.type'], ['Toast', 'entity.name.type']]],
        ['async local event(Shown) Fade(', [
            ['async', 'storage.modifier'], ['local', 'storage.modifier'], ['event', 'storage.type'], ['Shown', 'entity.name.type'],
            ['Fade', 'entity.name.function'], ['Fade', '!entity.name.function.call'],
        ]],
        ['local async event(Shown) Glow(', [['local', 'storage.modifier'], ['async', 'storage.modifier'], ['event', 'storage.type'], ['Glow', 'entity.name.function']]],
        ['async void Countdown()', [['async', 'storage.modifier'], ['void', 'storage.type.primitive'], ['Countdown', 'entity.name.function'], ['Wait', 'support.class'], ['await', 'keyword.control']]],
    ],
    'virtual and override, where a declaration starts': [
        ['virtual system Drift(', [['virtual', 'storage.modifier'], ['system', 'storage.type'], ['Drift', 'entity.name.function'], ['Drift', '!entity.name.function.call']]],
        ['virtual local async event(Shown) Blink(', [
            ['virtual', 'storage.modifier'], ['local', 'storage.modifier'], ['async', 'storage.modifier'], ['event', 'storage.type'],
            ['Shown', 'entity.name.type'], ['Blink', 'entity.name.function'], ['Blink', '!entity.name.function.call'],
        ]],
        ['override system Combat.Drive(', [
            ['override', 'storage.modifier'], ['system', 'storage.type'], ['Combat', 'entity.name.namespace'], ['.', 'punctuation.accessor'],
            ['Drive', 'entity.name.function'], ['Drive', '!entity.name.function.call'], ['mut', 'storage.modifier'],
        ]],
        ['override view Combat.Hud.Bar(', [['view', 'storage.type'], ['Combat', 'entity.name.namespace'], ['Hud', 'entity.name.namespace'], ['Bar', 'entity.name.function']]],
        ['override local event(Shown) Combat.Flash(', [
            ['override', 'storage.modifier'], ['local', 'storage.modifier'], ['event', 'storage.type'], ['Combat', 'entity.name.namespace'],
            ['Flash', 'entity.name.function'], ['Flash', '!entity.name.function.call'],
        ]],
        ['virtual command(Rename rename, PlayerID sender) Renamed(', [
            ['virtual', 'storage.modifier'], ['command', 'storage.type'], ['Rename', 'entity.name.type'], ['rename', 'variable.parameter'],
            ['PlayerID', 'storage.type.primitive'], ['sender', 'variable.parameter'], ['Renamed', 'entity.name.function'],
        ]],
        ['override command(Rename rename, PlayerID sender) Combat.OnRename(', [
            ['override', 'storage.modifier'], ['command', 'storage.type'], ['Combat', 'entity.name.namespace'], ['OnRename', 'entity.name.function'],
            ['OnRename', '!entity.name.function.call'],
        ]],
        ['virtual float Hurt(', [['virtual', 'storage.modifier'], ['float', 'storage.type.primitive'], ['Hurt', 'entity.name.function'], ['Hurt', '!entity.name.function.call']]],
        ['override float Combat.Harm(', [
            ['override', 'storage.modifier'], ['float', 'storage.type.primitive'], ['Combat', 'entity.name.type'], ['Harm', 'entity.name.function'],
            ['Harm', '!entity.name.function.call'],
        ]],
        ['override mut void Stats.Wound(', [['override', 'storage.modifier'], ['mut', 'storage.modifier'], ['Stats', 'entity.name.type'], ['Wound', 'entity.name.function']]],
        ['override frag Effects.Glow(', [['override', 'storage.modifier'], ['frag', 'storage.type'], ['Effects', 'entity.name.namespace'], ['Glow', 'entity.name.function']]],
        ['virtual bool IsGone()', [['virtual', 'storage.modifier'], ['bool', 'storage.type.primitive'], ['IsGone', 'entity.name.function']]],
        ['virtual mut void Drain(', [['virtual', 'storage.modifier'], ['mut', 'storage.modifier'], ['Drain', 'entity.name.function']]],
        ['var virtual = 1;', [['virtual', '!storage']]],
        ['var override = virtual + 1;', [['override', '!storage'], ['virtual', '!storage']]],
    ],
    'fails after the parameters or on a line of its own': [
        ['int Parse(string text) fails', [['fails', 'storage.modifier.fails'], ['ParseError', 'entity.name.type']]],
        ['fails ParseError', [['fails', 'storage.modifier.fails'], ['ParseError', 'entity.name.type']]],
        ['int ParseLong(', [['ParseLong', 'entity.name.function']]],
    ],
    'attributes side by side': [
        ['[After(Steer)] [Before(Fade)]', [
            ['After', 'entity.name.function.attribute'], ['Steer', 'entity.name.function'],
            ['Before', 'entity.name.function.attribute'], ['Fade', 'entity.name.function'], ['[', 'punctuation.definition.attribute.begin', 2],
        ]],
        ['[After(Steer), Before(Fade)] system SameLine()', [
            ['After', 'entity.name.function.attribute'], ['Before', 'entity.name.function.attribute'],
            ['system', 'storage.type'], ['SameLine', 'entity.name.function'],
        ]],
        ['[Stage(RenderStages.Objects, -10), Before(RenderStages.UI)] view DrawObjects()', [
            ['Stage', 'entity.name.function.attribute'], ['RenderStages', 'entity.name.function'],
            ['Objects', 'entity.name.function'], ['-', 'keyword.operator.arithmetic'], ['10', 'constant.numeric'],
            ['Before', 'entity.name.function.attribute'], ['UI', 'entity.name.function'],
            ['view', 'storage.type'], ['DrawObjects', 'entity.name.function'],
        ]],
        ['[Clamp(float2(0, -5), float2(10, 5))] float2 aim;', [
            ['Clamp', 'entity.name.function.attribute'], ['float2', 'storage.type.primitive'], ['float2', 'storage.type.primitive', 2],
            ['-', 'keyword.operator.arithmetic'], ['float2', 'storage.type.primitive', 3], ['aim', '!entity'],
        ]],
        ['[DontPredict] bool jump;', [['DontPredict', 'entity.name.function.attribute'], ['bool', 'storage.type.primitive']]],
        ['Predict() { jump = false; }', [['Predict', 'keyword.other']]],
        ['[Min(1)] [Max(3)] int gear', [['Min', 'entity.name.function.attribute'], ['Max', 'entity.name.function.attribute'], ['int', 'storage.type.primitive']]],
    ],
    'built-in types and groups': [
        ['enum Voxel : byte', [['enum', 'storage.type'], ['Voxel', 'entity.name.type'], [':', 'punctuation.separator.colon'], [':', '!keyword.operator'], ['byte', 'storage.type.primitive']]],
        ['Keyboard keyboard', [['Keyboard', 'support.class']]],
        ['Mouse mouse', [['Mouse', 'support.class']]],
        ['Gamepad pad', [['Gamepad', 'support.class']]],
        ['Button jump', [['Button', 'support.class']]],
        ['Dpad dpad', [['Dpad', 'support.class']]],
        ['float4x4 world', [['float4x4', 'storage.type.primitive']]],
        ['float2x3 wrong', [['float2x3', '!storage.type.primitive']]],
        ['int2x2 alsoWrong', [['int2x2', '!storage.type.primitive']]],
        ['Draw.Rect(', [['Draw', 'support.class'], ['Rect', '!storage.type'], ['Rect', 'entity.name.function.call'], ['Color', 'storage.type.primitive']]],
        ['Vertex corner', [['Vertex', 'support.class']]],
        ['Vertex3 corner3', [['Vertex3', 'support.class']]],
        ['Shape panel', [['Shape', 'support.class']]],
        ['GUIControl card', [['GUIControl', 'support.class']]],
        ['GUITheme look', [['GUITheme', 'support.class']]],
        ['Draw.Shape(panel)', [['Shape', '!support.class'], ['Shape', 'entity.name.function.call']]],
        ['Random dice = Random(7);', [['Random', 'support.class']]],
        ['var roll = dice.NextInt(6);', [['NextInt', 'entity.name.function.call']]],
        ['Draw.Mesh([corner]', [['Mesh', 'entity.name.function.call'], ['Filter', 'support.class']]],
        ['Draw.Mesh([corner3]', [['Mesh', 'entity.name.function.call'], ['Filter', 'support.class'], ['Wrap', 'support.class']]],
        ['Draw.Blend(Blend.Add),', [['Draw', 'support.class'], ['Blend', 'entity.name.function.call'], ['Blend', '!support.class'],
                                    ['Blend', 'support.class', 2]]],
        ['Draw.Clip(Rect(0, 0, 4, 4))', [['Clip', 'entity.name.function.call'], ['Rect', 'storage.type.primitive']]],
        ['Draw.SetTint(Color.red);', [['SetTint', 'entity.name.function.call']]],
        ['Draw.Depth(Depth.Read)', [['Draw', 'support.class'], ['Depth', 'entity.name.function.call'], ['Depth', '!support.class'],
                                      ['Depth', 'support.class', 2]]],
        ['Draw.SetDepth(Depth.None);', [['SetDepth', 'entity.name.function.call'], ['Depth', 'support.class']]],
        ['Draw.Cull(Cull.None)', [['Draw', 'support.class'], ['Cull', 'entity.name.function.call'], ['Cull', '!support.class'],
                                    ['Cull', 'support.class', 2]]],
        ['Draw.SetCull(Cull.Front);', [['SetCull', 'entity.name.function.call'], ['Cull', 'support.class']]],
        ['Draw.Stencil(StencilTest.Equal, 1)', [['Draw', 'support.class'], ['Stencil', 'entity.name.function.call'],
                                                ['StencilTest', 'support.class']]],
        ['Draw.SetStencil(StencilTest.Always, 2, StencilOp.Replace);', [['SetStencil', 'entity.name.function.call'],
                                                                        ['StencilOp', 'support.class']]],
        ['Draw.Text("DUEL"', [['Text', 'entity.name.function.call'], ['Anchor', 'support.class'], ['Font', 'support.class']]],
        ['extern void DrawUI(DrawList list);', [['DrawList', 'storage.type.primitive']]],
        ['RenderTarget scene; Texture image;', [['RenderTarget', 'storage.type.primitive'], ['Texture', 'storage.type.primitive']]],
        ['Draw.Blit(scene.color, Draw.window, Blur);', [['Blit', 'entity.name.function.call'], ['window', '!storage.type']]],
        ['if (GUI.Button("Copy")) Clipboard.Copy(', [['GUI', 'support.class'], ['Button', '!support.class'], ['Button', 'entity.name.function.call'], ['Clipboard', 'support.class']]],
        ['if (!Cursor.locked) Cursor.Lock();', [['Cursor', 'support.class'], ['locked', '!support.class'], ['Lock', 'entity.name.function.call']]],
        ['float step = Frame.dt;', [['Frame', 'support.class'], ['dt', '!support.class']]],
        ['async void SaveBytes(List<byte> bytes) fails IOError', [['List', 'support.class'], ['byte', 'storage.type.primitive'], ['IOError', 'support.class']]],
        ['var root = Application.persistentDataPath;', [['Application', 'support.class'], ['persistentDataPath', '!support.class']]],
        ['Directory.CreateDirectory(root)!;', [['Directory', 'support.class'], ['CreateDirectory', 'entity.name.function.call']]],
        ['try File.WriteAllBytes(Path.Combine(root,', [['File', 'support.class'], ['WriteAllBytes', 'entity.name.function.call'], ['Path', 'support.class'], ['Combine', 'entity.name.function.call']]],
        ['try await Storage.Flush();', [['Storage', 'support.class'], ['Flush', 'entity.name.function.call']]],
        ['fail IOError.OutOfSpace;', [['IOError', 'support.class'], ['OutOfSpace', '!support.class']]],
        ['[Json("max_players")] int maxPlayers;', [['Json', 'entity.name.function.attribute'], ['int', 'storage.type.primitive']]],
        ['var info = Json.Read<LobbyInfo>(text) ?? LobbyInfo { };', [['Json', 'support.class'], ['Read', '!support.class']]],
        ['var all = Json.Read<List<LobbyInfo>>(text.ToBytes()) ?? [];', [['Json', 'support.class'], ['List', 'support.class'],
                                                                         ['ToBytes', 'entity.name.function.call']]],
        ['if (int.TryParse(text.Split(",")[0], out var count)) return Json.Write(all);', [
            ['int', 'storage.type.primitive'], ['TryParse', 'entity.name.function.call'], ['Split', 'entity.name.function.call'],
            ['Write', 'entity.name.function.call']]],
        ['return string.FromBytes(text.ToBytes()) ?? Json.Write(info);', [['string', 'storage.type.primitive'],
                                                                          ['FromBytes', 'entity.name.function.call']]],
    ],
    "operators that aren't what they look like": [
        ['int value = Parse(text)!;', [['!', 'keyword.operator.or-default'], ['!', '!keyword.operator.logical']]],
        ['bool not = !true;', [['!', 'keyword.operator.logical']]],
        ['int either = true ? 1 : 2;', [['?', 'keyword.operator.ternary'], [':', 'keyword.operator.ternary']]],
        ['case 1:', [['case', 'keyword.control'], [':', 'punctuation.separator.colon'], [':', '!keyword.operator']]],
        ['default:', [['default', 'keyword.control'], [':', 'punctuation.separator.colon']]],
    ],
    'escapes': [
        ['string good =', [['\\"', 'constant.character.escape'], ['\\\\', 'constant.character.escape'], ['\\n', 'constant.character.escape'], ['"', '!invalid', 3]]],
        ['string bad =', [['\\t', 'invalid.illegal']]],
        ['string braces =', [['{{', 'constant.character.escape'], ['}}', 'constant.character.escape'], [':F2', 'constant.other.format'], ['value', 'meta.interpolation']]],
        ['string lone =', [['}', 'invalid.illegal']]],
        ['string badValue =', [['\\t', 'invalid.illegal'], ['value', 'meta.interpolation']]],
    ],
    'characters': [
        ['char letter =', [['char', 'storage.type.primitive'], ["'a'", 'string.quoted.single'], ['letter', '!string']]],
        ['char quote =', [["'\\''", 'string.quoted.single'], ["\\'", 'constant.character.escape'], [';', '!string']]],
        ['char tab =', [['\\t', 'constant.character.escape'], ['\\t', '!invalid']]],
        ['char wrong =', [['\\q', 'invalid.illegal']]],
        ['bool same =', [["'é'", 'string.quoted.single'], ["'\"'", 'string.quoted.single'], ['quote', '!string'], ['&&', '!string']]],
        ['char brace =', [["'{'", 'string.quoted.single']]],
    ],
    'directives': [
        ['#if TIDE_0_3_OR_NEWER', [
            ['#if', 'keyword.preprocessor'], ['TIDE_0_3_OR_NEWER', 'entity.name.variable.preprocessor.symbol'],
            ['&&', 'keyword.operator.logical'], ['!', 'keyword.operator.logical'], ['false', 'constant.language'],
            ['A newer tide', 'comment.line'],
        ]],
        ['#elif (TIDE_0_2_OR_NEWER)', [['#elif', 'keyword.preprocessor'], ['TIDE_0_2_OR_NEWER', 'entity.name.variable.preprocessor.symbol']]],
        ['#else', [['#else', 'keyword.preprocessor']]],
        ['#endif', [['#endif', 'keyword.preprocessor']]],
    ],
    "loops' headers": [
        ['parallel (var at in field.cells by 2 offset o)', [['parallel', 'keyword.control'], ['in', 'keyword.control'], ['by', 'keyword.control'], ['offset', 'keyword.control']]],
        ['foreach (var tick in Both(Ticks(), Ticks()))', [['in', 'keyword.control'], ['Both', 'entity.name.function.call'], ['in', '!keyword', 2]]],
    ],
    'query is a modifier in system parameters and a name elsewhere': [
        ['system BulletHits(', [
            ['system', 'storage.type'], ['BulletHits', 'entity.name.function'], ['query', 'storage.modifier.query'],
            ['Body', 'entity.name.type', 2], ['with', 'storage.modifier'], ['without', 'storage.modifier'],
            ['Fighter', 'entity.name.type'], ['Ghost', 'entity.name.type'], ['Time', 'support.class'],
        ]],
        ['query (', [['query', 'storage.modifier.query']]],
        ['view ShadowsOfFighters(', [
            ['view', 'storage.type'], ['ShadowsOfFighters', 'entity.name.function'], ['query', 'storage.modifier.query'],
            ['Platform', 'entity.name.type'], ['without', 'storage.modifier'], ['Ghost', 'entity.name.type'],
        ]],
        ['view SureOfFighters(', [
            ['query', 'storage.modifier.query'], ['verified', 'storage.modifier.verified', 2], ['with', 'storage.modifier'],
            ['Health', 'entity.name.type'], ['Fighter', 'entity.name.type'],
        ]],
        ['local event(Hit hit) ShowHitNear(', [
            ['local', 'storage.modifier'], ['event', 'storage.type'], ['Hit', 'entity.name.type'], ['hit', 'variable.parameter'],
            ['ShowHitNear', 'entity.name.function'], ['query', 'storage.modifier.query'], ['with', 'storage.modifier'],
            ['Fighter', 'entity.name.type'],
        ]],
        ['local event(verified Died) ShowDeathNear(', [
            ['verified', 'storage.modifier.verified'], ['Died', 'entity.name.type'], ['ShowDeathNear', 'entity.name.function'],
            ['mut', 'storage.modifier'], ['query', 'storage.modifier.query'], ['Spark', 'entity.name.type'],
        ]],
        ['with Fighter,', [['with', 'storage.modifier'], ['Fighter', 'entity.name.type']]],
        ['without Ghost', [['without', 'storage.modifier'], ['Ghost', 'entity.name.type']]],
        ['int query(', [['query', 'entity.name.function'], ['query', '!storage.modifier']]],
        ['int QueryName(', [['query', '!storage.modifier']]],
        ['void CallQuery(', [['query', 'entity.name.function.call'], ['query', '!storage.modifier']]],
    ],
    "the World, and a method's type in <>": [
        ['system Track(', [['mut', 'storage.modifier'], ['World', 'support.class'], ['world', '!support']]],
        ['var target = world.Get<Health>(', [
            ['Get', 'support.function'], ['<', 'punctuation.definition.typeparameters.begin'], ['<', '!keyword.operator'],
            ['Health', 'entity.name.type'], ['>', 'punctuation.definition.typeparameters.end'], ['>', '!keyword.operator'],
        ]],
        ['if (world.Has<Combat.Stats>(', [
            ['Has', 'support.function'], ['Combat', 'entity.name.type'], ['Stats', 'entity.name.type'],
            ['<', 'punctuation.definition.typeparameters.begin'], ['>', 'punctuation.definition.typeparameters.end'],
            ['<', 'keyword.operator.comparison', 2], ['>', 'keyword.operator.comparison', 2],
        ]],
        ['world.Set<Health>(', [['Set', 'support.function'], ['Health', 'entity.name.type']]],
        ['var lookup =', [['lookup', '!storage'], ['<', 'keyword.operator.comparison']]],
    ],
    'ownership is a keyword only right after with and without': [
        ['view Mine(', [['with', 'storage.modifier'], ['ownership', 'keyword.other.ownership']]],
        ['view Rest(', [
            ['with', 'storage.modifier'], ['Owner', 'support.class'], ['without', 'storage.modifier'],
            ['ownership', 'keyword.other.ownership'],
        ]],
        ['view Flagged(', [
            ['with', 'storage.modifier'], ['ownership', '!keyword'], ['Flag', 'entity.name.type'],
            ['with', 'storage.modifier', 2], ['ownership', '!keyword', 2],
        ]],
        ['int Owned(', [['ownership', '!keyword'], ['ownership', '!keyword', 2]]],
    ],
    "verified is a modifier before a view's parameter, after with and without, and before a local handler's event": [
        ['view HealthBar(', [
            ['verified', 'storage.modifier.verified'], ['Health', 'entity.name.type', 2], ['confirmed', '!storage'],
            ['mut', 'storage.modifier'], ['verified', 'storage.modifier.verified', 2], ['Combat', 'entity.name.type'],
            ['Armor', 'entity.name.type'],
        ]],
        ['view Corpses(', [
            ['with', 'storage.modifier'], ['verified', 'storage.modifier.verified'], ['Dead', 'entity.name.type'],
            ['without', 'storage.modifier'], ['verified', 'storage.modifier.verified', 2], ['Ghost', 'entity.name.type'],
        ]],
        ['verified Body sure,', [['verified', 'storage.modifier.verified'], ['Body', 'entity.name.type'], ['sure', '!storage']]],
        ['mut verified Mesh baked,', [['mut', 'storage.modifier'], ['verified', 'storage.modifier.verified'], ['Mesh', 'entity.name.type']]],
        ['without verified Dead)', [['without', 'storage.modifier'], ['verified', 'storage.modifier.verified'], ['Dead', 'entity.name.type']]],
        ['local event(verified Died died) ShowDeath(', [
            ['local', 'storage.modifier'], ['event', 'storage.type'], ['verified', 'storage.modifier.verified'],
            ['Died', 'entity.name.type'], ['died', 'variable.parameter'], ['ShowDeath', 'entity.name.function'],
            ['ShowDeath', '!entity.name.function.call'],
        ]],
        ['local event(verified Combat.Died) CountDeath(', [
            ['verified', 'storage.modifier.verified'], ['Combat', 'entity.name.type'], ['Died', 'entity.name.type'],
            ['CountDeath', 'entity.name.function'], ['CountDeath', '!entity.name.function.call'],
        ]],
    ],
    'verified is a name anywhere else': [
        ['local event(verified) HearVerified(', [
            ['verified', '!storage.modifier'], ['verified', 'entity.name.type'], ['HearVerified', 'entity.name.function'],
            ['HearVerified', '!entity.name.function.call'],
        ]],
        ['view Trusted(', [['verified', '!storage.modifier'], ['with', 'storage.modifier'], ['verified', '!storage.modifier', 2]]],
        ['int Checked(', [
            ['verified', '!storage.modifier'], ['verified', '!storage.modifier', 2], ['is', 'keyword.operator.expression.is'],
            ['verified', '!storage.modifier', 3], ['verified', '!storage.modifier', 4],
        ]],
        ['bool verified;', [['verified', '!storage.modifier']]],
        ['mut void Prove(', [['mut', 'storage.modifier'], ['verified', '!storage.modifier']]],
        ['mut verified tally', [['mut', 'storage.modifier'], ['verified', '!storage.modifier']]],
        ['verified = tally;', [['verified', '!storage.modifier']]],
    ],
    'a field that is a T?': [
        ['string? wished;', [['string', 'storage.type.primitive'], ['?', 'keyword.operator.optional'], ['?', '!keyword.operator.ternary']]],
        ['List<int>? wishes = null;', [['List', 'support.class'], ['int', 'storage.type.primitive'], ['?', 'keyword.operator.optional'], ['null', 'constant.language']]],
        ['Combat.Stats? granted;', [['Stats', 'entity.name.type'], ['?', 'keyword.operator.optional']]],
        ['float3? toward = float3(0, 1, 0);', [['float3', 'storage.type.primitive'], ['?', 'keyword.operator.optional'], ['1', 'constant.numeric']]],
        ['void Grant(', [['null', 'constant.language'], ['??', 'keyword.operator'], ['is', 'keyword']]],
    ],
    'local before a field is a modifier, and a name anywhere else': [
        ['local List<Vertex3> verts;', [['local', 'storage.modifier'], ['Vertex3', '!storage.modifier']]],
        ['local float fade = 1;', [['local', 'storage.modifier'], ['float', 'storage.type'], ['1', 'constant.numeric']]],
        ['local Combat.Stats stats;', [['local', 'storage.modifier']]],
        ['local int? picked;', [['local', 'storage.modifier']]],
        ['int local;', [['local', '!storage.modifier']]],
        ['view Bake(', [['local', '!storage.modifier'], ['local', '!storage.modifier', 2]]],
    ],
    'frag and vert are keywords only where a declaration starts': [
        ['frag Shade(', [
            ['frag', 'storage.type'], ['Shade', 'entity.name.function'], ['Shade', '!entity.name.function.call'],
            ['BoxVertex', 'entity.name.type'], ['out', 'storage.modifier'], ['Color', 'storage.type.primitive'],
            ['Box', 'entity.name.type'],
        ]],
        ['vert Wave(', [
            ['vert', 'storage.type'], ['Wave', 'entity.name.function'], ['Wave', '!entity.name.function.call'],
            ['mut', 'storage.modifier'], ['out', 'storage.modifier'], ['FromLamp', 'entity.name.type'], ['lamp', '!storage'],
            ['Time', 'support.class'], ['Instance', 'entity.name.type'],
        ]],
        ['float Blend(', [['Blend', 'entity.name.function'], ['frag', '!storage'], ['vert', '!storage']]],
        ['float both = frag + vert;', [['frag', '!storage'], ['frag', '!entity'], ['vert', '!storage'], ['vert', '!entity']]],
        ['frag = both;', [['frag', '!storage'], ['frag', '!entity'], ['both', '!entity']]],
        ['return vert * both;', [['return', 'keyword.control'], ['vert', '!storage']]],
        ['color = box.wood.Sample(', [['Sample', 'entity.name.function.call'], ['Sample', '!keyword']]],
        ['if (this.position.x < 10) this.Discard();', [
            ['this', 'variable.language.this'], ['this', 'variable.language.this', 2], ['Discard', 'entity.name.function.call'],
        ]],
        ['Draw.Mesh(models.crate', [['Draw', 'support.class'], ['Mesh', 'entity.name.function.call'], ['Wave', '!storage'], ['Shade', '!storage']]],
        ['Draw.Cube(', [['Draw', 'support.class'], ['Cube', 'entity.name.function.call']]],
    ],
    'arguments by name, defaults, and a shared name with its parameters': [
        ['float Strike(float base, float multiplier = 1', [
            ['Strike', 'entity.name.function'], ['Strike', '!entity.name.function.call'], ['=', 'keyword.operator.assignment'],
            ['multiplier', '!variable.parameter'], ['false', 'constant.language.boolean'],
        ]],
        ['return Strike(10, crit: true)', [
            ['crit', 'variable.parameter'], [':', 'punctuation.separator.colon'], [':', '!keyword.operator'],
            ['true', 'constant.language.boolean'],
        ]],
        ['return Strike(base: 1, multiplier: on ? 2 : 3)', [
            ['base', 'variable.parameter'], ['multiplier', 'variable.parameter'], [':', 'punctuation.separator.colon', 2],
            ['?', 'keyword.operator.ternary'], [':', 'keyword.operator.ternary', 3], ['on', '!variable.parameter'],
        ]],
        ['[After(Advance(Body, with Player))]', [
            ['After', 'entity.name.function.attribute'], ['Body', 'entity.name.type'], ['with', 'storage.modifier'],
            ['Player', 'entity.name.type'],
        ]],
    ],
    "out is a modifier before a parameter's type and before the argument for one": [
        ['bool TryHalf(int value, out int half)', [
            ['TryHalf', 'entity.name.function'], ['out', 'storage.modifier'], ['int', 'storage.type.primitive', 2], ['half', '!storage'],
        ]],
        ['bool TryFind(Entity target, out Combat.Health? found', [
            ['out', 'storage.modifier'], ['Combat', 'entity.name.type'], ['Health', 'entity.name.type'], ['?', 'keyword.operator.optional'],
            ['out', 'storage.modifier', 2], ['Map', 'support.class'], ['found', '!storage'],
        ]],
        ['bool TryPower(int least, out int found)', [['TryPower', 'entity.name.function'], ['out', 'storage.modifier']]],
        ['out List<int> halves)', [['out', 'storage.modifier'], ['List', 'support.class'], ['halves', '!storage']]],
        ['if (TryHalf(8, out var declared))', [['out', 'storage.modifier'], ['var', 'storage.type.var'], ['declared', '!storage']]],
        ['TryHalf(8, out Health typed);', [['out', 'storage.modifier'], ['Health', 'entity.name.type'], ['typed', '!storage']]],
        ['TryHalf(8, out mut var changed);', [['out', 'storage.modifier'], ['mut', 'storage.modifier'], ['var', 'storage.type.var']]],
        ['TryHalf(8, out existing);', [['out', 'storage.modifier'], ['existing', '!storage']]],
        ['TryHalf(8, out body.hits);', [['out', 'storage.modifier'], ['body', '!storage'], ['hits', '!storage']]],
        ['TryHalf(8, out _);', [['out', 'storage.modifier'], ['_', '!storage']]],
        ['TryHalf(value: 8, half: out var named);', [
            ['value', 'variable.parameter'], ['half', 'variable.parameter'], [':', 'punctuation.separator.colon', 2],
            ['out', 'storage.modifier'], ['var', 'storage.type.var'], ['named', '!storage'],
        ]],
        ['out var wrapped);', [['out', 'storage.modifier'], ['var', 'storage.type.var']]],
        ['if (scores.TryGet(key, out var score))', [['TryGet', 'entity.name.function.call'], ['out', 'storage.modifier'], ['var', 'storage.type.var']]],
    ],
    'out is a name anywhere else': [
        ['mut Set<int> out = [];', [['mut', 'storage.modifier'], ['Set', 'support.class'], ['out', '!storage.modifier']]],
        ['Write(out);', [['out', '!storage.modifier']]],
        ['Write(out, 1);', [['out', '!storage.modifier']]],
        ['Write(1, out);', [['out', '!storage.modifier']]],
        ['out.Add(out.count + 1);', [['out', '!storage.modifier'], ['out', '!storage.modifier', 2]]],
        ['var either = on ? 1 : out;', [['out', '!storage.modifier']]],
        ['if (out is Stats made) return;', [['out', '!storage.modifier'], ['is', 'keyword.operator.expression.is']]],
        ['Write(count: out);', [['count', 'variable.parameter'], ['out', '!storage.modifier']]],
    ],    'namespace blocks': [
        ['namespace ProbeBlocks.Inner', [['namespace', 'keyword.other.namespace'], ['ProbeBlocks', 'entity.name.namespace'], ['Inner', 'entity.name.namespace']]],
        ['component InBlock {', [['component', 'storage.type'], ['InBlock', 'entity.name.type']]],
        ['int BlockFunction(', [['int', 'storage.type.primitive'], ['BlockFunction', 'entity.name.function']]],
        ['namespace Deeper', [['namespace', 'keyword.other.namespace'], ['Deeper', 'entity.name.namespace']]],
        ['system InDeeper(', [['system', 'storage.type'], ['InDeeper', 'entity.name.function'], ['InBlock', 'entity.name.type']]],
    ],
};

for (const [name, lines] of Object.entries(cases)) {
    test(name, () => {
        for (const [start, checks] of lines) {
            const line = probeLine(start);
            for (const c of checks) check(line, c);
        }
    });
}

test('no text in probe.tide runs past its line', () => checkLinesEndText('probe.tide', probe));

test('degree literals are whole float tokens with a lowercase suffix', () => {
    const lines = tokenize('float degrees = 180deg + .5deg + 1e2deg + 1_8_0deg + 1.2_5e+1deg - 90deg;\n'
        + 'quaternion turn = quaternion.Euler(0, 180deg, 0);\n'
        + 'float ordinary = .5f + 1.5 + 1e2;\n'
        + 'float names = turn180deg + deg + 180DEG + 180Deg + 180degExtra;\n'
        + 'string text = "180deg .5deg"; // 90deg\n'
        + 'float field = value.5deg;');
    for (const text of ['180deg', '.5deg', '1e2deg', '1_8_0deg', '1.2_5e+1deg', '90deg']) {
        check(lines[0], [text, 'constant.numeric.float']);
        assert.equal(piecesOf(lines[0], text, 1).map(p => p.text).join(''), text, `${text} is one whole numeric token`);
    }
    check(lines[0], ['-', 'keyword.operator.arithmetic']);
    check(lines[1], ['180deg', 'constant.numeric.float']);
    for (const text of ['.5f', '1.5', '1e2']) check(lines[2], [text, 'constant.numeric.float']);
    for (const text of ['turn180deg', 'deg', '180DEG', '180Deg', '180degExtra']) check(lines[3], [text, '!constant.numeric.float']);
    check(lines[4], ['180deg', 'string.quoted.double']);
    check(lines[4], ['90deg', 'comment.line']);
    check(lines[5], ['.5deg', '!constant.numeric.float']);
});

test('leading-dot enum members retain their value scope', () => {
    const lines = tokenize('state = .Idle;\nreturn .Run;\ncase .idle:\nPick(.Jump);\nstate == .Run;\nvar n = .5;\nstate.idle;');
    for (const [i, name] of ['Idle', 'Run', 'idle', 'Jump', 'Run'].entries()) check(lines[i], [name, 'constant.other.enum']);
    check(lines[5], ['.5', 'constant.numeric.float']);
    check(lines[6], ['idle', 'variable.other.member']);
});

// The editors indent after a line that opens a brace, but not one in text or a comment.
test('indenting ignores braces in text and comments', () => {
    const configuration = JSON.parse(fs.readFileSync(path.join(here, '..', 'language-configuration.json'), 'utf8'));
    const increase = new RegExp(configuration.indentationRules.increaseIndentPattern);
    const lines = [
        ['{', true],
        ['    if (x) {', true],
        ['    if (x) { // go', true],
        ['    if (x) { // the } closes it', true],
        ['    Foo("{") {', true],
        ['    Spawn(Body {', true],
        ['system Foo()', false],
        ['component A { int x; }', false],
        ['    Foo("{");', false],
        ["    char open = '{';", false],
        ["    if (c == '{') {", true],
        ['    string s = $"{x}";', false],
        ['    string s = $"{{";', false],
        ['    // see {', false],
        ['    x = 1; // {', false],
        ['    /* { */', false],
    ];
    for (const [line, indents] of lines) assert.equal(increase.test(line), indents, `indents after: ${line}`);
});

// Every program in compiler/tests/e2e, with its declarations found without the
// grammar: data declarations, systems, views, frags, verts, handlers,
// functions, constants and externs at the start of a line, and methods in a
// type's body, which the formatter indents by four spaces. An override's name
// can be written with what it's in (Combat.Move, Stats.Hurt): its last part
// is the name.
const e2e = path.join(repo, 'compiler', 'tests', 'e2e');
const programs = fs.readdirSync(e2e, { recursive: true })
    .filter(f => f.endsWith('.tide') && fs.statSync(path.join(e2e, f)).isFile()).sort();

const modifiers = String.raw`(?:(?:local|async|virtual|override)\s+)*`;
const type = String.raw`[\w.]+(?:\s*<[\w.<>\s]*>)?\??`;
const keywords = new Set(['return', 'if', 'else', 'var', 'await', 'fail', 'try', 'case', 'switch', 'while', 'for', 'foreach', 'parallel']);
const declarations = [
    [new RegExp(String.raw`^${modifiers}(?:component|singleton|struct|input|event|command|enum|scene)\s+(\w+)`), 'entity.name.type'],
    [new RegExp(String.raw`^${modifiers}(?:system|view|frag|vert)\s+([\w.]+)`), 'entity.name.function'],
    [new RegExp(String.raw`^${modifiers}(?:event|command)\s*\([^)]*\)\s*([\w.]+)`), 'entity.name.function'],
    [new RegExp(String.raw`^const\s+${type}\s+(\w+)\s*=`), 'variable.other.constant'],
    [new RegExp(String.raw`^extern\s+${type}\s+(\w+)\s*\(`), 'entity.name.function'],
    [new RegExp(String.raw`^${modifiers}(?:mut\s+)?(${type})\s+([\w.]+)\s*\(`), 'entity.name.function'],
];
const method = new RegExp(String.raw`^    (?:virtual\s+)?(?:mut\s+)?(${type})\s+(\w+)\s*\(`);

for (const program of programs) {
    test(`e2e/${program.replaceAll('\\', '/')}`, () => {
        const lines = tokenize(fs.readFileSync(path.join(e2e, program), 'utf8'));
        checkLinesEndText(program, lines);
        let inType = false; // In a component's, singleton's, struct's, input's or scene's body
        let found = 0;
        const blocks = []; // The namespace blocks open: how far in each one's declarations are
        for (const line of lines) {
            const whole = line.text.replace(/\r$/, '');
            // Inside `namespace Name { ... }`, declarations are a step in: read as they'd be at a file's top
            const shift = blocks.length > 0 ? blocks[blocks.length - 1] : 0;
            const opens = /^( *)namespace\s+[\w.]+\s*\{?$/.exec(whole);
            if (opens) {
                blocks.push(opens[1].length + 4);
                continue;
            }
            if (shift > 0 && whole === ' '.repeat(shift - 4) + '}') {
                blocks.pop();
                continue;
            }
            const text = whole.startsWith(' '.repeat(shift)) ? whole.slice(shift) : whole;
            for (const piece of line.pieces) {
                assert.ok(!has(piece.scopes, 'invalid'), `${program}:${line.number}: "${piece.text}" is ${piece.scopes.join(' ')}`);
            }
            if (/^(?:local\s+)?(?:component|singleton|struct|input|scene)\b/.test(text) && !text.includes('}')) inType = true;
            else if (text === '}') inType = false;
            let name = null;
            let scope = null;
            for (const [pattern, wanted] of declarations) {
                const match = pattern.exec(text);
                if (!match) continue;
                const last = match[match.length - 1];
                if (pattern === declarations[5][0] && keywords.has(match[1])) break;
                name = last.split('.').pop();
                scope = wanted;
                break;
            }
            const inside = inType && method.exec(text);
            if (inside && !keywords.has(inside[1])) {
                name = inside[2];
                scope = 'entity.name.function';
            }
            if (!name) continue;
            found++;
            const index = text.search(new RegExp(String.raw`\b${name}\b`)) + (whole.length - text.length);
            const pieces = line.pieces.filter(p => p.start < index + name.length && p.end > index);
            for (const piece of pieces) {
                assert.ok(has(piece.scopes, scope) && !has(piece.scopes, 'entity.name.function.call'),
                    `${program}:${line.number}: ${name} is ${piece.scopes.join(' ')}, not ${scope}\n  ${text}`);
            }
        }
        assert.ok(found > 0 || program.includes('empty'), `${program} has declarations`);
    });
}
