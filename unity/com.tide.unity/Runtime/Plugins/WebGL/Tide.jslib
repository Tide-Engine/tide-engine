// Unity's side of Tide on the web (TideLibrary): Tide's own program runs beside
// Unity's in the page (tide-game.js, which Tide's build processor puts in it:
// tide.js with the game's WebAssembly and pack), with its own memory and
// threads. Each Unity frame copies Unity's input into it, runs its frame and
// copies its view packet out, into a buffer of Unity's heap. While the page is
// hidden Unity stops, and tide.js's worker keeps the match's frames going.
var TideWebLibrary = {
    $TideWeb: {
        started: false,
        out: {},
        program: function () {
            var tide = window.Tide;
            var x = tide && tide.exports ? tide.exports() : null;
            return x && x.tide_unity_ready && x.tide_unity_ready() ? x : null;
        },
        memory: function () {
            return new Uint8Array(window.Tide.memory().buffer);
        },
        // Copies `size` bytes at `at` in Tide's memory into a buffer of Unity's heap kept for `which`.
        copyOut: function (which, at, size) {
            var held = TideWeb.out[which] || (TideWeb.out[which] = { ptr: 0, capacity: 0 });
            if (held.capacity < size) {
                if (held.ptr) _free(held.ptr);
                held.capacity = Math.max(size, held.capacity * 2, 256);
                held.ptr = _malloc(held.capacity);
            }
            HEAPU8.set(TideWeb.memory().subarray(at, at + size), held.ptr);
            return held.ptr;
        },
        // Copies `size` bytes of Unity's heap at `from` into Tide's scratch buffer, and says where they are there.
        copyIn: function (x, from, size) {
            var at = x.tide_unity_buffer(size);
            if (!at) return 0;
            TideWeb.memory().set(HEAPU8.subarray(from, from + size), at);
            return at;
        },
    },

    // TideEmbedDesc on wasm32: game, pack, packSize, tickRate, argc, argv, gameName, relay, libraryDir
    TideWebCreate__deps: ['$TideWeb'],
    TideWebCreate: function (desc) {
        var tide = window.Tide;
        if (!tide || !tide.start || !tide.exports) {
            console.error('tide: the page has no Tide program: Tide\'s build processor puts tide-game.js in the page as the player builds.');
            return 0;
        }
        var argc = HEAP32[(desc >> 2) + 4], argv = HEAP32[(desc >> 2) + 5], relay = HEAP32[(desc >> 2) + 7];
        var args = [];
        for (var i = 1; i < argc; i++) args.push(UTF8ToString(HEAP32[(argv >> 2) + i]));
        tide.arguments = args;
        if (relay) tide.relay = UTF8ToString(relay);
        if (!TideWeb.started) {
            TideWeb.started = true;
            tide.start();
        }
        return 1;
    },

    TideWebDestroy__deps: ['$TideWeb'],
    TideWebDestroy: function () {
        // Tide's program lives as long as the page, as Unity's does
    },

    TideWebFrame__deps: ['$TideWeb'],
    TideWebFrame: function (seconds, input, size, width, height, scale) {
        var x = TideWeb.program();
        if (!x) return 0;
        TideWeb.memory().set(HEAPU8.subarray(input, input + size), x.tide_unity_input());
        x.tide_unity_frame_info(seconds, width, height, scale);
        window.Tide.frame();
        return 0;
    },

    TideWebStatus__deps: ['$TideWeb'],
    TideWebStatus: function (status, size) {
        var x = TideWeb.program();
        if (!x) {
            HEAPU8.fill(0, status, status + size);
            return;
        }
        var at = x.tide_unity_status();
        HEAPU8.set(TideWeb.memory().subarray(at, at + size), status);
    },

    TideWebView__deps: ['$TideWeb'],
    TideWebView: function (size) {
        var x = TideWeb.program();
        var at = x ? x.tide_unity_view() : 0, n = x ? x.tide_unity_view_size() : 0;
        HEAPU32[size >> 2] = at ? n : 0;
        return at && n ? TideWeb.copyOut('view', at, n) : 0;
    },

    TideWebSchema__deps: ['$TideWeb'],
    TideWebSchema: function (size) {
        var x = TideWeb.program();
        var at = x ? x.tide_unity_schema() : 0, n = x ? x.tide_unity_schema_size() : 0;
        HEAPU32[size >> 2] = at ? n : 0;
        return at && n ? TideWeb.copyOut('schema', at, n) : 0;
    },

    TideWebDraw__deps: ['$TideWeb'],
    TideWebDraw: function (size) {
        var x = TideWeb.program();
        var at = x ? x.tide_unity_draw() : 0, n = x ? x.tide_unity_draw_size() : 0;
        HEAPU32[size >> 2] = at ? n : 0;
        return at && n ? TideWeb.copyOut('draw', at, n) : 0;
    },

    TideWebRender__deps: ['$TideWeb'],
    TideWebRender: function (size) {
        var x = TideWeb.program();
        var at = x ? x.tide_unity_render() : 0, n = x ? x.tide_unity_render_size() : 0;
        HEAPU32[size >> 2] = at ? n : 0;
        return at && n ? TideWeb.copyOut('render', at, n) : 0;
    },

    TideWebRenderReset__deps: ['$TideWeb'],
    TideWebRenderReset: function () {
        var x = TideWeb.program();
        if (x) x.tide_unity_render_reset();
    },

    TideWebRequest__deps: ['$TideWeb'],
    TideWebRequest: function (kind, text, port) {
        var x = TideWeb.program();
        if (!x) return 0;
        var at = 0;
        if (text) {
            var n = 0;
            while (HEAPU8[text + n]) n++;
            at = TideWeb.copyIn(x, text, n + 1);
            if (!at) return 0;
        }
        return x.tide_unity_request(kind, at, port);
    },

    TideWebBackground__deps: ['$TideWeb'],
    TideWebBackground: function (hidden) {
        var x = TideWeb.program();
        if (x) x.tide_unity_background(hidden);
    },

    TideWebCamera__deps: ['$TideWeb'],
    TideWebCamera: function (matrices, given) {
        var program = TideWeb.program();
        if (!program) return;
        if (given) TideWeb.memory().set(HEAPU8.subarray(matrices, matrices + 128), program.tide_unity_camera_matrices());
        program.tide_unity_camera(given);
    },

    TideWebCommand__deps: ['$TideWeb'],
    TideWebCommand: function (type, index, generation, value, size) {
        var x = TideWeb.program();
        if (!x) return 0;
        var at = size ? TideWeb.copyIn(x, value, size) : 0;
        if (size && !at) return 0;
        return x.tide_unity_command(type, index, generation, at, size);
    },
};

autoAddDeps(TideWebLibrary, '$TideWeb');
mergeInto(LibraryManager.library, TideWebLibrary);
