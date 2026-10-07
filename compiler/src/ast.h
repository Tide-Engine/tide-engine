#pragma once

#include "common.h"
#include "lexer.h"

// Components and archetypes a game can have. Components are bits of a mask,
// which spawn sites, filters and archetypes are made of.
#define MAX_COMPONENTS 256
#define MAX_ARCHETYPES 1024
#define COMP_WORDS (MAX_COMPONENTS / 64)

typedef struct comp_mask {
    uint64_t w[COMP_WORDS];
} comp_mask;

static inline comp_mask mask_none(void)
{
    return (comp_mask){{0}};
}

static inline comp_mask mask_bit(const int i)
{
    comp_mask m = {{0}};
    m.w[i / 64] = (uint64_t)1 << (i % 64);
    return m;
}

static inline bool mask_has(const comp_mask m, const int i)
{
    return (m.w[i / 64] >> (i % 64)) & 1u;
}

static inline comp_mask mask_or(const comp_mask a, const comp_mask b)
{
    comp_mask m;
    for (int i = 0; i < COMP_WORDS; i++) m.w[i] = a.w[i] | b.w[i];
    return m;
}

static inline comp_mask mask_and(const comp_mask a, const comp_mask b)
{
    comp_mask m;
    for (int i = 0; i < COMP_WORDS; i++) m.w[i] = a.w[i] & b.w[i];
    return m;
}

// a without b's bits
static inline comp_mask mask_andnot(const comp_mask a, const comp_mask b)
{
    comp_mask m;
    for (int i = 0; i < COMP_WORDS; i++) m.w[i] = a.w[i] & ~b.w[i];
    return m;
}

static inline bool mask_eq(const comp_mask a, const comp_mask b)
{
    for (int i = 0; i < COMP_WORDS; i++) {
        if (a.w[i] != b.w[i]) return false;
    }
    return true;
}

static inline bool mask_any(const comp_mask a)
{
    for (int i = 0; i < COMP_WORDS; i++) {
        if (a.w[i]) return true;
    }
    return false;
}

// Whether every bit of a is in b.
static inline bool mask_subset(const comp_mask a, const comp_mask b)
{
    for (int i = 0; i < COMP_WORDS; i++) {
        if (a.w[i] & ~b.w[i]) return false;
    }
    return true;
}

static inline bool mask_intersects(const comp_mask a, const comp_mask b)
{
    for (int i = 0; i < COMP_WORDS; i++) {
        if (a.w[i] & b.w[i]) return true;
    }
    return false;
}

typedef struct decl decl;
typedef struct expr expr;
typedef struct stmt stmt;

// ---------------------------------------------------------------------------
// Types

typedef enum type_kind {
    TY_ERROR, // Already reported; suppresses follow-up errors.
    TY_VOID,
    TY_BOOL,
    TY_BYTE,      // byte: an unsigned 8-bit integer; arithmetic promotes to int
    TY_INT,
    TY_LONG,       // long: a 64-bit int. An int converts to it implicitly; floats convert both ways only explicitly
    TY_INT2,
    TY_INT3,
    TY_INT4,
    TY_FLOAT,
    TY_FLOAT2,
    TY_FLOAT3,
    TY_FLOAT4,
    TY_QUATERNION,
    TY_FLOAT2X2,
    TY_FLOAT3X3,
    TY_FLOAT4X4,
    TY_ENTITY,
    TY_LOCAL_ENTITY, // LocalEntity: an entity of the local world, which only local code can hold
    TY_PLAYER,     // PlayerID
    TY_COLOR,
    TY_STRING,     // Text: literals, and function parameters and locals that hold them
    TY_RECT,       // Rect: a GUI rectangle, x and y from the top left, width and height
    TY_ACTION,      // Action: a function's last parameter, the code its caller writes in braces after the call
    TY_DRAW_LIST,  // DrawList: the frame's draw list, `Draw.list`, which only an extern function takes
    TY_TEXTURE,    // Texture: a local reference to pixels or a render target's color image
    TY_DEPTH_TEXTURE, // DepthTexture: a local reference to a render target's depth, which a frag or vert reads
    TY_CUBEMAP,    // Cubemap: a local reference to six images a frag or vert reads by a direction: an asset's, or one drawn into
    TY_RENDER_TARGET, // RenderTarget: a local drawing destination; Draw.window names the window
    TY_CLIP,       // Clip: a sound of the game's pack, this machine's to play (an asset's, or a copy of one)
    TY_LIST,       // List<T>: decl is the list type, whose one field is its element
    TY_MAP,        // Map<K, V>: decl is the map type, whose fields are its key and its value
    TY_SET,        // Set<K>: decl is the set type, whose one field is its key
    TY_GRID,       // Grid2<T> and Grid3<T>: decl is the grid type, whose one field is its cell
    TY_ARRAY,      // T[N]: decl is the array type (DECL_ARRAY), whose one field is its element and array_length N
    TY_WORLD,      // World: a system's, view's or handler's parameter, which reads (or with mut, changes) any entity's components by handle
    TY_QUERY,      // A system's read-only second entity set; decl is its compiler-only row struct
    TY_COMPONENT,
    TY_SINGLETON,
    TY_INPUT,      // The game's input declaration
    TY_RECORD,     // Built-in read-only data: Devices, Keyboard, Button, ...
    TY_STRUCT,     // A struct: plain data, copied like any value
    TY_EVENT,      // An event's value: what `Send` sends and a handler receives
    TY_ENUM,       // A value of an enum: one of its members, an int underneath
    TY_OPTIONAL,   // T?: a value or nothing. decl is a DECL_RESULT whose field 0 is the value
    TY_FAILABLE,   // What a function that `fails` gives: its value (field 0, maybe void) or its error (field 1)
} type_kind;

typedef struct type {
    type_kind kind;
    decl *decl; // For components, singletons, inputs, records, structs and events.
} type;

// ---------------------------------------------------------------------------
// Files and names

// One source file of a program, or one `namespace Name { ... }` block of a
// file: where declarations get their namespace, and what their code reads
// names through. A block's unit has its file's source and its `using`s, and
// the namespace its name makes inside the one it's written in. Both are in
// program.units, a file's before its blocks', each block where it opens.
typedef struct unit {
    const source *src;
    str ns;           // `namespace Game.Combat;`, or empty for the global namespace
    loc ns_at;
    VEC(str) usings;  // `using Physics;`: the file's, which its blocks share
    VEC(loc) using_at;
    const struct unit *outer; // A block's: the file or the block it's written in. NULL for a file
    str ns_written;   // A block's name as written, at ns_at: `Combat` in a file of `namespace Game;`, where ns is Game.Combat
    loc block_at;     // A block's `namespace` word
    loc open_at;      // ...its '{'
    loc close_at;     // ...and its '}'; no line where it's missing
    // The column its declarations start at, which is how a parser that
    // recovers and the editors tell where one starts: 1 in a file, and in a
    // block that of its first line after the '{', or four past its
    // `namespace` word, as the formatter puts them.
    int decl_col;
} unit;

// A type as it's written: a name, maybe qualified, what it takes in <>, and
// `?` after it. The parser builds one for every place a type is written (a
// field's, a parameter's, a local's, a constant's, an asset's, what a function
// returns or fails with, the types in <>, what's after `is`), and the checker
// resolves it into a `type` (resolve_type in check.c). `text` is the type as
// messages and editors write it: "List<Grid2<Color>>", "Map<K, V>" with that
// comma and space, "int?".
typedef struct type_ref {
    str name;    // "List", "Combat.Health", "int"
    loc at;      // Where it starts: its namespace if it's qualified
    loc name_at; // Its last part: the name itself
    VEC(struct type_ref *) args; // What's in its <>, in order; none for a plain name
    VEC(expr *) sizes;           // T[N][M]: what's in each [] after it, the first the outer; constant expressions
    bool optional;   // `T?`
    loc question_at; // ...the '?'
    str text;        // As written, without spaces but the one after a map's comma
    loc end;         // Just past its last token
} type_ref;

// One parameter of a declaration that's named with its parameters, as C#'s
// `cref` names an overload: `Position` or `with Player` in
// [After(Move(Position, with Player))]. Its type as written, with neither a
// name nor `mut`.
typedef struct sig_item {
    int mode;      // PARAM_READ, PARAM_WITH, PARAM_WITHOUT or PARAM_QUERY
    bool verified; // `verified Health`, `with verified Dead`: a view's, from the world the server has
    str type_name; // "ownership" for `with ownership`; empty for a query
    const type_ref *type_ref; // ...as written; NULL for `ownership` and a query
    loc at;        // Where it starts
    loc type_at;   // The type's name (the last part if it's qualified)
    loc type_qual_at;
    VEC(struct sig_item) query; // PARAM_QUERY: its own parameters
    const struct param *param;  // The parameter it names, once checked
} sig_item;

typedef VEC(sig_item) sig_items;

// A name that may be qualified by a namespace: Health, Combat.Health.
typedef struct qname {
    str text;    // Without spaces: "Combat.Health"
    loc at;      // The first part
    loc name_at; // The last part: the name itself
    struct decl *decl; // What it names, once checked
    const struct enum_member *enum_member; // Ordering attributes: a phase in this enum
    bool has_signature;  // Written with its parameters, `Move(Position, with Player)`: which of the declarations that share the name
    sig_items signature;
} qname;

