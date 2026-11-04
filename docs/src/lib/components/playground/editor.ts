// The playground's code editor: Monaco with the editors' Tide grammar
// (through Shiki, as the docs' code blocks), and the real language server,
// tidels compiled to WebAssembly, for completion, hovers and diagnostics.
// Browser-only: Playground.svelte imports it once the page has mounted.

import * as monaco from 'monaco-editor/editor/editor.api';
// The editor features the playground uses, without Monaco's other languages.
import 'monaco-editor/features/codicon/register';
import 'monaco-editor/features/codeEditor/register';
import 'monaco-editor/features/bracketMatching/register';
import 'monaco-editor/features/caretOperations/register';
import 'monaco-editor/features/clipboard/register';
import 'monaco-editor/features/comment/register';
import 'monaco-editor/features/contextmenu/register';
import 'monaco-editor/features/cursorUndo/register';
import 'monaco-editor/features/dropOrPasteInto/register';
import 'monaco-editor/features/find/register';
import 'monaco-editor/features/folding/register';
import 'monaco-editor/features/format/register';
import 'monaco-editor/features/gotoError/register';
import 'monaco-editor/features/gotoSymbol/register';
import 'monaco-editor/features/hover/register';
import 'monaco-editor/features/indentation/register';
import 'monaco-editor/features/linesOperations/register';
import 'monaco-editor/features/multicursor/register';
import 'monaco-editor/features/parameterHints/register';
import 'monaco-editor/features/rename/register';
import 'monaco-editor/features/smartSelect/register';
import 'monaco-editor/features/snippet/register';
import 'monaco-editor/features/suggest/register';
import 'monaco-editor/features/wordHighlighter/register';
import 'monaco-editor/features/wordOperations/register';
// Its feature module brings the suggestions' model, not their widget.
import 'monaco-editor/editor/contrib/suggest/browser/suggestController';
import EditorWorker from 'monaco-editor/editor/editor.worker?worker';
import { createHighlighterCore } from 'shiki/core';
import { createOnigurumaEngine } from 'shiki/engine/oniguruma';
import { shikiToMonaco } from '@shikijs/monaco';
import githubLight from 'shiki/themes/github-light.mjs';
import githubDark from 'shiki/themes/github-dark.mjs';
import grammar from '../../../../../tools/tide-syntax/syntaxes/tide.tmLanguage.json' with { type: 'json' };

const DOCUMENT = 'file:///game.tide';
const LIGHT = 'tide-light';
const DARK = 'tide-dark';

// Monaco's own worker, for its word and link computations.
(self as unknown as { MonacoEnvironment: monaco.Environment }).MonacoEnvironment = { getWorker: () => new EditorWorker() };

// The docs' code colors, on the docs' code background.
function theme(base: typeof githubLight, name: string, background: string) {
    return { ...base, name, colors: { ...base.colors, 'editor.background': background, 'editor.lineHighlightBackground': '#80808014' } };
}