// [Before(Physics.Integrate)]
typedef struct attribute {
    str name;
    loc at;
    VEC(qname) args;     // On declarations, names: [After(Physics.Gravity)]
    VEC(expr *) values;  // Constant values: field bounds, NativeName's text, or Stage's optional offset
} attribute;

// ---------------------------------------------------------------------------
// Declarations

typedef struct field {
    str name;
    str type_name;
    loc at;      // The name
    type type;
    expr *default_value; // Constant expression, or NULL for zero.
    loc type_at; // The type's name (the last part if it's qualified)
    VEC(attribute) attributes; // Bounds, [Snap], [Absolute], and view correction roles
    int view_correction; // 0: ordinary interpolation, 1: [Position], 2: [Rotation]
    loc type_qual_at; // Where the type starts: its namespace if it's qualified
    bool hidden; // The engine's own, in generated C only: a scene's visibility and players
    int leaf;    // In the device records: 1 + where its values start among its record's (a value is one)
    bool window; // ...a position in this machine's window, which the match can't read
    bool typed;  // ...what the keyboard typed, as text: a frame's, which only views read, and no device value
    bool is_local; // `local List<Vertex3> verts;` in a match component or singleton: this machine's, outside the match
    loc local_at;  // ...the `local` keyword
    const type_ref *type_ref; // The type as written; NULL for the engine's own fields and a container's
} field;

typedef enum param_mode {
    PARAM_READ,
    PARAM_MUT,
    PARAM_WITH,
    PARAM_WITHOUT,
    PARAM_EVENT, // An event handler's trigger: `event(Hit hit)`, always its first parameter
    PARAM_SENDER, // A command handler's authenticated PlayerID, following its payload
    PARAM_IN,    // `in Stats stats`, an extern function's: C gets a read-only pointer to the value
    PARAM_QUERY, // `query (Body body, with Fighter) fighters`: a second candidate set
} param_mode;

// One value the devices send, like keyboard.space or gamepad.leftStick.
typedef struct device_leaf {
    const char *path;          // Its C access from tide_devices: "keyboard.space"
    const struct field *field; // Its field in the device records
} device_leaf;

#define DEVICE_WORDS 4 // Bits for up to 256 device leaves

typedef struct param {
    param_mode mode;
    str type_name;
    const type_ref *type_ref; // The type as written; NULL for `ownership` and the parameters the checker makes
    str name; // Empty for with/without, and for a trigger without a name: `event(Spawned)`.
    loc at;   // The first token, a modifier or the type
    type type;
    loc type_at;      // The type's name (the last part if it's qualified)
    loc type_qual_at; // Where the type starts: its namespace if it's qualified
    loc name_at;
    bool read;    // The body names it
    bool written; // The body assigns through it
    bool ownership; // `with ownership`, `without ownership`: by whether this machine's player owns the entity; no type, `type_at` is the word
    // `verified Health health`, `with verified Dead`, `event(verified Died died)`: from the world the server has
    // confirmed, in a view; in a local handler's trigger, once the server confirmed the tick that sent the event
    bool verified;
    loc verified_at; // ...the word
    bool function_param; // A method's or function's: a copy, or with mut, the caller's variable itself
    // `out Health health`, a function's or method's, as C#'s: the call makes the caller's variable, which starts
    // unassigned in the code and is assigned on every path that returns. Its mode is PARAM_MUT, as for everything
    // else it's a variable the call changes, by address. An extern function's `out List<byte> image` is a list C
    // makes the elements of, through a tide_list_out (tide/list.h). A frag's `out Color color` is its pixel's
    // color, and a vert's `out FromLamp lamp` a struct it hands the frag it's drawn with, which reads it blended
    // across the triangle: the same rules for the code, with the GPU in the caller's place
    bool is_out;
    loc out_at; // ...the word
    int output; // A frag's `out Color`: which of its outputs it is, from 0, in the order they're written
    struct expr *default_value; // ...and what a call that leaves it out passes, `float multiplier = 1`: a constant expression, or NULL
    str default_text;           // ...as its declaration writes it, for messages and editors: "1"
    str mode_word;       // `mut` or `out` as a system, view, handler, frag or vert writes it, for what editors offer in its place
    bool *reads;         // A frag's or vert's: for each field of its type, whether the code reads it
    bool *local_reads;   // ...and for each of its type's local fields (its local_part's)
    bool whole;          // ...or reads it as a value, all its fields at once
    bool task_ref;       // An async function's component or singleton: its task gets it again each time it goes on
    // A match component or singleton with local fields (its type's local_part):
    bool local_use;      // The code reads or writes its local fields, itself or through a function it passes it to
    loc local_use_at;    // ...where it first does
    bool local_written;  // ...and writes one
    bool match_write;    // A helper's: it changes the match's fields of it, itself or through another helper
    loc match_write_at;  // ...where it first does
    VEC(struct param) query; // PARAM_QUERY's read-only bindings and component filters
    comp_mask query_need_mask;
    comp_mask query_without_mask;
    bool query_local; // The candidates belong to the local world rather than the match
    const struct decl *query_of; // Its first component, which says whose its entities are; NULL with none
    // A view's query with `verified` bindings or filters, as a view's own
    // (decl.verified_need): what its entities need and lack in the world the
    // server confirmed, and whether it goes through that world's entities (all
    // of it is `verified`) or the drawn world's that are in both
    comp_mask query_verified_need;
    comp_mask query_verified_without;
    bool query_verified_entities;
    bool query_verified_join;
    // A view's or a local handler's over the match: its entries are made before
    // the code runs, as it sees the match (blended, or as its event's tick left it)
    bool query_rows;
    struct param *query_source; // A nested binding or filter's containing query parameter
} param;

// Why a system waits for one that runs before it in the tick (see parallel.c).
typedef enum conflict_kind {
    CONFLICT_BOTH_WRITE,     // Both write the data
    CONFLICT_EARLIER_READS,  // The earlier system reads what this one writes
    CONFLICT_EARLIER_WRITES, // The earlier system writes what this one reads
    CONFLICT_TEXT,           // Both write text or lists, which the match keeps in one heap
    CONFLICT_SPAWN,          // Both spawn: entities get their IDs in order
    CONFLICT_TASKS,          // Both start tasks: the match keeps them in the order they start
} conflict_kind;

typedef struct conflict {
    struct decl *data; // A component or singleton
    conflict_kind kind;
} conflict;

// A view's filter by whether this machine's player owns the entity (decl.ownership).
// `with ownership`: it has an Owner, whose player is Session.player. `without
// ownership`: every other entity, with an Owner or without.
enum {
    OWNERSHIP_NONE,
    OWNERSHIP_WITH,
    OWNERSHIP_WITHOUT,
};

// An async function's world (decl.task_side): what it does decides where it can run.
enum {
    TASK_EITHER, // Neither changes the match nor uses local state: it runs in whichever world starts it
    TASK_MATCH,
    TASK_LOCAL,
};

// A component code reaches through its World parameter or a query, beyond
// the entity the system runs for. It's access to every entity's, which the
// schedule counts as the system's own.
typedef struct world_use {
    struct decl *component;
    bool writes;  // Some call is a Set
    loc at;       // The first call that names it
    loc write_at; // ...and the first Set
    bool query_read; // A query parameter also reads this component's candidate rows
    bool through_world; // Some World call names it
} world_use;

typedef struct system_wait {
    struct decl *on;         // A system earlier in the tick
    VEC(conflict) conflicts; // The data they share; empty if only an attribute orders them
    bool ordered;            // [After] or [Before] orders them
    struct decl *through;    // Another system it waits for that already waits for `on`, or NULL
} system_wait;

typedef enum decl_kind {
    DECL_COMPONENT,
    DECL_SINGLETON,
    DECL_SYSTEM,
    DECL_INPUT,  // input PlayerInput { fields; PlayerInput(Devices devices) { ... } }
    DECL_RECORD, // Built-in device data; not in program.decls
    DECL_STRUCT, // struct Stats { fields }: a value type for fields and locals
    DECL_METHOD, // bool IsDead() { ... } in a struct or component; in its `methods`, not program.decls
    DECL_FUNCTION, // void Heal(mut Stats stats, float amount) { ... }: code other code calls; `extern` ones are C's
    DECL_EVENT,    // event Hit { fields }: something that happened, sent with Send
    DECL_ENUM,     // enum Page { Title, Options }: a type with named values
    DECL_LIST,     // List<T>, one per element type: its one field is the element; in program.lists
    DECL_MAP,      // Map<K, V> or Set<K>, one per key and value type: fields key and (maps) value; in program.maps
    DECL_GRID,     // Grid2<T> or Grid3<T>, one per cell type and dimensions: its one field is the cell; in program.grids
    DECL_ARRAY,    // T[N], one per element type and length: its one field is the element, array_length N; in program.arrays
    DECL_RESULT,   // T? or `T fails E`, one per combination: field 0 is the value, field 1 the error; in program.results
    DECL_CONST,    // const int MAX_HEALTH = 100;: a value code reads by name, the same on every machine
    DECL_SETTINGS, // settings { tickRate = 30; }: the engine's settings, as fields; in program.settings
    DECL_SHADER,   // frag Shade(...) { ... } and vert Wave(...) { ... }: code the GPU runs for a mesh's pixels or vertices
    DECL_ASSET,    // asset Grid2<Color> hero = "art/hero.png";: a file's contents under a name, read-only, packed with the game
} decl_kind;

// How a frag or vert reads a texture: a color, or a target's depth, as a
// number (depth.Sample) or compared with one of its own (depth.Compare). The
// values are tide/draw.h's tide_shader_read's.
// READ_CUBE is a Cubemap's color, read by a direction.
typedef enum shader_read { READ_COLOR, READ_DEPTH, READ_COMPARE, READ_CUBE } shader_read;

// A grid a frag or vert samples: a field of one of its parameters, and how.
typedef struct shader_sample {
    int param;    // -1 for an asset's grid
    int field;    // -1 for a direct Texture parameter; otherwise its component or singleton field
    bool local;   // The field is one of its type's local fields (its local_part's)
    int filter;   // Its Filter's number, tide_filter's: the nearest cell, blended, or between its smaller images too
    const struct decl *asset; // A Grid2<Color> or Texture asset sampled by name, or NULL
    int wrap;     // What a uv past 0 and 1 reads: its Wrap's number, tide_wrap's
    shader_read read; // A DepthTexture's: which of its two ways it's read
    // Whether how it's read is known here: the Sample says its filter and its wrap, or it's of a grid field
    // or an asset by name, whose declaration's it then has, worked into `filter` and `wrap`
    // (declared_sampling). A Texture's that isn't (a frag's own Texture parameter, a Texture field) is read as
    // the Texture it's given says, which each draw's run carries (tide_draw_run.sampling)
    bool filter_said, wrap_said;
} shader_sample;

// One of an enum's members: `Options`, or `Options = 3`.
typedef struct enum_member {
    str name;
    loc at;
    struct expr *value; // The value written after '=', or NULL for the one after the previous member's
    int64_t number;     // Its value, once checked
} enum_member;

// A channel commands are in: an enum's member, named by [Channel]
typedef struct command_channel {
    struct decl *enumeration;
    const enum_member *member;
} command_channel;