let languageReady: Promise<void> | undefined;
function registerLanguage(): Promise<void> {
    languageReady ??= (async () => {
        monaco.languages.register({ id: 'tide', extensions: ['.tide'] });
        monaco.languages.setLanguageConfiguration('tide', {
            comments: { lineComment: '//', blockComment: ['/*', '*/'] },
            brackets: [['{', '}'], ['[', ']'], ['(', ')']],
            autoClosingPairs: [
                { open: '{', close: '}' }, { open: '[', close: ']' }, { open: '(', close: ')' },
                { open: '"', close: '"', notIn: ['string'] }, { open: "'", close: "'", notIn: ['string'] },
                { open: '/*', close: ' */', notIn: ['string'] },
            ],
            surroundingPairs: [{ open: '{', close: '}' }, { open: '[', close: ']' }, { open: '(', close: ')' }, { open: '"', close: '"' }],
            indentationRules: { increaseIndentPattern: /^.*\{[^}"']*$/, decreaseIndentPattern: /^\s*\}/ },
        });
        const highlighter = await createHighlighterCore({
            themes: [theme(githubLight, LIGHT, '#f6f6f7'), theme(githubDark, DARK, '#161618')],
            langs: [{ ...(grammar as object), name: 'tide' } as never],
            engine: createOnigurumaEngine(import('shiki/wasm')),
        });
        shikiToMonaco(highlighter, monaco as never);
    })();
    return languageReady;
}

// --- The language server, in its worker ------------------------------------

type Position = { line: number; character: number };
type Range = { start: Position; end: Position };
type Markup = string | { kind?: string; language?: string; value: string };

function toRange(range: Range): monaco.IRange {
    return {
        startLineNumber: range.start.line + 1, startColumn: range.start.character + 1,
        endLineNumber: range.end.line + 1, endColumn: range.end.character + 1,
    };
}

function toPosition(position: monaco.Position): Position {
    return { line: position.lineNumber - 1, character: position.column - 1 };
}

function toMarkdown(content: Markup | Markup[] | undefined): monaco.IMarkdownString[] {
    if (!content) return [];
    if (Array.isArray(content)) return content.flatMap(toMarkdown);
    if (typeof content === 'string') return [{ value: content }];
    if (content.language) return [{ value: '```' + content.language + '\n' + content.value + '\n```' }];
    return [{ value: content.value }];
}

function documentation(content: Markup | undefined): monaco.IMarkdownString | string | undefined {
    if (!content) return undefined;
    return typeof content === 'string' ? content : { value: content.value };
}

// LSP's CompletionItemKind, 1 to 25, by Monaco's names for them.
const KINDS = ['Text', 'Method', 'Function', 'Constructor', 'Field', 'Variable', 'Class', 'Interface', 'Module',
    'Property', 'Unit', 'Value', 'Enum', 'Keyword', 'Snippet', 'Color', 'File', 'Reference', 'Folder', 'EnumMember',
    'Constant', 'Struct', 'Event', 'Operator', 'TypeParameter'] as const;

const SEVERITY = [monaco.MarkerSeverity.Error, monaco.MarkerSeverity.Error, monaco.MarkerSeverity.Warning,
    monaco.MarkerSeverity.Info, monaco.MarkerSeverity.Hint];

export interface Problem {
    line: number;
    column: number;
    severity: 'error' | 'warning' | 'info';
    message: string;
}

class Language {
    private worker: Worker;
    private next = 1;
    private waiting = new Map<number, (result: unknown) => void>();
    private sent: string;
    private timer: ReturnType<typeof setTimeout> | undefined;
    failed: string | undefined;

    constructor(private model: monaco.editor.ITextModel, private problems: (problems: Problem[]) => void) {
        this.worker = new Worker(new URL('../../../../playground/language-worker.mjs', import.meta.url), { type: 'module' });
        this.worker.onmessage = ({ data }) => {
            if (data.failed) {
                this.failed = data.failed;
                console.warn(`Tide language server stopped: ${data.failed}`);
            } else if (data.diagnostics) this.diagnostics(data.diagnostics);
            else if (data.id !== undefined) {
                this.waiting.get(data.id)?.(data.result);
                this.waiting.delete(data.id);
            }
        };
        this.sent = model.getValue();
        this.worker.postMessage({ method: 'open', params: { text: this.sent } });
    }

    // Typing sends the new text after a pause; a request sends it first, so
    // its position is in the text the server has.
    changed() {
        clearTimeout(this.timer);
        this.timer = setTimeout(() => this.flush(), 200);
    }

    private flush() {
        clearTimeout(this.timer);
        const text = this.model.getValue();
        if (text === this.sent) return;
        this.sent = text;
        this.worker.postMessage({ method: 'textDocument/didChange', params: { text } });
    }

    request<T>(method: string, params: object): Promise<T | null> {
        this.flush();
        const id = this.next++;
        return new Promise(resolve => {
            this.waiting.set(id, result => resolve(result as T | null));
            this.worker.postMessage({ id, method, params });
        });
    }

    at<T>(method: string, position: monaco.Position, extra: object = {}) {
        return this.request<T>(method, { position: toPosition(position), ...extra });
    }

    private diagnostics(list: { range: Range; severity?: number; message: string; tags?: number[]; source?: string }[]) {
        if (this.model.getValue() !== this.sent) return; // Stale: newer text is on its way
        monaco.editor.setModelMarkers(this.model, 'tide', list.map(d => ({
            ...toRange(d.range), severity: SEVERITY[d.severity ?? 1], message: d.message, source: d.source, tags: d.tags,
        })));
        this.problems(list.map(d => ({
            line: d.range.start.line + 1, column: d.range.start.character + 1, message: d.message,
            severity: d.severity === 2 ? 'warning' : (d.severity ?? 1) === 1 ? 'error' : 'info',
        })));
    }

    dispose() {
        clearTimeout(this.timer);
        this.worker.terminate();
        for (const resolve of this.waiting.values()) resolve(null);
        this.waiting.clear();
    }
}

// Only the playground's one document is the server's; it can't open others.
let active: Language | undefined;
function server(model: monaco.editor.ITextModel) {
    return active && model.uri.toString() === DOCUMENT ? active : undefined;
}

function registerProviders() {
    monaco.languages.registerCompletionItemProvider('tide', {
        triggerCharacters: ['.'],
        async provideCompletionItems(model, position) {
            type Item = {
                label: string; kind?: number; detail?: string; documentation?: Markup; sortText?: string;
                filterText?: string; insertText?: string; insertTextFormat?: number; preselect?: boolean;
                textEdit?: { range?: Range; insert?: Range; replace?: Range; newText: string };
                additionalTextEdits?: { range: Range; newText: string }[];
            };
            const result = await server(model)?.at<Item[] | { items: Item[]; isIncomplete?: boolean }>('textDocument/completion', position);
            if (!result) return { suggestions: [] };
            const items = Array.isArray(result) ? result : result.items;
            const word = model.getWordUntilPosition(position);
            const fallback = { startLineNumber: position.lineNumber, endLineNumber: position.lineNumber, startColumn: word.startColumn, endColumn: word.endColumn };
            return {
                incomplete: !Array.isArray(result) && result.isIncomplete,
                suggestions: items.map(item => {
                    const edit = item.textEdit;
                    const range = edit?.range ?? edit?.replace;
                    return {
                        label: item.label,
                        kind: monaco.languages.CompletionItemKind[KINDS[(item.kind ?? 1) - 1] ?? 'Text'],
                        detail: item.detail,
                        documentation: documentation(item.documentation),
                        sortText: item.sortText,
                        filterText: item.filterText,
                        preselect: item.preselect,
                        insertText: edit?.newText ?? item.insertText ?? item.label,
                        insertTextRules: item.insertTextFormat === 2 ? monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet : undefined,
                        range: range ? toRange(range) : fallback,
                        additionalTextEdits: item.additionalTextEdits?.map(e => ({ range: toRange(e.range), text: e.newText })),
                    };
                }),
            };
        },
    });

    monaco.languages.registerHoverProvider('tide', {
        async provideHover(model, position) {
            const hover = await server(model)?.at<{ contents: Markup | Markup[]; range?: Range }>('textDocument/hover', position);
            if (!hover) return null;
            return { contents: toMarkdown(hover.contents), range: hover.range && toRange(hover.range) };
        },
    });

    monaco.languages.registerSignatureHelpProvider('tide', {
        signatureHelpTriggerCharacters: ['(', ','],
        signatureHelpRetriggerCharacters: [','],
        async provideSignatureHelp(model, position) {
            type Signature = { label: string; documentation?: Markup; parameters?: { label: string | [number, number]; documentation?: Markup }[]; activeParameter?: number };
            const help = await server(model)?.at<{ signatures: Signature[]; activeSignature?: number; activeParameter?: number }>('textDocument/signatureHelp', position);
            if (!help || !help.signatures.length) return null;
            return {
                value: {
                    signatures: help.signatures.map(s => ({
                        label: s.label, documentation: documentation(s.documentation), activeParameter: s.activeParameter,
                        parameters: (s.parameters ?? []).map(p => ({ label: p.label, documentation: documentation(p.documentation) })),
                    })),
                    activeSignature: help.activeSignature ?? 0,
                    activeParameter: help.activeParameter ?? 0,
                },
                dispose() {},
            };
        },
    });

    type Location = { uri?: string; range?: Range; targetUri?: string; targetSelectionRange?: Range };
    monaco.languages.registerDefinitionProvider('tide', {
        async provideDefinition(model, position) {
            const found = await server(model)?.at<Location | Location[]>('textDocument/definition', position);
            if (!found) return null;
            // Built-ins are defined in engine headers, which this page has no tab for.
            return (Array.isArray(found) ? found : [found])
                .map(l => ({ uri: l.targetUri ?? l.uri, range: l.targetSelectionRange ?? l.range }))
                .filter(l => l.uri === DOCUMENT && l.range)
                .map(l => ({ uri: model.uri, range: toRange(l.range!) }));
        },
    });

    monaco.languages.registerDocumentHighlightProvider('tide', {
        async provideDocumentHighlights(model, position) {
            const found = await server(model)?.at<{ range: Range; kind?: number }[]>('textDocument/documentHighlight', position);
            return found?.map(h => ({ range: toRange(h.range), kind: (h.kind ?? 1) - 1 })) ?? null;
        },
    });

    type WorkspaceEdit = { changes?: Record<string, { range: Range; newText: string }[]>; documentChanges?: { textDocument: { uri: string }; edits: { range: Range; newText: string }[] }[] };
    monaco.languages.registerRenameProvider('tide', {
        async resolveRenameLocation(model, position) {
            const found = await server(model)?.at<Range | { range: Range; placeholder: string }>('textDocument/prepareRename', position);
            if (!found) return { range: new monaco.Range(1, 1, 1, 1), text: '', rejectReason: 'This name cannot be renamed.' };
            if ('range' in found) return { range: toRange(found.range), text: found.placeholder };
            const range = toRange(found);
            return { range, text: model.getValueInRange(range) };
        },
        async provideRenameEdits(model, position, newName) {
            const edit = await server(model)?.at<WorkspaceEdit>('textDocument/rename', position, { newName });
            if (!edit) return null;
            const edits = [
                ...Object.entries(edit.changes ?? {}).map(([uri, list]) => ({ uri, list })),
                ...(edit.documentChanges ?? []).map(d => ({ uri: d.textDocument.uri, list: d.edits })),
            ].filter(e => e.uri === DOCUMENT).flatMap(e => e.list);
            return { edits: edits.map(e => ({ resource: model.uri, textEdit: { range: toRange(e.range), text: e.newText }, versionId: undefined })) };
        },
    });

    monaco.languages.registerDocumentFormattingEditProvider('tide', {
        async provideDocumentFormattingEdits(model, options) {
            const edits = await server(model)?.request<{ range: Range; newText: string }[]>('textDocument/formatting', {
                options: { tabSize: options.tabSize, insertSpaces: options.insertSpaces },
            });
            return edits?.map(e => ({ range: toRange(e.range), text: e.newText })) ?? null;
        },
    });

    monaco.languages.registerFoldingRangeProvider('tide', {
        async provideFoldingRanges(model) {
            const found = await server(model)?.request<{ startLine: number; endLine: number }[]>('textDocument/foldingRange', {});
            return found?.map(f => ({ start: f.startLine + 1, end: f.endLine + 1 })) ?? null;
        },
    });
}

export interface Editor {
    readonly value: string;
    setValue(text: string): void;
    focus(): void;
    hasFocus(): boolean;
    reveal(line: number, column: number): void;
    setDark(dark: boolean): void;
    layout(): void;
    dispose(): void;
}

let providers = false;
export async function createEditor(element: HTMLElement, options: {
    value: string; dark: boolean; changed: () => void; compile: () => void; problems: (problems: Problem[]) => void;
}): Promise<Editor> {
    await registerLanguage();
    if (!providers) {
        providers = true;
        registerProviders();
    }
    monaco.editor.getModel(monaco.Uri.parse(DOCUMENT))?.dispose();
    const model = monaco.editor.createModel(options.value, 'tide', monaco.Uri.parse(DOCUMENT));
    model.updateOptions({ tabSize: 4, insertSpaces: true });
    const editor = monaco.editor.create(element, {
        model,
        theme: options.dark ? DARK : LIGHT,
        automaticLayout: true,
        fontFamily: getComputedStyle(element).fontFamily,
        fontSize: 13,
        lineHeight: 21,
        minimap: { enabled: false },
        scrollBeyondLastLine: false,
        padding: { top: 14, bottom: 14 },
        wordBasedSuggestions: 'off',
        fixedOverflowWidgets: true,
        renderLineHighlight: 'line',
        bracketPairColorization: { enabled: false },
        guides: { indentation: true },
        tabSize: 4,
        ariaLabel: 'Tide source code',
    });
    const language = new Language(model, options.problems);
    active = language;
    model.onDidChangeContent(() => {
        language.changed();
        options.changed();
    });
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, options.compile);
    return {
        get value() { return model.getValue(); },
        setValue(text: string) {
            // An edit, so Undo brings the previous code back.
            editor.executeEdits('reset', [{ range: model.getFullModelRange(), text }]);
            editor.pushUndoStop();
            editor.setScrollPosition({ scrollTop: 0, scrollLeft: 0 });
        },
        focus: () => editor.focus(),
        hasFocus: () => editor.hasTextFocus(),
        reveal(line: number, column: number) {
            editor.setPosition({ lineNumber: line, column });
            editor.revealLineInCenterIfOutsideViewport(line);
            editor.focus();
        },
        setDark: (dark: boolean) => monaco.editor.setTheme(dark ? DARK : LIGHT),
        layout: () => editor.layout(),
        dispose() {
            if (active === language) active = undefined;
            language.dispose();
            editor.dispose();
            model.dispose();
        },
    };
}