typedef struct decl {
    decl_kind kind;
    str name;
    str qualified; // With its namespace, for messages: "Combat.Health"
    loc at;
    loc end; // The closing brace
    const unit *unit; // The file it's in; NULL for built-ins
    VEC(attribute) attributes;
    bool builtin;
    bool query_row;   // Compiler-only foreach row: entity and named read-only component values
    struct param *query_source; // Its query parameter, for editor binding lookup
    bool is_local;    // `local`: belongs to this machine, not the match. Views are always local code.
    loc local_at;     // The `local` keyword
    bool is_scene;    // `scene Arena { ... }`: a DECL_COMPONENT whose entity is a loaded scene
    bool is_sparse;   // [Sparse], or a tag any Add or Remove names: a presence byte and its lanes in every archetype it can reach, not an archetype of its own
    bool named;       // The built-in Random, Depth and Cull: some code names it, so generated C has it
    // A match component's or singleton's `local` fields are a declaration of
    // their own, its local part: a local one of the same kind and name, which
    // isn't in program.decls. A component's is kept for the match's entities
    // in a table of the local world (tide/attached.h), a row for each entity
    // something was written for; a singleton's is a local singleton.
    struct decl *local_part; // Its local fields, or NULL when it has none
    struct decl *part_of;    // A local part: the component or singleton it's of
    bool is_extern;   // `extern float Noise(float x);`: a DECL_FUNCTION written in C, with no body
    // `virtual` before a declaration with a body (a system, view, handler,
    // function, method, frag or vert) says another package can replace it, and
    // `override` before one replaces it, as in C#. An override is the
    // declaration of the same name and parameters (their types, with a
    // system's filters: what tells overloads apart), and takes its place while
    // compiling: in program.decls and the lists made of it (or its type's
    // `methods`), with its name, its namespace and its attributes, so the
    // schedule, every reference and generated C have the override where the
    // other was. What it replaced is in program.replaced: still checked for
    // errors, and nothing else.
    bool is_virtual;
    loc virtual_at;    // The `virtual` keyword
    bool is_override;
    loc override_at;   // The `override` keyword
    str override_name; // An override's name as written, which can be qualified: "Combat.Move", "Combat.Stats.Hurt"
    loc override_qual_at;     // ...where it starts; `at` is its last part
    struct decl *replaces;    // An override's: the declaration it took the place of
    struct decl *replaced_by; // What an override replaced: the override
    bool placed;              // An override that's in the place of what it replaces
    // The file whose namespace a declaration's name is in, and where its
    // attributes are written: its own (NULL), but for an override in place,
    // the file of what it replaced. Its code still reads names as its own
    // file does (`unit`).
    const struct unit *named_in;
    bool params_checked;      // A system, view or handler whose parameters were checked ahead of the others: a virtual one, or an override
    // `extern system Move(mut Position position, Time time);`: a DECL_SYSTEM written in C, with no body. C is
    // called for each run of rows the system's filters admit, with their components as arrays (tide/rows.h) and
    // the singletons it takes; to the schedule it's the system its signature says
    bool extern_system;
    const char *c_name; // Records: the C struct name. Extern functions and systems: the C function, from [NativeName] or the name.
    const char *gen_name; // Its name in generated C (Combat_Health for Combat.Health), which codegen gives every declaration once, before it writes any
    // Overloads, as in C#: functions that share a name, a type's methods that
    // do, and systems, views, handlers, frags and verts that do. Their
    // parameters tell them apart: their types in order, with a system's
    // filters, whatever their names, `mut` and defaults.
    bool shares_name;     // Another declaration of its kind has its name, where it's declared
    const char *c_suffix; // ...so its name in generated C goes on with its parameters' types: "_float_Weapon"
    const char *signature; // ...and messages and tools write it with them, as code names it: "Move(Position, with Player)"
    bool device_group;  // Devices, Keyboard, Mouse, Gamepad, Dpad, Touch, ...: records made of device values
    int leaves_count;   // ...how many
    VEC(int) instances; // ...where each place it has in the devices starts in program.device_leaves
    struct decl *array_of; // Records that are arrays of another, like Touchscreen.touches: the element
    int array_length;      // ...and how many

    // Components, singletons, inputs, records, structs and events
    VEC(field) fields;
    int index; // Component bit / singleton index / event index / system order / list, map or grid number. Constants: 1 while checking, 2 once checked.
    int dims;  // Grids: 2 or 3
    bool resized;         // Lists: some code calls Resize, so generated C has its helper
    bool paths;           // Lists and maps: code reaches into an element's fields (`rows[i].x`), so generated C has the helpers that point at one
    bool is_set;          // Maps: a Set<K>, with no values
    struct decl *entry;   // Maps: the struct a foreach over the map gives, `Entry<K, V>`: fields key and value
    struct decl *optional; // Maps: the V? that Get gives, once something calls it
    bool tries;           // Maps: some code calls TryGet, so generated C has its helper
    bool tried;           // Components: some code calls world.TryGet of it, so generated C has its helper

    // Events
    bool world_event;        // Built-in events the engine sends to the world, never to an entity
    bool is_command;        // `command`: a local submission whose one match handler receives its sender
    const struct decl *needs_target; // A handler that takes data from the entity the event is sent to, or NULL
    VEC(struct decl *) handlers;     // Its handlers, in the order they run
    VEC(struct decl *) listeners;    // A match event's local handlers (decl.listens), in the order they run

    // Structs and components: their methods
    VEC(struct decl *) methods;

    // Enums
    VEC(enum_member) members;
    str backing;    // `enum Voxel : byte`: the type it's stored as, or empty for int
    loc backing_at;
    int width;      // ...in bytes: 1 (byte), 2 (ushort) or 4 (int), once checked

    // Methods and functions
    struct decl *owner;       // A method's struct or component; NULL for a function
    bool is_mut_method;       // `mut void Damage(...)`: it may change the fields
    bool is_operator;         // `Money operator +(Money a, Money b)`: in a struct, with no value of its own
    bool is_interpolate;      // `Angle Interpolate(Angle from, Angle to, float t)`: how views blend its type, no value of its own
    bool uses_this;           // A component's method that reads `this`, itself or through its other methods: it's given the entity
    loc this_at;              // ...where it first does
    struct decl *interpolate; // Structs and components: their Interpolate, if they have one
    bool snapped;             // Singletons: something calls .Snap() on it, so it counts its snaps
    bool shown;               // Values with fields, and lists: text shows one somewhere, so it has a text helper
    tok_kind op;              // An operator's: T_PLUS, T_EQ, ...; T_MINUS is negation with one parameter
    str return_type_name;     // "void" if it returns nothing. A constant's type.
    const type_ref *return_type_ref; // ...as written; NULL for what has none (a system)
    loc return_type_at;       // Its last part, if it's qualified
    loc return_type_qual_at;
    type return_type;
    str fails_name;           // `int Parse(string text) fails ParseError`: the error type, or empty
    const type_ref *fails_ref; // ...as written, or NULL
    loc fails_at;             // The `fails` keyword
    loc fails_type_at;        // The error type's last part
    loc fails_type_qual_at;
    type fails;               // The error type; TY_VOID if it can't fail
    type result;              // What a call gives: return_type, or `return_type fails E` (TY_FAILABLE)

    // Constants, and assets (whose value is their path, a constant text)
    struct expr *value; // A constant expression of its type (return_type)
    // Assets: the files the path resolved to, once checked (asset_files.h); NULL where it couldn't
    struct asset_value *asset;
    int asset_kind;  // The asset_kind its type says, once checked (asset_files.h)
    // How its [Filter] and [Wrap] attributes say a texture of it is sampled, where a draw or a Sample that
    // names it doesn't say: 1 + the enum member's number, or 0 for none (declared_sampling)
    int sample_filter, sample_wrap;
    // [Raw] before it: its pixels are numbers as the file has them, with no alpha worked into them, which
    // only a frag or a vert reads, by its name (declared_raw)
    bool sample_raw;
    bool asset_many; // A List or Map of a glob's matches

    // Systems, views, methods, functions, and an input's Sample
    VEC(param) params;
    stmt *body;
    loc body_at;
    stmt *sanitize; // An input's Sanitize() { ... }
    loc sanitize_at;
    bool is_view;        // A view: a DECL_SYSTEM that runs once per frame, reads the world and draws.
    bool is_handler;     // An event handler: a DECL_SYSTEM that runs when its event is sent, `event(Hit hit) Name(...)`.
    struct decl *event;  // A handler's event
    struct decl *forgets; // A local handler of Forgotten: the match component or singleton whose local fields it hears go
    // A local handler of one of the match's events, `local event(Hit hit) ShowHit(...)`: it runs at the start of a
    // frame, for what the ticks since the last one sent, with the entity's components as the event's turn left them.
    // It's among its event's `listeners`, not its `handlers`, which the tick runs.
    bool listens;
    bool listens_verified; // ...once the server confirmed the tick: `local event(verified Hit hit)`
    bool per_entity;     // Runs once per matching entity, not once per tick.
    bool entity_local;   // Its entities are the local world's (views of local components)
    bool draws;          // A function that draws or uses the GUI, itself or through the functions it calls
    loc draws_at;        // ...where it first does
    bool plays;          // Plays sounds (Audio), itself or through the functions it calls: local code's
    loc plays_at;
    bool render_resources; // Uses local rendering handles, directly or through its calls
    loc render_resources_at;
    bool uses_io;         // Filesystem access, directly or through helpers: this machine's local state
    loc io_at;
    bool io_network;      // ...and what it first reaches is the network (Web.Fetch), for the message
    bool uses_fetch;      // Awaits Web.Fetch itself: its task's frame keeps the fetch's ticket
    bool frame_time;      // Reads Frame.dt, directly or through helpers: this machine's local state has it
    loc frame_time_at;
    bool frame_devices;  // ...and whether that's reading this frame's Devices, not drawing
    bool frame_cursor;   // ...or using its cursor: Cursor.Lock and Cursor.locked
    bool takes_action;   // A function whose last parameter is an Action: inlined where it's called
    bool calls_c;        // Code that calls an extern function, itself or through others: its calls run in order
    bool writes_text;    // A system that writes text, lists or grids into its world: its heap, which one system changes at a time
    bool spawns;         // A system that spawns or loads scenes: entity IDs are handed out in order
    bool starts_tasks;   // Code that starts tasks, calling an async function without await: they're the world's, in order
    loc starts_at;       // ...where it first does

    // Async functions and handlers: they run as tasks, which can wait
    bool is_async;       // `async`
    loc async_at;        // The `async` keyword
    int task_side;       // An async function's world, from what it does: TASK_EITHER, TASK_MATCH or TASK_LOCAL
    loc task_side_at;    // ...where it first decides
    bool task_root;      // Something starts it without await: its tasks have a table of their own
    bool runs_in[2];     // Worlds it runs in, the match's ([0]) and the local one ([1]): codegen makes each
    VEC(struct decl *) awaits;  // Async functions it awaits, once each: their frames are in its own
    VEC(loc) await_at;          // ...and where it first awaits each
    VEC(struct decl *) callees; // Functions it calls, once each
    VEC(loc) callee_at;         // ...and where it first calls each
    uint64_t device_uses[DEVICE_WORDS]; // Device values it reads through parameters, a bit per device leaf
    loc position_at;     // ...where it first reads a position in the window that way (the mouse's, a touch's)
    const char *position_what; // ...whose: "the mouse's position"
    VEC(world_use) world_uses; // Query reads and World access, by component: query declaration order, then first body use
    // A function or method that takes the World: what it reaches through it, itself and through the
    // functions it passes it on to (its world_uses, once every body is checked), says which worlds it's
    // handed in generated C: the match's (`tide_w`, which it changes if it sets), this machine's (`tide_l`)
    bool world_match;
    bool world_local;
    bool world_sets;
    comp_mask need_mask; // Components an entity must have (access and `with`).
    comp_mask without_mask;
    // A view's `verified` parameters and filters name the world the server has
    // confirmed. With components of both worlds, it runs for the entities of
    // the world views draw that are in the confirmed one too, with these there
    // (verified_join). With `verified` components only, it runs for the
    // confirmed world's entities, and need_mask and without_mask are theirs
    // (verified_entities).
    comp_mask verified_need;
    comp_mask verified_without;
    bool verified_join;
    bool verified_entities;
    bool takes_verified; // Some parameter of it is `verified`, a singleton's too: it needs a confirmed world to run
    int ownership;       // OWNERSHIP_WITH or OWNERSHIP_WITHOUT: a view's `with ownership` or `without ownership`, which its rows are checked for
    VEC(struct decl *) after; // Systems or views that must run first ([After], and [Before] on them)
    struct decl *order_enum; // [Stage]: the enum whose declaration order defines the phases
    const enum_member *order_member;
    int order_offset; // Position within the phase, not added to the enum's value
    loc order_at;
    // A command's ordering: [Channel(Channels.Uploads)], a channel of the
    // game's, numbered from 1 in program.channels (0 is the default
    // channel), or [Unordered], no order at all (TIDE_COMMAND_UNORDERED)
    struct decl *channel_enum;
    const enum_member *channel_member;
    uint32_t channel;
    bool unordered;
    loc channel_at;

    // Systems in the tick, from analyze_parallelism
    VEC(system_wait) waits;       // Earlier systems it must wait for: every one that's its own, and the first few through others
    int waits_unlisted;           // ...and how many more it waits for through others
    VEC(struct decl *) alongside; // Systems it can run at the same time as: the first few
    int alongside_count;          // ...and how many there are
    int stage;                    // 1 + the deepest stage it waits for; 0 before the analysis

    // Frags and verts (DECL_SHADER), and the functions they call
    bool is_frag;                 // A frag; else a vert
    int outputs;                  // A frag: the colors it writes, its `out Color` parameters, which go to a target's color images in that order
    VEC(shader_sample) samples;   // The grids it samples, once each
    int gpu_state;                // Code checked for the GPU: 1 while it is, 2 once it fits, 3 if it doesn't
    VEC(struct decl *) bound;     // Views: components and singletons the frags and verts of its mesh draw calls read, which it doesn't take itself
} decl;

// ---------------------------------------------------------------------------
// Expressions

typedef enum expr_kind {
    E_INT,
    E_FLOAT,
    E_BOOL,
    E_STRING,  // "text"; `text` holds it without the quotes, escapes as written
    E_NAME,
    E_MEMBER,
    E_CALL,    // name(args): float3(...), Spawn(...)
    E_METHOD,  // object.name(args): e.Add(...)
    E_BINARY,
    E_UNARY,
    E_LITERAL, // Transform { position = ... }
    E_CONDITIONAL, // cond ? lhs : rhs
    E_INTERP,  // $"score {score}": `parts` around the values in `args`, each with its format
    E_INDEX,   // object[lhs]: a list's element
    E_LIST,    // [a, b, c]: a list of `args`, whose type comes from where it goes
    E_THIS,    // this: the entity the code runs for
    E_DEFAULT, // `default`: the default value of the type where it goes
    E_NULL,    // `null`: the nothing of the T? where it goes
    E_COALESCE,  // lhs ?? rhs: lhs's value, or rhs when it failed or is nothing
    E_IS,        // lhs is Type name: whether lhs holds a value (or error) of that type, which `binding` names
    E_TRY,       // try lhs: lhs's value, or its error passed on to the caller
    E_DEFAULTED, // lhs!: lhs's value, or its type's default when it failed or is nothing
    E_AWAIT,     // await lhs: an async call's value once it's done, or Wait.Ticks(n) and the like
} expr_kind;

// What `x is ...` looks for.
typedef enum is_pattern {
    IS_VALUE,  // is int score: the value
    IS_ERROR,  // is ParseError why: the error
    IS_MEMBER, // is ParseError.Empty: that error, one of an enum's members (enum_member)
} is_pattern;

typedef enum builtin_call {
    CALL_NONE,
    CALL_CONSTRUCT, // float3(...), int(...), quaternion(...), float4x4(...), and an enum's Card(number)
    CALL_BUILTIN,   // Math.Dot(...), quaternion.AxisAngle(...): calls c_callee
    CALL_DRAW,      // Draw.Circle(...): calls c_callee with the view's draw list first
    CALL_AUDIO,     // Audio.Play(...): calls c_callee with the frame's audio list first (tide_l->tide_audio)
    CALL_SPAWN,
    CALL_ADD,
    CALL_REMOVE,
    CALL_DESTROY,
    CALL_METHOD,    // stats.IsDead(), or IsDead() inside another of Stats' methods
    CALL_FUNCTION,  // Heal(unit.stats, 5), or Combat.Heal(...)
    CALL_SEND,      // Send(RoundOver { ... }), or target.Send(Hit { ... }): type_decl is the event
    CALL_LOAD,      // Scene.Load(Arena { ... }): a spawn whose entity is its own scene; type_decl is the scene
    CALL_UNLOAD,    // Scene.Unload(scene)
    CALL_SCENE_PLAYER, // Scene.AddPlayer(scene, player) and Scene.RemovePlayer(scene, player)
    CALL_SESSION,   // Session.Start, Open, Close, Join, Connect and Leave: `name` says which; type_decl is Start's scene
    CALL_CLIPBOARD, // Clipboard.Copy(text)
    CALL_SNAP,      // entity.Snap() or singleton.Snap(): views draw it as it is this tick; type_decl is a singleton's
    CALL_GUI,       // GUI.Button(...), GUILayout.Horizontal() { ... }: calls c_callee with the GUI first
    CALL_ACTION,    // content(): runs the Action its function was given
    CALL_TEXT,      // name.Contains(...), text's methods: calls c_callee with the text first
    CALL_WORLD,     // world.Get<Health>(e), Has and Set, the World's methods: `name` says which; type_decl is the component
    CALL_LIST,      // items.Add(...), a list's methods: `name` says which
    CALL_MAP,       // scores.Has(k), a map's or a set's methods: `name` says which; type_decl is the map
    CALL_WAIT,      // Wait.Ticks(n), Wait.Seconds(s) and Wait.Frames(n), awaited: `name` says which
    CALL_GRID,      // cells.Clear(), a grid's methods: `name` says which; type_decl is the grid
    CALL_NEW_GRID,  // Grid2(1024, 1024) or Grid3(...): a grid with that size (0: open), whose type comes from where it goes
    CALL_DISCARD,   // pixel.Discard(), in a frag: the pixel isn't drawn
    CALL_RANDOM,    // random.NextInt(6), a Random's methods: calls c_callee with the generator's address first
} builtin_call;

// What a GUI call needs besides its arguments, and which form of a draw
// setting a call is (expr.gui).
enum {
    GUI_ID = 1,        // A widget ID, from where it's called
    GUI_CONTAINER = 2, // A block after the call: GUILayout.Vertical() { ... }
    GUI_SKIPS = 4,     // A container whose block doesn't always run: it returns -1 then
    DRAW_BLOCK = 8,    // A draw setting around a block, which puts back what it found: Draw.Blend(...) { ... }
    DRAW_SET = 16,     // ...and its other form, for the draws that follow, which takes no block: Draw.SetBlend(...)
};

// How a constructor call builds its value.
typedef enum ctor_form {
    CTOR_SCALAR,         // float(x), int(x)
    CTOR_SPLAT,          // float3(1): every component the same
    CTOR_CONVERT,        // float3(int3), int3(float3)
    CTOR_COMPONENTS,     // float4(v.xy, 1, 2): components from scalars and vectors
    CTOR_QUAT_FROM_F4,   // quaternion(float4)
    CTOR_QUAT_FROM_MAT,  // quaternion(float3x3)
    CTOR_MAT_COLUMNS,    // float3x3(c0, c1, c2)
    CTOR_MAT_SCALARS,    // float2x2(m00, m01, m10, m11), row by row
    CTOR_MAT_FROM_QUAT,  // float3x3(quaternion)
    CTOR_MAT_FROM_ROT_T, // float4x4(float3x3 rotation, float3 translation)
    CTOR_PLAYER,         // PlayerID(index)
    CTOR_COLOR,          // Color(r, g, b) or Color(r, g, b, a)
    CTOR_RECT,           // Rect(x, y, width, height)
    CTOR_ENUM,           // Card(number), Combat.Stance(number): the enum's value with that number; type_decl is the enum
} ctor_form;

typedef enum binding_kind {
    BIND_NONE,
    BIND_PARAM,
    BIND_LOCAL,
    BIND_TYPE,  // A component or event name used as a value: Spawn(Player), Send(RoundOver).
    BIND_FIELD, // A field of the input, named directly inside its Sample or Sanitize.
    BIND_NAMESPACE, // `Combat` in Combat.Health
    BIND_DEVICES,   // `Devices`: this machine's devices, in views and the input's Sample
    BIND_CONST,     // A constant, MAX_HEALTH or Combat.MAX_HEALTH: `constant` is it
    BIND_SHADER,    // A frag or vert named at mesh draw: `type_decl` is it
    BIND_ASSET,     // An asset, hero or Art.hero: `constant` is its declaration, read as a value of its type
} binding_kind;

typedef enum input_edge {
    EDGE_NONE,
    EDGE_DOWN, // input.jump.down: true now, false last tick
    EDGE_UP,   // input.jump.up: false now, true last tick
} input_edge;

typedef struct field_init {
    str name;
    loc at;
    expr *value;
    field *field;
} field_init;

struct expr {
    expr_kind kind;
    loc at;
    type type;

    // E_INT, E_FLOAT. int_value is a case label's value too, worked out by the checker.
    str text;
    int64_t int_value;
    bool is_long; // E_INT: a long literal (an L suffix, or a value past int's range)

    // E_BOOL
    bool bool_value;

    // E_NAME, and the callee/method name for E_CALL/E_METHOD
    str name;
    binding_kind bind;
    param *param;       // BIND_PARAM
    stmt *local;        // BIND_LOCAL: the S_VAR declaring it
    decl *type_decl;    // BIND_TYPE, E_LITERAL's type, CALL_SEND's event, and CTOR_ENUM's enum
    decl *constant;     // BIND_CONST, on an E_NAME or E_MEMBER

    // E_MEMBER, E_METHOD
    expr *object;
    str member;
    field *field;          // Field of a component, singleton, input or record; BIND_FIELD's field.
    input_edge edge;       // .down / .up on an input field.
    int swizzle_len;       // Vector swizzle: number of components (0 if not a swizzle).
    int swizzle[4];        // Component indices: 0..3 for x, y, z, w.
    const char *c_constant; // Static member such as quaternion.identity, as C.
    const enum_member *enum_member; // Page.Title: the member (type_decl is its enum)
    bool typed_text;       // keyboard.text: what was typed, which C keeps as code points
    bool local_field;      // box.verts: a local field of a match component or singleton, in its local part (`field` is that one's)
    bool raw_sampled;      // A [Raw] Texture asset's name as the object of a Sample, which is the one place it goes
    bool target_colors;    // target.colors: a render target's color images, of which an index reads one (target.colors[1])
                           // ...and cube.faces: a Cubemap's faces, of which an index is one as a RenderTarget (cube.faces[2])


    // E_CALL, E_METHOD
    VEC(expr *) args;
    // Arguments written with their parameter's name, `crit: true`, and
    // parameters a call leaves out for their defaults. The parser leaves
    // `args` as written. Where a call names arguments or leaves some out, the
    // checker makes `args` what each parameter gets, in the parameters' order
    // (a parameter's own default where the call wrote nothing, the same
    // expression at every such call), and keeps what was written in `written`.
    VEC(str) arg_names;   // Each argument's name as written, empty for one without; no items when none has one
    VEC(loc) arg_name_at;
    bool settled;         // `args` is in the parameters' order, defaults filled in: `written` has what the call wrote
    VEC(expr *) written;  // The arguments as written, for a settled call (none, when it wrote none)
    VEC(int) arg_order;   // Named arguments out of the parameters' order: each written argument's place in `args`, in the order written, which they're worked out in
    bool names_taken;     // The checker matched the names to parameters; it refuses names on calls that take none
    builtin_call call;
    ctor_form ctor;         // CALL_CONSTRUCT; CTOR_ENUM even where the checker refused the conversion
    const char *c_callee;   // CALL_BUILTIN
    const char *c_pack;     // ...and the function that makes the one value it takes of its arguments, or NULL
    VEC(type) arg_want;     // CALL_BUILTIN, CALL_METHOD and CALL_FUNCTION: the type each argument converts to.
    struct decl *method;    // CALL_METHOD and CALL_FUNCTION: the method or function called
    comp_mask spawn_mask; // CALL_SPAWN: components the new entity has.
    int spawn_archetype;  // CALL_SPAWN: index into the archetype list.
    bool local_world;     // CALL_SPAWN, CALL_ADD, CALL_REMOVE, CALL_DESTROY and CALL_SEND: in the local world
    struct stmt *compare[2]; // CALL_LIST Sort: the two elements its block compares, as read-only locals of the block
    const char *hoisted;  // Spawns, and calls, units and operators that keep their order: the temporary codegen ran it into, before the statement
    bool kept;            // ...or any value read before a call that writes: `hoisted` is its copy, as it was then
    unsigned arg_mut;     // CALL_GUI: a bit per argument the call changes, which it takes by address
    unsigned arg_out;     // ...and per argument it makes, for an `out` parameter: its bit in arg_mut is set too
    int gui;              // CALL_GUI: GUI_ID and GUI_CONTAINER; CALL_DRAW: DRAW_BLOCK and DRAW_SET
    struct stmt *block;   // A call's block, written in braces after it: Foldout("Audio") { ... }
    bool stacked;         // One of the calls listed with commas before one block: each one's block is the
                          // call after it, and the last one's the block itself
    struct decl *vert;    // mesh draw: the vert and the frag it names, or NULL
    bool instanced;      // Instanced: its third argument supplies the instances
    bool blit;           // Draw.Blit: source texture, destination target and a frag
    bool targeted;       // A RenderTarget receiver supplies this draw's destination
    bool resource_operation; // RenderTarget creation, resize or disposal: ordered local side effects
    int target_formats;      // RenderTarget(...) of several color images: how many of its last arguments are their formats
    bool target_samples;     // ...which `samples: n` comes after: the call's last argument, and its formats go to C as several, however many
    bool io_operation;       // File/Directory operation: gets this machine's provider
    bool path_operation;     // Pure Path operation: receives tide_str, rather than a C string
    bool storage_flush;      // Storage.Flush, awaited through a provider completion ticket
    bool web_fetch;          // Web.Fetch, awaited the same way: its body is the task's value
    int draw_dims;        // CALL_DRAW: 0 for a call that selects no destination; else the window's, and a mesh's vertex dimensions
    struct decl *instance_type; // Its List<T> element struct; NULL for List<float4x4>
    struct expr *values;        // mesh draw and Blit: the struct it gives its vert and frag after their names, or NULL
    struct decl *values_type;   // ...and its type: sent once, for the draw
    struct decl *frag;
    struct shader *shader; // ...and what draws it, once planned: NULL for a List<Vertex> or List<Vertex3> with neither
    str type_arg;         // E_METHOD: the type written in <> after its name, world.Get<Health>(e), or empty
    loc type_arg_at;      // ...its last part, if it's qualified
    loc type_arg_qual_at; // ...and where it starts
    const type_ref *type_arg_ref; // ...as written, or NULL

    // E_BINARY, E_UNARY; E_CONDITIONAL's two sides. `method` is the struct's
    // operator, if one is used.
    tok_kind op;
    expr *lhs;
    expr *rhs;
    expr *cond; // E_CONDITIONAL

    // E_INTERP: the text before, between and after the values (escapes as written),
    // and each value's format as written ("F2") and as tide/text.h takes it
    VEC(str) parts;
    VEC(str) formats;
    VEC(int32_t) format_codes;

    // E_LITERAL
    VEC(field_init) inits;
    loc qual_at; // Where a qualified name starts (Combat.Health { }); `at` is its last part

    // E_IS: the type after `is` as written ("ParseError", or "ParseError.Empty"),
    // and the local its name declares, an S_VAR with no value
    str pattern;
    const type_ref *pattern_ref; // ...as written (an enum's member reads as a qualified name)
    loc pattern_at;      // Its last part
    loc pattern_qual_at; // Where it starts
    is_pattern looks_for;
    struct stmt *binding;
    bool binding_ok;     // In an if's or loop's condition, joined by &&: its name is in scope where it's true

    // An argument written with `out`, for an `out` parameter, as C#'s. The
    // expression is the variable the call makes: one the code has (`out
    // health`, `out body.health`), or an E_NAME of the local the argument
    // declares (`out var health`, `out Health health`: `out_local`, an S_VAR
    // with no value) or of a place nothing names (`out _`: `out_local` too,
    // with `discard`). Once checked it's bound like any variable, so the code
    // that takes a `mut` argument by address takes it the same way.
    bool is_out;
    loc out_at;             // The word
    struct stmt *out_local; // The local it declares, or the place `out _` makes

    // E_INDEX of the grid a parallel loop goes over, at its step's place plus
    // constants: read from, or written in, the step's window (the loop's
    // `windowed`), this far from its place.
    bool window;
    int window_offset[3];
};

// ---------------------------------------------------------------------------
// Statements

typedef enum stmt_kind {
    S_BLOCK,
    S_IF,
    S_RETURN,
    S_VAR,
    S_ASSIGN,
    S_EXPR,
    S_SWITCH,
    S_BREAK,
    S_WHILE,    // while (cond) then_stmt
    S_FOR,      // for (init; cond; step) then_stmt: each part optional
    S_CONTINUE,
    S_FOREACH,  // foreach (var name in value) then_stmt: `type` is the element's, and it's the variable's declaration
    // parallel (var name in value by by offset offset) then_stmt: a grid's cells
    // or blocks, at once; `type` is a position's, and it's the variable's declaration
    S_PARALLEL,
    S_FAIL,     // fail value;: ends a function that `fails` with an error
} stmt_kind;

// A switch's section: its labels, then the statements they run.
typedef struct switch_case {
    VEC(struct expr *) labels; // `case` values; NULL for `default`
    VEC(loc) label_at;         // Each label's `case` or `default`
    VEC(struct stmt *) body;
} switch_case;

struct stmt {
    stmt_kind kind;
    loc at;

    // S_BLOCK
    VEC(stmt *) stmts;
    loc end; // The closing brace, or where a block cut short by a syntax error ends

    // S_IF, and S_SWITCH's value; S_WHILE and S_FOR, with then_stmt their body
    expr *cond;
    stmt *then_stmt;
    stmt *else_stmt;

    // S_FOR
    stmt *init; // A local, an assignment or a call, or NULL
    stmt *step; // An assignment, i++ or a call, or NULL

    // S_SWITCH
    VEC(switch_case) cases;

    // S_VAR
    bool is_mut;
    bool loop_var; // Declared by a for: only its step changes it, unless it's mut
    bool read;     // Something reads it: its name is somewhere other than where an assignment writes
    bool assigned; // An assignment writes it, after its declaration
    bool unused;   // Nothing reads it (a local a statement declares): the checker warns
    int errors_before; // The errors reported before it was declared: more by its scope's end hide uses
    // A local an `out` argument declares, `out var health`: it has no value of
    // its own, and is unassigned until the call that makes it has run
    bool out_decl;
    bool discard;  // ...or the place `out _` makes for what the call gives, which no code names
    const expr *out_arg; // ...and the argument that declares it
    str type_name; // Empty for `var`. With its namespace if it's qualified.
    const type_ref *type_ref; // ...as written; NULL for `var`
    str name;
    type type;
    loc type_at;      // The type's name (the last part if it's qualified)
    loc name_at;
    loc type_qual_at; // Where the type starts: its namespace if it's qualified

    param *query; // S_FOREACH over a query parameter; its compiler-only row is `type`

    // S_VAR initializer (NULL for the name after `is`), S_ASSIGN value, S_EXPR
    // expression, S_RETURN value (or NULL), S_FAIL error
    expr *value;

    // S_PARALLEL: each step's block (a constant int or vector; NULL for a
    // cell) and where blocks start (NULL for 0), and the block's cells along
    // each axis. A foreach over a grid whose steps only touch their own cell
    // runs as one (`parallel`).
    expr *by;
    expr *offset;
    int block[3];
    bool parallel;
    // A parallel loop whose steps read and write the cells near their places
    // through a window: each task copies the cells its blocks reach, from
    // window_lo to window_hi cells around a block's place, once, and its
    // steps read and write those as plain memory (tide_par_window).
    bool windowed;
    int window_lo[3];
    int window_hi[3];

    // S_ASSIGN
    expr *target;
    tok_kind op; // T_ASSIGN, a compound one like T_PLUS_ASSIGN, or T_PLUS_PLUS and T_MINUS_MINUS (value 1)
    struct decl *operator_decl; // A compound assignment's struct operator: `money += tip` uses `+`
};

// ---------------------------------------------------------------------------
// Program

// Something a shader reads that a draw supplies: a field of a component, one
// for each instance, or of a singleton, one for each draw. Its floats and ints
// are words of the instance's or the uniforms' float4s and int4s.
typedef struct shader_value {
    const struct decl *source; // The component or singleton; NULL for the transform mesh draw was given
    const field *field;
    bool per_instance;
    int floats, ints;       // Words it takes of each
    int float_at, int_at;   // ...and where they start
    bool vert_reads, frag_reads;
    int frag_float_at, frag_int_at; // An instance's that the frag reads: where it is among what the vertex stage passes on
} shader_value;

typedef struct shader_texture {
    const struct decl *source;
    const field *field;
    int filter;  // Its Filter's number, tide_filter's, where it's known here (filter_said)
    bool direct; // Supplied explicitly by Draw.Mesh or Draw.Blit, rather than a field
    const struct decl *asset; // A Grid2<Color> or Texture asset, sampled by name: neither a field nor the draw's
    int wrap;    // Its Wrap's number, tide_wrap's: 0 where the draw says (a `textured` shader's own)
    shader_read read; // A DepthTexture field's: as a number, or compared, each a texture of its own
    bool filter_said, wrap_said; // As its samples' (shader_sample): false for what's left to the Texture it's given
} shader_texture;

// A struct a vert writes for its frag, an `out` parameter of the vert's: the
// fields the frag reads go from the mesh's corners to its pixels, blended.
typedef struct shader_handed {
    const struct decl *type;
    const param *from; // The vert's `out` parameter
    const param *to;   // The frag's parameter of that type; NULL when it takes none
    bool *fields;      // By the struct's field: whether the pixels read it
} shader_handed;

// What draws a mesh: a vertex struct with a vert, a frag, both or neither, one
// for each combination a mesh draw uses (see shader.c). Codegen writes each
// as a tide_shader (tide/draw.h).
typedef struct shader {
    int index;
    struct decl *vertex; // The mesh's vertex: a struct
    struct decl *vert;   // NULL: the vertices as they are
    struct decl *frag;   // NULL: the vertex's color times the texture
    int dims;            // 2 or 3: its position's
    bool textured;       // No frag, and a texture given at the draw, read as that texture says
    bool direct_texture; // The draw supplies a Texture: the one it draws with, or its shader stages' source
    bool blit;           // A full destination overwrite with a fixed Vertex quad
    bool instanced;      // Its draw supplies a list of instances
    struct decl *instance_type; // Explicit instance data, or NULL for transforms only
    struct decl *values_type;   // The struct its draws give after their vert and frag, sent once for each draw, or NULL
    const field *instance_transform; // The instance struct's optional float4x4 transform
    VEC(shader_value) values;
    VEC(shader_texture) textures;
    VEC(struct decl *) bound; // The components and singletons it reads, in the order its draws pass them
    VEC(shader_handed) handed; // What its vert writes for its frag, in the order the vert takes them
    int instance_floats, instance_ints; // Words
    int uniform_floats, uniform_ints;
    int frag_floats, frag_ints; // Words of the instance the vertex stage passes on to the pixels
    bool *frag_fields;          // By the vertex's field: whether its pixels read it
    bool frag_world;            // ...and whether they read where they are in the world
    const char *name;           // For messages: "Wave and Shade on BoxVertex"
    const char *glsl_vertex, *glsl_fragment, *wgsl;
} shader;

// A mesh draw with a vertex struct of the game's, a vert or a frag, and the
// view (or function) it's in.
typedef struct mesh_draw {
    struct expr *call;
    struct decl *in;
} mesh_draw;

typedef VEC(comp_mask) mask_list;

// An Add or Remove, and whose entity it changes: the system (or handler, or
// view) it runs for, through `this`; the entity a Spawn or Scene.Load made in
// the same code, through a local that isn't mut; or, with neither, any
// entity of its world (one read from a field or a parameter). The archetypes
// derive from the ones each site can reach.
typedef struct change_site {
    comp_mask comps;
    const struct decl *from;
    const struct expr *spawn;
    bool local;
} change_site;

typedef VEC(change_site) change_sites;

// Declarations by a hash of a name of theirs, for the checker (check.c).
typedef struct decl_index {
    int *first;  // By bucket: the first declaration there, or -1
    int *next;   // By declaration: the next one in its bucket, in decls' order, or -1
    int buckets; // A power of two
    int count;   // The declarations it covers
} decl_index;

typedef struct program {
    VEC(unit *) units; // Its files, in the order they're parsed: sorted by path
    VEC(decl *) decls; // In source order, file by file, builtins first.
    VEC(decl *) replaced; // What overrides replaced (decl.replaced_by), which is no longer in `decls` or any list made of it
    bool dry;             // A copy of the program that replaced code is checked against: what it would add to the build is dropped
    decl_index by_name;   // ...by namespace and name, once the checker asks
    decl_index by_c_name; // ...and by name in generated C

    // Filled by the checker
    VEC(decl *) components;
    VEC(decl *) singletons;
    VEC(decl *) systems;
    VEC(decl *) views;
    VEC(decl *) handlers; // Every event handler, in the order they run
    VEC(decl *) events;   // Built-in ones first
    VEC(decl *) command_types; // Commands are event-shaped values, with a separate incoming wire API
    VEC(command_channel) channels; // The channels [Channel] names, in order: channel i + 1
    decl *spawned;        // Built-in events
    decl *destroyed;
    decl *player_joined;
    decl *player_left;
    decl *main;          // The scene named Main, where the program starts
    int main_archetype;  // Main's archetype
    decl *input;         // The input declaration, if any.
    decl *scene_visibility; // The built-in enum SceneVisibility
    decl *anchor;        // The built-in enum Anchor: where GUILayout.Area goes, and the place of a text's box
    decl *font;          // The built-in enum Font, the weight text is drawn in
    // Whether code names Font.Bold, or makes a Font of a number: only then does
    // generated C name the bold weight's file, so only then does the game carry it.
    bool names_bold;
    // Whether code names Filter.Trilinear: only then does generated C have the member, and only then is a
    // Sample that leaves its filter to the Texture it's given written so that it can read between that
    // texture's smaller images. So a game that doesn't generates the C and the shaders it did.
    bool names_trilinear;
    decl *vertex;        // The built-in struct Vertex, a corner of what mesh draw draws
    decl *vertex3;       // The built-in struct Vertex3, a corner of a 3D mesh
    decl *shape;         // The built-in struct Shape, what Draw.Shape draws
    decl *gui_theme;     // The built-in struct GUITheme, how the GUI's widgets look
    decl *gui_control;   // The built-in struct GUIControl, what GUI.Control says of a control
    decl *sound;         // The built-in struct Sound, a sound source a local field keeps: in generated C when named
    decl *filter;        // The built-in enum Filter, how mesh draw samples its texture
    decl *wrap;          // The built-in enum Wrap, what a uv past a texture's edges reads: in generated C when code names it
    decl *blend;         // The built-in enum Blend, how what's drawn goes over what's there
    decl *depth;         // The built-in enum Depth, what a 3D mesh does with depth: in generated C when code names it
    decl *cull;          // The built-in enum Cull, which side of a 3D mesh's triangles isn't drawn: the same
    decl *format;        // The built-in enum Format, what a render target holds: in generated C when code names it
    decl *stencil_test;  // The built-in enum StencilTest, the test a draw's stencil number passes: the same
    decl *stencil_op;    // The built-in enum StencilOp, what becomes of the stored number where a draw draws: the same
    decl *io_error;      // The built-in IOError enum, shared with tide/io.h
    VEC(struct expr *) io_calls; // Checked filesystem calls, for result adapters in generated C
    bool decodes;        // grid.Decode(bytes) is called: its adapter and tide/png.h go in the generated C
    type decode_result;  // ...and what it gives: void fails IOError
    decl *pixel;         // The built-in record Pixel, a frag's parameter: where its pixel is
    decl *instance;      // The built-in record Instance, a frag's or vert's: how its mesh was drawn
    decl *time;          // The built-in singleton Time
    decl *random;        // The built-in struct Random: a random number generator's state
    // Time.seed, the match's seed: Time's last field while code is checked, and
    // only in the world when something uses it (uses_seed): code that reads it,
    // or a Random in the match's state, which the engine seeds from it.
    field *seed;
    bool uses_seed;
    // Application.title is read: generated C has the game's title as text
    // (its title setting, or the program's name), and no other game's does.
    bool uses_title;
    bool uses_audio;     // Some code plays sounds: the local state carries the frame's audio list
    VEC(mesh_draw) mesh_draws; // Every mesh draw that draws through a shader
    VEC(shader *) shaders;     // ...and the shaders they draw through, once each
    decl *session;       // The built-in local singleton Session: this machine's part in a match
    decl *connected;     // Built-in local events: this machine joined a match, and left it
    decl *disconnected;
    decl *forgotten;     // ...and a match entity's local fields are going, as it's gone
    VEC(decl *) local_parts; // The local parts of the match's components and singletons (decl.local_part), in their order
    VEC(decl *) start_scenes; // Scenes Session.Start starts matches in
    bool out_args;       // Some call has an `out` argument: generated C declares the locals they make before their statements
    bool uses_text;      // Some code makes text, in the scratch area the run functions clear
    bool uses_heap;      // Some field holds text or a list: the worlds have a heap
    bool local_heap;     // Some local field of the match's does: the local world has a heap, whether the match has or not
    VEC(decl *) lists;   // Every List<T> type the program uses, one per element type
    VEC(decl *) maps;    // Every Map<K, V> and Set<K> type, one per key and value type
    VEC(decl *) grids;   // Every Grid2<T> and Grid3<T> type, one per cell type and dimensions
    VEC(decl *) arrays;  // Every T[N] type, one per element type and length
    VEC(decl *) results; // Every T? and `T fails E` it uses, one per combination
    decl *owner;         // The built-in Owner component.
    decl *devices;       // The built-in Devices record.
    VEC(decl *) records; // Built-in records: Devices, Keyboard, Mouse, Gamepad, Dpad, Button.
    VEC(device_leaf) device_leaves; // Each value the devices send: a button, stick, trigger, axis, int or bool
    bool match_devices;  // Systems or handlers take Devices: the input sends the devices too
    uint64_t device_uses[DEVICE_WORDS]; // The leaves match code reads: all the input sends of them
    VEC(decl *) structs; // In an order where each comes after the structs it contains.

    mask_list archetypes;      // Component masks (the components that aren't sparse), in derivation order.
    mask_list possible;        // Per archetype: the sparse components its entities can have (stored with a presence byte each)
    comp_mask sparse_mask;     // The sparse components (decl.is_sparse)
    VEC(bool) archetype_local; // Per archetype: in the local world rather than the match
    VEC(bool) spawn_target;    // Per archetype: does some Spawn create it directly?
    comp_mask spawned_mask;    // Union of components that appear in spawns.
    comp_mask added_mask;      // Components that appear in Add.
    comp_mask removed_mask;    // Components that appear in Remove.
    change_sites adds;         // Every Add, and whose entity it changes
    change_sites removes;      // Every Remove, likewise
    bool uses_destroy;
    VEC(struct fix) fixes;     // Quick fixes for editors

    // The engine's settings for the game (builtins.h), from `settings { ... }`
    VEC(decl *) settings;      // Each block written, though a game has one
    VEC(decl *) assets;        // Every asset declaration, in source order: what the pack holds
    const char *game_folder;   // Where the game's .tide/ is (tide says; NULL for tidec on its own): asset hashes and fetched URLs are kept there
    uint32_t tick_rate;        // tickRate, or 0 where it isn't set
    uint32_t max_players;      // maxPlayers, or 0 where it isn't set: TIDE_PLAYERS_DEFAULT
    const expr *title;         // title, the text it's set to, or NULL
    const expr *app_id;        // appId, likewise
    const expr *version;       // version, likewise
    bool host_migration;       // hostMigration
    bool delay_prediction;     // prediction = Prediction.Delay: clients never predict (tide_prediction)
    bool stats;                // stats: the host shows its stats overlay
    bool debug_views;          // debugViews: hosts show their debug views when asked
    decl *prediction;          // The built-in enum Prediction
} program;

// A change that fixes a diagnostic, which editors offer as a quick fix.
typedef enum fix_kind {
    FIX_ADD_MUT,    // Writing through a read-only parameter: declare it mut
    FIX_REMOVE_MUT, // A mut parameter that's never written
    FIX_USE_WITH,   // A component parameter that's never used: filter with `with` instead
    FIX_MUT_LOCAL,  // Writing a read-only local: declare it mut
    FIX_MUT_METHOD, // A read-only method that changes a field or calls a mut method: declare it mut
    FIX_CREATE_FUNCTION,  // A call of an unknown function: declare it, taking the arguments' types
    FIX_CREATE_STRUCT,    // An unknown type where a struct fits: declare one
    FIX_CREATE_COMPONENT, // An unknown type where a component fits: declare one
    FIX_USE_THIS,   // An Entity or LocalEntity parameter: remove it, and name the entity `this`
    FIX_UNWRAP,     // A failable call's or a T?'s value that nothing handles: `!` after it, or `try` before it
    FIX_UNUSED_LOCAL, // A local that's never read, with nothing to offer: it's assigned later, or a for declares it
    FIX_REMOVE_LOCAL, // ...whose value does nothing: remove its declaration
    FIX_KEEP_CALL,    // ...whose value is a call that does something: keep the call, as a statement of its own
    FIX_LOCAL_WORD,   // `local` on a field where it means nothing: remove the word
    FIX_ADD_OUT,      // An argument for an `out` parameter, written without the word: `out` before it
    FIX_REMOVE_OUT,   // `out` before an argument no `out` parameter takes: remove the word
    FIX_DISCARD_OUT,  // A local an `out` argument declares that nothing reads: `out _` instead
    // A return, or a function's end, that leaves an `out` parameter (`param`) of `method` unassigned:
    // `x = default;` before it. At the function's name for its end
    FIX_ASSIGN_OUT,
    // An `out` parameter (`param`) of `method` read before it's assigned: `x = default;` as the function
    // starts, or the parameter as `mut`, with the word gone from its calls
    FIX_ASSIGN_FIRST,
    FIX_ADD_VIRTUAL,  // An override of a declaration (`method`) that isn't marked: `virtual` before it
    FIX_ADD_OVERRIDE, // A declaration (`method`) with the name and parameters of a virtual one in another package: `override` before it
} fix_kind;

typedef struct fix {
    fix_kind kind;
    loc at; // Where the diagnostic points
    const param *param;
    const stmt *local;   // FIX_MUT_LOCAL, and the unused local's
    const decl *method;  // FIX_MUT_METHOD; FIX_UNWRAP: the function `try` would pass the error on from, if it fits
    const expr *call;    // FIX_CREATE_FUNCTION; FIX_UNWRAP: the value to unwrap
    str name;            // FIX_CREATE_STRUCT and FIX_CREATE_COMPONENT
} fix;

program *program_new(void);

// Works out which systems in the tick can run at the same time, and why the
// others wait (fills decl.waits, alongside, their counts and stage). Two systems conflict
// when one writes a component or singleton the other reads or writes, unless
// their entities can never be the same (disjoint archetypes). Conflicting
// systems keep their order in the tick, and [Before]/[After] order them too.
// Needs the archetypes.
void analyze_parallelism(program *prog);

// Whether a system's entities are split across threads, a task per chunk of
// them: it runs per entity, and what it changes is its entities' own, in no
// order across them. One system at a time changes the heap, and a singleton is
// everyone's. Its spawns get temporary handles until it's done (tide/jobs.h).
// C is trusted: what it does on several threads at once is the game's to get
// right.
bool system_splits(const decl *sys);

// Whether a system reads through its World or a query a component it also changes
// through a parameter: it runs on one thread, and writes its rows in place, so
// what it reads of the rows it changed is the same on every machine.
bool world_reads_own_writes(const decl *sys);

// The components code reads (writes = false) or writes through World or queries:
// "Transform and Health". `quote` wraps names, written as code in `from`'s
// namespace writes them (qualified without one). Returns how many there are.
int world_use_names(const decl *sys, bool writes, const char *quote, const decl *from, sb *out);

// The loops a statement holds whose steps run at once, on threads: parallel
// loops, and foreach loops over grids that run as one.
int parallel_loops(const stmt *s);

// A grid type's chunk: its cells' size in bytes, and 1 << shift[i] cells
// along axis i (codegen.c).
void grid_shape(const decl *grid, int *cell, int shift[3]);

// Why `sys` waits for `w->on`, like "both write Transform". `quote` wraps names
// ("`" for Markdown).
void describe_wait(const decl *sys, const system_wait *w, const char *quote, sb *out);

// The file whose namespace a declaration's name is in: its own, or for an
// override in place, the file of what it replaced.
static inline const unit *decl_home(const decl *d)
{
    return d->named_in ? d->named_in : d->unit;
}

// A declaration's name as code in `from`'s namespace writes it: short in the
// same namespace, qualified elsewhere, and always qualified without `from`.
void put_decl_name(sb *out, const decl *d, const char *quote, const decl *from);

// The whole tick's schedule as text, for `tidec --schedule`.
void print_schedule(const program *prog, const char *game, sb *out);

// Parses one file into `prog`. Without `recover`, stops at the first syntax
// error and returns false. With it, reports every syntax error it finds and
// keeps what it could read, skipping a broken statement or declaration: for
// editors, which need structure while code is half typed.
bool parse_file(program *prog, const source *src, token *toks, bool recover);

// Whether the attributes starting at toks[i] (a '[') belong to a field: after
// the last ']' come a type name and a field name. Otherwise they belong to a
// declaration. For recovery, which takes a '[' at column 1 as a declaration.
bool attributes_before_field(const token *toks, int i);
bool check(program *prog);

// Definite assignment, as C#'s, for what starts unassigned: a function's or
// method's `out` parameters, which its code assigns before it reads them and
// on every path that returns, and the locals `out` arguments declare, which
// are only read where the call that makes them has run (assigned.c). `code`
// is the function or method whose body it is, or NULL for other code.
void check_assigned(program *prog, const decl *code, const stmt *body);

// Checks every frag and vert, and the functions they call, for what the GPU
// runs, and plans the shader each mesh draw draws through (program.shaders):
// what it reads, how that's laid out, and its GLSL and WGSL (shader.c).
void plan_shaders(program *prog);

// How the declaration of the texture `texture` names says it's sampled: the
// [Filter] and [Wrap] of an asset (by its name, or an element of its list or
// map) or of a Grid2<Color> field. Each is its enum member's number, or -1
// where the declaration says none, and for an expression that names no
// declaration: a Texture that's a value (a field's, a local's, a target's
// color), which is sampled as each draw and each Sample says.
void declared_sampling(const expr *texture, int *filter, int *wrap);
// Whether a texture's declaration says [Raw]: an asset by its name, or a grid's field.
bool declared_raw(const expr *texture);
bool field_is_raw(const field *f);

// Whether a shader reads a local field of the match's component or singleton
// `d`: its draws are handed d's local part too.
bool shader_reads_local(const shader *sh, const decl *d);

// Every shader's sources as text, for `tidec --shaders`.
void print_shaders(const program *prog, sb *out);
