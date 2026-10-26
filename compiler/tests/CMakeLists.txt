# End-to-end tests: e2e/<name>.tide (or the SOURCES given) is transpiled and
# compiled together with e2e/<name>_test.c and the shared test harness, then run.
# WARNINGS are the ones tidec has to give, and PACKAGES the files of packages
# the game builds with, each after its package's name, as for tide_add_game.
function(tide_e2e_test name)
    cmake_parse_arguments(ARG "LAYOUT" "" "SOURCES;WARNINGS;PACKAGES;DEFINES" ${ARGN})
    set(sources "e2e/${name}.tide")
    if(ARG_SOURCES)
        set(sources ${ARG_SOURCES})
    endif()
    set(target tidec_e2e_${name})
    set(layout)
    if(ARG_LAYOUT)
        set(layout LAYOUT)
    endif()
    tide_add_game(${target} ${layout} SOURCES ${sources} NAME game WARNINGS ${ARG_WARNINGS} PACKAGES ${ARG_PACKAGES} DEFINES ${ARG_DEFINES}
        HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/${name}_test.c")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    add_test(NAME ${target} COMMAND ${target})
endfunction()

tide_e2e_test(asset_sharing SOURCES e2e/asset_sharing.tide e2e/asset_sharing_c.c)
tide_e2e_test(hosting)
tide_e2e_test(move_clamp)
tide_e2e_test(map_iteration)
tide_e2e_test(structural)
tide_e2e_test(reach)
tide_e2e_test(sparse)
tide_e2e_test(rows) # A system's loop over a chunk's rows: pages shared with a snapshot or not, rows left out, chunks in pieces
if(TIDE_PLATFORM)
    target_link_libraries(tidec_e2e_rows PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_rows PRIVATE TIDE_ROWS_THREADS=1)
endif()
tide_e2e_test(world) # Other entities' components through the World
tide_e2e_test(component_loads) # Fixed-size entity loads preserve full layouts, current pages and absence on both worlds
add_test(NAME tidec_component_loads_codegen
    COMMAND "${CMAKE_COMMAND}" "-DGAME_C=${CMAKE_CURRENT_BINARY_DIR}/tide/tidec_e2e_component_loads/game.c"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/component_loads_codegen.cmake")
tide_e2e_test(world_set_own) # ...and this entity's own, beside a parameter that reads it

tide_e2e_test(world_set_read) # Read parameters follow preceding cross-entity Set writes and snapshot page replacement
if(TIDE_PLATFORM)
    target_link_libraries(tidec_e2e_world_set_read PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_world_set_read PRIVATE TIDE_SET_READ_THREADS=1)
endif()
tide_e2e_test(world_set_mut) # Self Set replaces active mutable bindings through helper chains and task resumes
if(TIDE_PLATFORM)
    target_link_libraries(tidec_e2e_world_set_mut PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_world_set_mut PRIVATE TIDE_SET_MUT_THREADS=1)
endif()
tide_e2e_test(world_functions) # ...and through functions and methods the World is passed on to
tide_e2e_test(properties)
tide_e2e_test(transforms) # Shared component placement, checked parenting and hierarchy state
tide_e2e_test(visibility_groups) # Shared audiences, whole-component restrictions and scene inheritance
tide_e2e_test(visibility_projection SOURCES e2e/visibility_projection.tide e2e/visibility_projection_c.c)
tide_e2e_test(shards) # One world split across shards: moving, ghosts, waiting on borders, checkpoints and splits
tide_e2e_test(shards_players) # A player's machine following their avatar from shard to shard
tide_e2e_test(shards_visibility) # A visibility group the root keeps, read on every shard and changed from a region
tide_e2e_test(prefabs SOURCES e2e/prefabs.tide e2e/prefabs.tprefab)
tide_e2e_test(scene_startup SOURCES e2e/scene_startup.tide e2e/scene_startup.tprefab)
tide_e2e_test(asset_references SOURCES e2e/asset_references.tide e2e/asset_references.tprefab)
tide_e2e_test(asset_reference_empty)
tide_e2e_test(skinned_asset_references) # Typed current-pack identities preserve skinned palette and LOD provenance
tide_e2e_test(function_acts) # Functions and methods that send, spawn and change entities, as their callers would
tide_e2e_test(component_values SOURCES e2e/component_values.tide e2e/component_values.tprefab) # Copied component and payload values
tide_e2e_test(texture_size) # Metadata-only CPU/GPU image dimensions, aliases and resource lifetime
tide_e2e_test(texture_capacity SOURCES shaders/texture_capacity.tide) # Twelve bindings, high sampling banks and ten-binding skinning.
tide_e2e_test(raw_texture_fields LAYOUT) # Raw texture references, captures, pack lifetime and role migration
if(TIDE_PLATFORM)
    target_link_libraries(tidec_e2e_raw_texture_fields PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_raw_texture_fields PRIVATE TIDE_RAW_TEXTURE_MIGRATION=1)
endif()
tide_e2e_test(function_tasks) # ...and start tasks, in their callers' worlds and for their callers' entities
tide_e2e_test(queries) # Read-only iteration over a separate entity set
tide_e2e_test(handler_queries) # Current-world queries at match event/command turns, with ordinary deferred changes
tide_e2e_test(query_borrows) # Borrowed helper queries retain selection, row projection, order and caller capabilities
tide_e2e_test(query_local_fields) # Current attached local fields through read-only and mutable query bindings
tide_e2e_test(event_containers) # Selectively retained container values in local event journals
if(TIDE_PLATFORM)
    target_link_libraries(tidec_e2e_queries PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_queries PRIVATE TIDE_QUERY_THREADS=1)
endif()
set_tests_properties(tidec_e2e_queries PROPERTIES TIMEOUT 120)
tide_e2e_test(query_blocks) # ...beside a comparison block and a parallel loop, which capture the code's other parameters
tide_e2e_test(world_local) # ...in views and local handlers
tide_e2e_test(debug)
tide_e2e_test(long)
tide_e2e_test(double)
tide_e2e_test(io)
tide_e2e_test(byte)
tide_e2e_test(maps)
tide_e2e_test(nested) # Lists of lists, maps of lists, lists of maps and sets, structs with lists as elements
target_compile_definitions(tidec_e2e_nested PRIVATE NESTED_HASH=9428574205239071589ull)
tide_e2e_test(arrays) # T[N]: fixed-length arrays in components, structs, lists, maps, events, inputs and locals
target_compile_definitions(tidec_e2e_arrays PRIVATE ARRAYS_HASH=8958662768801678541ull)
tide_e2e_test(arrays_plain_input) # Bounds and padding inside integer-only array input
tide_e2e_test(arrays_forward) # Arrays of structs whose fields and types are declared later
tide_e2e_test(sort)
tide_e2e_test(edge_cases WARNINGS "'settings' is never used" "system 'NeverRuns' never runs")
tide_e2e_test(empty_world WARNINGS "system 'PlayerMovement' never runs")
tide_e2e_test(unused_locals WARNINGS "local 'fwd' is never used" "local 'spark' is never used"
    "local 'bumped' is never used" "local 'total' is never used") # Locals nothing reads: warned about, and run as written
tide_e2e_test(defaults)
tide_e2e_test(default_value)
tide_e2e_test(math)
tide_e2e_test(float_bits)
tide_e2e_test(readonly_aggregates)
tide_e2e_test(screen_scale)
tide_e2e_test(input)
tide_e2e_test(input_borrows)
tide_e2e_test(crossplatform)
tide_e2e_test(evaluation_order SOURCES e2e/evaluation_order.tide e2e/evaluation_order_c.c) # Left to right, whatever order C would pick
tide_e2e_test(prototype_codegen) # Nested foreach receivers, stored vector writes and captured numeric literals
tide_e2e_test(increments) # ++i and i++ as values, in their turn
tide_e2e_test(random) # Random's state in the world, and the match's seed: the same bits on every platform
tide_e2e_test(random_local_order)
tide_add_game(tidec_e2e_random_local_first SOURCES e2e/random_local_order.tide NAME game DEFINES LOCAL_FIRST
    HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/random_local_order_test.c")
target_include_directories(tidec_e2e_random_local_first PRIVATE "${PROJECT_SOURCE_DIR}/tests")
add_test(NAME tidec_e2e_random_local_first COMMAND tidec_e2e_random_local_first)
tide_e2e_test(rolls) # Random.NextInt and the like on the type, and Random.seed: a system's rolls for its entity, with no state
tide_e2e_test(gpu_rolls) # ...and in frags and verts: the frame's number and the seed their draws send, alike for a frame's draws
tide_e2e_test(views)
tide_e2e_test(editor_views)
# Compile the private native-preview entry point as C on both native and web
# test hosts. The editor itself dynamically loads this ABI only on desktop.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/preview-include/tide")
configure_file("${PROJECT_SOURCE_DIR}/tools/tide-editor/preview_api.h"
    "${CMAKE_CURRENT_BINARY_DIR}/preview-include/tide/editor_preview.h" COPYONLY)
foreach(placement match local)
    set(preview_dir "${CMAKE_CURRENT_BINARY_DIR}/preview-${placement}")
    set(preview_flags "")
    set(preview_local 0)
    if(placement STREQUAL "local")
        set(preview_flags --editor-local)
        set(preview_local 1)
    endif()
    add_custom_command(OUTPUT "${preview_dir}/game.c" "${preview_dir}/game.h"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${preview_dir}"
        COMMAND "${TIDEC_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/editor_views.tide"
            --editor-prefab Preview ${preview_flags} --no-line --name game -o "${preview_dir}"
        DEPENDS ${TIDEC_DEPENDS} "e2e/editor_views.tide" VERBATIM)
    add_executable(tidec_editor_preview_${placement} "${preview_dir}/game.c"
        editor_preview_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_compile_definitions(tidec_editor_preview_${placement} PRIVATE EDITOR_LOCAL=${preview_local})
    target_include_directories(tidec_editor_preview_${placement} PRIVATE "${preview_dir}"
        "${CMAKE_CURRENT_BINARY_DIR}/preview-include" "${PROJECT_SOURCE_DIR}/platform/include" "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(tidec_editor_preview_${placement} PRIVATE tide)
    add_test(NAME tidec_editor_preview_${placement} COMMAND tidec_editor_preview_${placement})
endforeach()
tide_e2e_test(shapes) # The shapes with smooth edges: each Draw call, the general Shape, and target receivers
tide_e2e_test(gpu_functions) # Owned callable captures, named stage bindings and heterogeneous SDF instances.
tide_e2e_test(mut_callables) # Mutable callable arguments forward storage through dispatch and captured helpers.
tide_e2e_test(draw_settings) # Blend, tint, transform, clip and cameras: around blocks, listed with commas, and set
tide_e2e_test(draw_depth) # ...and what 3D meshes do with depth: the setting that's in a game's C only when it names it
tide_e2e_test(draw_cull) # ...and which side of their triangles draws, with the instances a transform mirrors told apart
tide_e2e_test(draw_frustum) # Conservative per-instance frustum culling, explicit bounds and operand effects
tide_e2e_test(draw_stencil) # ...and stencil: Draw's, for 2D and 3D draws alike, said to a renderer once before the draws it's for
tide_e2e_test(wrap) # What a uv past a texture's edges reads: a mesh draw's wrap after its filter, and a frag's at Sample
tide_e2e_test(mipmaps) # Filter.Trilinear, wherever a filter is said: what a draw records, and how a frag reads between a texture's smaller images
tide_e2e_test(assets) # Files a game is built with, read by name: each type, globs, a grid drawn and sampled, match code
tide_e2e_test(assets_plain) # ...and the same game with none: its world is the same bytes
tide_e2e_test(asset_textures) # Texture assets, and [Filter] and [Wrap] on a texture's declaration
tide_e2e_test(sprites) # Draw.Sprite: a texture upright over a box in one call, each of its forms
tide_e2e_test(asset_borrows) # Synchronous helpers borrow resource asset lists/maps without retaining the pack
tide_e2e_test(asset_borrows_unused) # Collection signatures alone need their C types, without enabling asset loading/audio
tide_e2e_test(clip_values) # Scalar Clip values need their C type without assets or playback.
tide_e2e_test(audio_listener_only) # Audio.SetListener with nothing to play still has the frame's audio list's C type
tide_e2e_test(asset_psds) # Photoshop's saved composite images, packed as Texture and Grid2<Color> assets
tide_e2e_test(asset_images) # JPEG, TGA and BMP imports through the same pixel/texture assets
tide_e2e_test(asset_audio_formats) # MP3/FLAC imports as PCM clips, preserving raw byte assets
tide_e2e_test(asset_defaults) # A field's default that names an asset: the match's text and bytes, this machine's images and clips
tide_e2e_test(asset_meshes) # Mesh assets: glTF and OBJ files as a vertex struct of the game's, drawn and read by the match
tide_e2e_test(lod) # Levels of detail: [LOD] mesh assets, what each draw picks, Draw.LOD and Draw.LODCamera
tide_e2e_test(skinned) # Checked catalogs, contracts, CPU pose edits, GLB decoding and match snapshots
tide_e2e_test(animation_imports) # Cleaned exporter names, selected clips and combined source catalogs
tide_e2e_test(animation_lookup) # Bool/out catalogs, failure defaults, evaluation order and owned outputs
tide_e2e_test(skinned_rest) # Zero-clip models, default-pose values and omitted-pose draw forms
tide_e2e_test(skinned_instanced) # Per-item poses, rest instances, shader fields, capture and batching
tide_e2e_test(skinned_scratch) # Owned model structs reclaim temporary lists after each entity and frame
tide_e2e_test(skinned_draw_borrow) # Draw-only borrows avoid owned copies, preserving operand snapshots and captured palettes
tide_e2e_test(skinned_attribute_limit SOURCES shaders/skinned_attribute_limit.tide) # Atlas bases reuse a spare int lane; full layouts keep individual palettes
tide_e2e_test(fbx_assets) # ASCII/binary FBX assets, checked catalogs, CPU poses and match snapshots
target_compile_definitions(tidec_e2e_fbx_assets PRIVATE FBX_HASH=936010611238560858ull)
tide_e2e_test(skinned_defaults) # Model defaults retain dependency identity while unrelated visual assets reload
tide_e2e_test(animations) # Animation-only assets, inferred members, dynamic catalogs and persistent ownership
if(TIDE_NODE)
    add_test(NAME tidec_animations_identity
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/animations_identity.mjs"
            "${TIDEC_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/animations-identity" "${PROJECT_SOURCE_DIR}")
    set_tests_properties(tidec_animations_identity PROPERTIES TIMEOUT 120)
    add_test(NAME tidec_skinned_default_identity
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/skinned_default_identity.mjs"
            "${TIDEC_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/skinned-default-identity" "${PROJECT_SOURCE_DIR}")
    set_tests_properties(tidec_skinned_default_identity PROPERTIES TIMEOUT 120)
endif()
target_compile_definitions(tidec_e2e_skinned PRIVATE SKINNED_HASH=13181359130345510980ull)
tide_e2e_test(audio) # Clip assets, and Audio.Play from views, local handlers and the functions they call
tide_e2e_test(sound) # Sound fields the frame names to the mixer while they play, and the listener
tide_e2e_test(buses) # A game's [Bus] enum, a Sound's bus and Audio.SetVolume
tide_e2e_test(effects) # Audio declarations: code the audio thread runs, with state and knobs, on a Sound and a bus
# Its echo keeps a delay line of 384 KB, which nothing may copy: a function of the generated C that had one on
# its stack, on the audio thread's, doesn't build
target_compile_options(tidec_e2e_effects PRIVATE -Wframe-larger-than=65536)
tide_e2e_test(textures) # Texture as the one type a draw and a Sample take: grids of colors and of floats as textures, and the sampling a Texture carries
tide_e2e_test(texture_compression)
tide_e2e_test(stages) # Enum phases, local offsets and whole-phase ordering in every schedule
tide_e2e_test(render_targets) # Target receivers, explicit Texture sources and distributed rendering phases
tide_e2e_test(render_lifecycle) # Shared target copies, resizing and explicit disposal from local code
tide_e2e_test(render_depth) # What a target holds, said when it's made, and a target's depth as frags and verts read it
tide_e2e_test(render_colors) # Targets of several color images, a format each, and frags that write them all
tide_e2e_test(render_samples) # A target's samples, said by name where it's made and kept in its id through a reload
tide_e2e_test(cubemaps) # Cubemaps: an asset's six files and a cube a view draws into a face at a time, read by a direction
tide_e2e_test(ownership) # Views `with ownership` and `without ownership`: this machine's player's entities, and the others
tide_e2e_test(ownership_events) # Event-time ownership, lifecycle/availability, replay and recipient privacy
tide_e2e_test(conditional)
tide_e2e_test(switch_values)
tide_e2e_test(input_bounds)
tide_e2e_test(structs)
tide_e2e_test(methods)
tide_e2e_test(overloads) # Default values, named arguments, and functions, methods and systems that share a name
tide_e2e_test(overloads_extern SOURCES e2e/overloads_extern.tide e2e/overloads_extern_c.c) # ...and C functions, which have neither
# virtual and override: a package's overrides of the game's code, and the game's of the package's. What only
# replaced code spawned is in no archetype, so the system that needs it matches nothing
tide_e2e_test(overrides SOURCES e2e/overrides/game.tide PACKAGES Expansion e2e/overrides/Expansion/expansion.tide
    WARNINGS "system 'Haunt' never runs")
tide_e2e_test(singleton_params) # Explicit singleton values, caller capabilities and local-field sidecars
tide_e2e_test(operators)
tide_e2e_test(events)
tide_e2e_test(events_lookup) # Verified event target rows and fallback after structural changes
tide_e2e_test(events_batch) # Queue-page runs preserve recording order, filters, defaults and C observers
tide_e2e_test(events_runs) # Runs of events taken in their own loop: found, moved, other and gone targets, against C
tide_e2e_test(commands)
# Commands in channels, unordered ones, and a command of a file's bytes, in chunks: on a slow network too
tide_e2e_test(commands_channels)
set_tests_properties(tidec_e2e_commands_channels PROPERTIES TIMEOUT 300)
add_test(NAME tidec_command_schema
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}"
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/command_schema"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/command_schema.cmake")
add_test(NAME tidec_map_layout
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}"
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/map_layout"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/map_layout.cmake")
add_test(NAME tidec_raw_texture_layout
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}"
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/raw_texture_layout"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/raw_texture_layout.cmake")
add_test(NAME tidec_task_layout
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}"
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/task_layout"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/task_layout.cmake")
tide_e2e_test(local)
# `local` fields of the match's components and singletons: this machine's, outside the match
tide_e2e_test(local_fields WARNINGS "'box' is declared mut but never written")
tide_e2e_test(local_fields_plain) # ...whose match is the same without them
tide_e2e_test(local_fields_net) # ...and what a client's wrong predictions do to them, on a network
tide_e2e_test(local_events) # Local handlers of the match's events: heard once, whatever a client predicted
tide_e2e_test(changed_events) # Changed<Health.value>: a field that changed over a tick, heard by local handlers
tide_e2e_test(visibility_lifecycle) # Local availability differs from genuine match lifecycle
tide_e2e_test(local_events_same) # Events only local code handles: the match they cost nothing,
tide_e2e_test(local_events_plain) # ...which is the same without them
tide_e2e_test(verified_views) # `verified` parameters: what views read of the world the server confirmed
tide_e2e_test(verified_heap) # ...its text, lists, maps, sets and grids, from its own heap
tide_e2e_test(view_queries) # Queries in views and local handlers: blended, `verified`, or as an event's tick left them
tide_e2e_test(view_bound_query) # ...and a view whose frag reads what it doesn't take, its parameters grown after its query was bound
tide_e2e_test(enums)
tide_e2e_test(scenes)
tide_e2e_test(scenes_local)
tide_e2e_test(scenes_fallback)
tide_e2e_test(scene_filters) # with scene, without scene: systems, views and handlers by an entity's scene
tide_e2e_test(gui)
tide_e2e_test(gui_custom)
tide_e2e_test(gui_entered) # TextField, IntField and FloatField with `out entered`: whether Enter ended the typing
tide_e2e_test(actions)
tide_e2e_test(tasks)
tide_e2e_test(tasks_local)
tide_e2e_test(local_workers)
tide_e2e_test(task_handles LAYOUT)
tide_e2e_test(task_visibility)
tide_e2e_test(worker_handles)
if(TIDE_PLATFORM)
    target_link_libraries(tidec_e2e_task_handles PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_task_handles PRIVATE TIDE_TASK_MIGRATION=1)
endif()
tide_e2e_test(worker_values)
tide_e2e_test(worker_grids)
if(TIDE_PLATFORM)
    target_link_libraries(tidec_e2e_local_workers PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_local_workers PRIVATE TIDE_WORKER_THREADS=1)
    target_link_libraries(tidec_e2e_worker_values PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_worker_values PRIVATE TIDE_WORKER_THREADS=1)
    target_link_libraries(tidec_e2e_worker_grids PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_worker_grids PRIVATE TIDE_WORKER_THREADS=1)
endif()
tide_e2e_test(tasks_flow)
tide_e2e_test(loops)
tide_e2e_test(constant_flow) # Known bool conditions agree for returns, switch exits, loops and out assignment
tide_e2e_test(strings)
# char: literals, a text's characters by index and by foreach, switch, keys, where they're kept, inputs and commands
tide_e2e_test(chars)
target_compile_definitions(tidec_e2e_chars PRIVATE CHARS_HASH=5269360396618934956ull)
tide_e2e_test(show_values)
tide_e2e_test(text_fields)
tide_e2e_test(lists)
tide_e2e_test(lists_held) # Lists in structs, and what stands in for lists inside lists
tide_e2e_test(lists_big SOURCES e2e/lists_big.tide e2e/lists_big_c.c) # Lists in chunks, and C reading them
tide_e2e_test(lists_resize) # Resize: a list sized in one call, a few elements or chunks of them
tide_e2e_test(lists_out SOURCES e2e/lists_out.tide e2e/lists_out_c.c) # `out List<T>`: lists C makes
tide_e2e_test(resident SOURCES e2e/resident.tide e2e/resident_c.c) # A match singleton's Resident field: a C library's state as the world's pages
tide_e2e_test(out_params SOURCES e2e/out_params.tide e2e/out_params_c.c) # `out` parameters and arguments, as C#'s
tide_e2e_test(json) # Json.Read<T> and Json.Write, and text as bytes, pieces and numbers
target_compile_definitions(tidec_e2e_json PRIVATE JSON_HASH=13621850945442854179ull)
tide_e2e_test(optional_fields) # T? fields: in structs, components, singletons, scenes, events, commands and the input
target_compile_definitions(tidec_e2e_optional_fields PRIVATE OPTIONAL_FIELDS_HASH=10352090099506241445ull)
tide_e2e_test(grids SOURCES e2e/grids.tide e2e/grids_c.c) # Grids
tide_e2e_test(parallel) # Parallel loops over grids and lists
tide_e2e_test(parallel_list_batch) # Independent output lanes preserve exact reductions, calls and snapshots
tide_e2e_test(window_indices) # Task-local windows with global positions, offsets and partial writes
tide_e2e_test(parallel_work) # Unknown nested loop work must still select the worker pool
# These share the same behavioral check, with a different nested-loop form.
foreach(variant IN ITEMS condition step block constant)
    set(target tidec_e2e_parallel_work_${variant})
    tide_add_game(${target} SOURCES e2e/parallel_work_${variant}.tide NAME game
        HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/parallel_work_test.c")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    if(variant STREQUAL "constant")
        target_compile_definitions(${target} PRIVATE EXPECTED_ROUNDS=0u EXPECTED_TOTAL=45)
    endif()
    add_test(NAME ${target} COMMAND ${target})
endforeach()
tide_e2e_test(mesh SOURCES e2e/mesh.tide e2e/mesh_c.c) # Meshes, grids as textures, and C that draws
tide_e2e_test(mesh3d) # 3D meshes, their cameras, and instances
tide_e2e_test(draw_apis SOURCES e2e/draw_apis.tide e2e/draw_apis_c.c) # Separate drawing APIs and coordinates
tide_e2e_test(measure_text) # Draw.MeasureText: on the screen, under a camera and in a render target
tide_e2e_test(text) # Draw.Text at an anchor and in a weight, and measuring in one
tide_e2e_test(shaders) # frag and vert: what a mesh draw through them records
tide_e2e_test(instancing) # Instanced: lists of transforms and ordinary instance structs
tide_e2e_test(draw_values) # A draw's values: a struct after its vert and frag, sent once for the draw
tide_e2e_test(draw_helpers) # Shaded helpers bind only explicit parameters, forwarding attached local fields
tide_e2e_test(crossplatform_text)
tide_e2e_test(devices)
tide_e2e_test(pause)
tide_e2e_test(claims) # Widgets a view draws itself: what's typed, and claiming the pointer and the keyboard
tide_e2e_test(cursor) # Cursor.Lock, asked each frame, and Cursor.locked, which the window says
tide_e2e_test(frame_time) # Frame.dt: the frame's length a host gives, read by local code with a match and with none
tide_e2e_test(session_main) # Session.Start() with no scene is Main, and Application.title the title setting
tide_e2e_test(net)
tide_e2e_test(view_time)
tide_e2e_test(net_no_input)
tide_e2e_test(net_ended)
tide_e2e_test(net_join_big) # A world that takes longer to send than the ticks kept
tide_e2e_test(guess) # Other players' inputs, guessed: their last ones for a while, then let go, but for [Absolute] fields
tide_e2e_test(predict) # Per-tick custom guesses and immediate action resets
tide_e2e_test(predict_fields) # Attribute-only input: no custom method or repair path
tide_e2e_test(predict_method) # Method-only input: no attribute defaults
tide_e2e_test(seen) # How late other players see what one does: from a key read to another's view
tide_e2e_test(random_net) # Generators through prediction and rollback, and the seed a server picks for its match
tide_e2e_test(blend)
tide_e2e_test(view_corrections) # Tagged presentation fields: correction accumulation, rotation and private lifetime
tide_e2e_test(big_world)
tide_e2e_test(crlf) # Windows line endings, kept by .gitattributes
tide_e2e_test(directives) # #if and the rest: what other versions of tide read is left out
# #define and #undef, and the build's symbols: --define's, DEBUG, and PACKAGE_<NAME> for each of its packages
tide_e2e_test(defines SOURCES e2e/defines/game.tide PACKAGES Extra e2e/defines/Extra/extra.tide DEFINES FAST WIDE)
target_compile_definitions(tidec_e2e_defines PRIVATE DEFINES_DEBUG=$<IF:$<CONFIG:Debug>,1,0>)
tide_e2e_test(multi_file SOURCES e2e/multi_file/a_physics.tide e2e/multi_file/b_main.tide
    e2e/multi_file/c_combat.tide e2e/multi_file/d_items.tide)
tide_e2e_test(namespace_blocks SOURCES e2e/namespace_blocks/a_game.tide e2e/namespace_blocks/b_more.tide
    e2e/namespace_blocks/c_tools.tide) # `namespace Name { ... }`: several in a file, one opened twice, blocks in blocks
# What a game declares itself comes before what a namespace it imports has by that name
tide_e2e_test(using_own SOURCES e2e/using_own/a_lib.tide e2e/using_own/a_other.tide e2e/using_own/b_game.tide
    e2e/using_own/c_inner.tide)
tide_e2e_test(type_aliases SOURCES e2e/type_aliases/a_aliases.tide e2e/type_aliases/b_game.tide
    e2e/type_aliases/c_consumer.tide e2e/type_aliases/z_types.tide) # Closed aliases retain canonical values and declaration-site name binding
tide_e2e_test(alias_assets SOURCES e2e/alias_assets.tide
    PACKAGES AliasKit e2e/alias_assets/AliasKit/kit.tide) # Mesh assets and checked references through closed aliases
add_test(NAME tidec_alias_canonical_array
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/alias_canonical_array"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/alias_canonical_array.cmake")
tide_e2e_test(extern SOURCES e2e/extern.tide e2e/extern_noise.tide e2e/extern_c.c) # C functions, from the game's C file
tide_e2e_test(extern_system SOURCES e2e/extern_system.tide e2e/extern_system_c.c) # Systems written in C, and their rows
if(TIDE_PLATFORM) # ...on the platform layer's threads, where there are some
    target_link_libraries(tidec_e2e_extern_system PRIVATE tide_platform)
    target_compile_definitions(tidec_e2e_extern_system PRIVATE TIDE_EXTERN_SYSTEM_THREADS=1)
endif()
tide_e2e_test(cpp SOURCES e2e/cpp.tide e2e/cpp_code.cpp) # ...and from its C++ file, with no C++ runtime
tide_e2e_test(fails SOURCES e2e/fails.tide e2e/fails_c.c) # Errors as values, and T?
tide_e2e_test(constants SOURCES e2e/constants/a_tuning.tide e2e/constants/b_main.tide)
tide_e2e_test(settings SOURCES e2e/settings.tide e2e/settings_more.tide) # Several blocks, in two files, are one list
tide_e2e_test(profile SOURCES e2e/profile.tide e2e/profile_combat.tide) # What ticks and frames say to a host's profiler
tide_e2e_test(handover)
tide_e2e_test(players) # 270 loopback players, including indices beyond a byte
tide_e2e_test(players_ceiling) # Exact shared ceiling, sparse highest-slot connection and recovery
tide_e2e_test(delta)
if(TIDE_PLATFORM)
    # grid.Decode: the engine's PNG reader against the platform's writer, and a PNG decoded in the match
    tide_e2e_test(decode)
    target_include_directories(tidec_e2e_decode PRIVATE "${PROJECT_SOURCE_DIR}/platform/src")
    target_link_libraries(tidec_e2e_decode PRIVATE tide_platform)
    # await Web.Fetch, against a server on this machine (platform/tests/fetch_routes.mjs): natively through
    # the platform's HTTP client, and in a page through the page's fetch, served by the test's own server
    if(TIDE_WEB)
        tide_add_game(tidec_e2e_fetch_web SOURCES e2e/fetch.tide NAME game HOST "${CMAKE_CURRENT_SOURCE_DIR}/e2e/fetch_web.c" WINDOW)
        target_include_directories(tidec_e2e_fetch_web PRIVATE "${PROJECT_SOURCE_DIR}/tests")
        target_link_libraries(tidec_e2e_fetch_web PRIVATE tide_platform)
        tide_web_page(tidec_e2e_fetch_web SHELL "${PROJECT_SOURCE_DIR}/platform/web/test_shell.html")
        add_test(NAME tidec_e2e_fetch_web
            COMMAND "${TIDE_NODE}" "${PROJECT_SOURCE_DIR}/cmake/run_web_served_test.mjs" "${TIDE_BROWSER}"
                "$<TARGET_FILE_DIR:tidec_e2e_fetch_web>/tidec_e2e_fetch_web.html"
                "${CMAKE_CURRENT_BINARY_DIR}/tidec_e2e_fetch_web-browser" "backend=webgl"
                --routes "${PROJECT_SOURCE_DIR}/platform/tests/fetch_routes.mjs")
        # The guard around the runner's own 100 seconds and what it asks the page after them (cmake/WebTest.cmake)
        set_tests_properties(tidec_e2e_fetch_web PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" TIMEOUT 150)
    elseif(NOT TIDE_ANDROID AND TIDE_NODE)
        tide_add_game(tidec_e2e_fetch SOURCES e2e/fetch.tide NAME game
            HOST "${PROJECT_SOURCE_DIR}/tests/test_main.c" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/fetch_test.c")
        target_include_directories(tidec_e2e_fetch PRIVATE "${PROJECT_SOURCE_DIR}/tests")
        target_link_libraries(tidec_e2e_fetch PRIVATE tide_platform)
        add_test(NAME tidec_e2e_fetch
            COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/e2e/fetch.mjs" "$<TARGET_FILE:tidec_e2e_fetch>")
        set_tests_properties(tidec_e2e_fetch PROPERTIES TIMEOUT 120)
    endif()
endif()

# An endless chain of events stops the program, naming the event: here past
# 1000 deep, so it stops quickly.
tide_add_game(tidec_e2e_chain_endless SOURCES e2e/chain_endless.tide NAME game
    HOST "${CMAKE_CURRENT_SOURCE_DIR}/e2e/chain_endless_main.c")
target_compile_definitions(tidec_e2e_chain_endless PRIVATE TIDE_MAX_CHAIN=1000u)
add_test(NAME tidec_e2e_chain_endless COMMAND tidec_e2e_chain_endless)
set_tests_properties(tidec_e2e_chain_endless PROPERTIES TIMEOUT 30
    PASS_REGULAR_EXPRESSION "an endless chain of events: P[io]ng is 1000 deep in one tick")

# Safety tests: each safety/<name>.tide is a game of Tide alone that tries to
# make the generated C or the engine do something undefined: an index anywhere,
# a handle to nothing, numbers at their limits, a comparison that lies. No Tide
# code can corrupt memory, so each ends in a defined
# way: it runs to the end having said what its first lines expect, or stops
# with the engine's own message (safety/expect_safety.cmake). They mean the
# most under sanitizers (-DTIDE_SANITIZE=ON), where a read or write out of
# bounds or an undefined operation is a report, and a report fails the test;
# without them they still pin what each case gives, the same on every platform.
# safety/host.c runs them as a session would: snapshots that share the world's
# pages, views between ticks, the world as bytes and back; on the platform
# layer's threads where there's one.
file(GLOB TIDE_SAFETY_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/safety/*.tide")
string(REPLACE ";" "|" TIDE_SAFETY_EMULATOR "${CMAKE_CROSSCOMPILING_EMULATOR}")
foreach(source IN LISTS TIDE_SAFETY_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    set(target tidec_safety_${name})
    tide_add_game(${target} SOURCES safety/${name}.tide NAME game
        HOST "${CMAKE_CURRENT_SOURCE_DIR}/safety/host.c")
    if(TIDE_PLATFORM)
        target_link_libraries(${target} PRIVATE tide_platform)
        target_compile_definitions(${target} PRIVATE TIDE_SAFETY_THREADS=1)
        if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
            target_compile_definitions(${target} PRIVATE TIDE_SAFETY_RENDERER=1)
        endif()
    endif()
    add_test(NAME ${target}
        COMMAND "${CMAKE_COMMAND}"
            "-DRUN=$<TARGET_FILE:${target}>"
            "-DEMULATOR=${TIDE_SAFETY_EMULATOR}"
            "-DSOURCE=${source}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/safety/expect_safety.cmake")
    set_tests_properties(${target} PROPERTIES TIMEOUT 300 LABELS safety)
    # One that draws (`// draws` among its first lines) goes through the
    # renderer too, where tests have a window and a GPU for it (label `gpu`)
    file(STRINGS "${source}" draws LIMIT_COUNT 1 REGEX "^// draws")
    if(draws AND TIDE_PLATFORM AND NOT TIDE_WEB AND NOT TIDE_ANDROID)
        add_test(NAME ${target}_gpu
            COMMAND "${CMAKE_COMMAND}"
                "-DRUN=$<TARGET_FILE:${target}>"
                "-DEMULATOR=${TIDE_SAFETY_EMULATOR}"
                "-DSOURCE=${source}"
                -DGPU=gpu
                -P "${CMAKE_CURRENT_SOURCE_DIR}/safety/expect_safety.cmake")
        set_tests_properties(${target}_gpu PROPERTIES TIMEOUT 300 LABELS "safety;gpu")
    endif()
endforeach()

# Error tests: each errors/<name>.tide must fail to compile, and tidec's output
# must contain the text on the file's first line, written as `// expect: <text>`.
# A folder errors/<name>/ is one program of several files.
file(GLOB TIDE_ERROR_TESTS CONFIGURE_DEPENDS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/errors/*")
foreach(source IN LISTS TIDE_ERROR_TESTS)
    if(NOT IS_DIRECTORY "${source}" AND NOT source MATCHES "\\.tide$")
        continue()
    endif()
    get_filename_component(name "${source}" NAME_WE)
    add_test(NAME tidec_error_${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTIDEC=${TIDEC_PATH}"
            "-DSOURCE=${source}"
            "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/errors"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_error.cmake")
endforeach()

# Warning tests: each warnings/<name>.tide must compile, with as many warnings
# as its first line says, `// warnings: <count>`, and each text on its
# `// expect: <text>` lines somewhere in what tidec says: warnings and notes.
file(GLOB TIDE_WARNING_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/warnings/*.tide")
foreach(source IN LISTS TIDE_WARNING_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    add_test(NAME tidec_warning_${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTIDEC=${TIDEC_PATH}"
            "-DSOURCE=${source}"
            "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/warnings"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_warnings.cmake")
endforeach()

# Scale test: a big game of many namespaces, made up at test time, which tidec
# has to compile within a generous time (see scale/scale.cmake): codegen's
# time must not grow with the game's size squared.
add_test(NAME tidec_scale
    COMMAND "${CMAKE_COMMAND}"
        "-DTIDEC=${TIDEC_PATH}"
        "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/scale"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/scale/scale.cmake")
set_tests_properties(tidec_scale PROPERTIES TIMEOUT 600)

# Restrict lane parameters and sparse presence/fallback columns together must
# fit WebAssembly's declaration limit, even for a game at the component limit.
if(TIDE_NODE)
    set(TIDE_ROWS_PARAMETER_ARGS "${TIDEC_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/rows-parameters" "${PROJECT_SOURCE_DIR}")
    if(TIDE_WEB)
        list(APPEND TIDE_ROWS_PARAMETER_ARGS "${TIDE_CLANG}" "${CMAKE_C_COMPILER_TARGET}" "${CMAKE_SYSROOT}")
    endif()
    add_test(NAME tidec_rows_parameters
        COMMAND "${TIDE_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/rows/parameters.mjs" ${TIDE_ROWS_PARAMETER_ARGS})
    set_tests_properties(tidec_rows_parameters PROPERTIES TIMEOUT 120)
endif()

# Schedule tests: `tidec --schedule` on schedule/<name>.tide must print
# schedule/<name>.txt exactly.
file(GLOB TIDE_SCHEDULE_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/schedule/*.tide")
foreach(source IN LISTS TIDE_SCHEDULE_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    get_filename_component(dir "${source}" DIRECTORY)
    add_test(NAME tidec_schedule_${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTIDEC=${TIDEC_PATH}"
            "-DSOURCE=${source}"
            "-DEXPECTED=${dir}/${name}.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_schedule.cmake")
endforeach()

add_test(NAME tidec_unity_folder_names
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/unity_folder_names"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/unity_folder_names.cmake")

add_test(NAME tidec_declarations_only
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/declarations_only"
            "-DCC=${TIDE_CLANG}" "-DTARGET=${CMAKE_C_COMPILER_TARGET}" "-DSYSROOT=${CMAKE_SYSROOT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/declarations_only.cmake")
add_test(NAME tidec_prefab_error_once
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/prefab_error_once"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/prefab_error_once.cmake")
add_test(NAME tidec_scene_startup
    COMMAND "${CMAKE_COMMAND}" "-DTIDEC=${TIDEC_PATH}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/scene_startup"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/scene_startup.cmake")

# Outline tests: `tidec --outline` on outline/<name>.tide, run in its folder so the paths it prints are the
# file's name, must print outline/<name>.json exactly.
file(GLOB TIDE_OUTLINE_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/outline/*.tide")
foreach(source IN LISTS TIDE_OUTLINE_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    get_filename_component(dir "${source}" DIRECTORY)
    add_test(NAME tidec_outline_${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTIDEC=${TIDEC_PATH}"
            "-DSOURCE=${source}"
            "-DEXPECTED=${dir}/${name}.json"
            "-DOPTION=--outline"
            "-DRELATIVE=ON"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_schedule.cmake")
endforeach()

# Shader tests: `tidec --shaders` on shaders/<name>.tide must print
# shaders/<name>.txt exactly: the GLSL and WGSL of each shader its mesh draw
# calls draw through.
file(GLOB TIDE_SHADER_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.tide")
foreach(source IN LISTS TIDE_SHADER_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    get_filename_component(dir "${source}" DIRECTORY)
    add_test(NAME tidec_shaders_${name}
        COMMAND "${CMAKE_COMMAND}"
            "-DTIDEC=${TIDEC_PATH}"
            "-DSOURCE=${source}"
            "-DEXPECTED=${dir}/${name}.txt"
            "-DOPTION=--shaders"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_schedule.cmake")
endforeach()

# ...and their SPIR-V, Vulkan's, which spirv-val takes, where there's one
# (cmake/VulkanTests.cmake).
if(TIDE_SPIRV_VAL)
    foreach(source IN LISTS TIDE_SHADER_TESTS)
        get_filename_component(name "${source}" NAME_WE)
        add_test(NAME tidec_spirv_${name}
            COMMAND "${CMAKE_COMMAND}"
                "-DTIDEC=${TIDEC_PATH}"
                "-DSOURCE=${source}"
                "-DSPIRV_VAL=${TIDE_SPIRV_VAL}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/spirv/${name}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_spirv.cmake")
    endforeach()
endif()

# ...and their HLSL, Unity's, which fxc compiles, where there's one
# (cmake/HlslTests.cmake).
if(TIDE_FXC)
    foreach(source IN LISTS TIDE_SHADER_TESTS)
        get_filename_component(name "${source}" NAME_WE)
        add_test(NAME tidec_hlsl_${name}
            COMMAND "${CMAKE_COMMAND}"
                "-DTIDEC=${TIDEC_PATH}"
                "-DSOURCE=${source}"
                "-DFXC=${TIDE_FXC}"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/hlsl/${name}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_hlsl.cmake")
    endforeach()
endif()

# The mesh files' reader on its own (src/mesh_files.c): the numbers it reads,
# what it refuses, and that a file cut or wrong anywhere is never read past
# its bytes. The language server reads a game's files at every keystroke.
if(TARGET tidec_core)
    add_executable(tidec_asset_usage_tests asset_usage_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_asset_usage_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(tidec_asset_usage_tests PRIVATE ASSET_USAGE_ROOT="${PROJECT_SOURCE_DIR}"
        ASSET_USAGE_WORK="${CMAKE_CURRENT_BINARY_DIR}/asset_usage")
    target_link_libraries(tidec_asset_usage_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_asset_usage COMMAND tidec_asset_usage_tests)
    add_executable(tidec_lexer_tests lexer_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_lexer_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(tidec_lexer_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_lexer COMMAND tidec_lexer_tests)
    add_executable(tidec_fbx_files_tests fbx_files_test.c fbx_parity_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_fbx_files_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(tidec_fbx_files_tests PRIVATE FBX_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/e2e/fbx"
        FBX_WORK="${CMAKE_CURRENT_BINARY_DIR}/fbx_cache")
    target_link_libraries(tidec_fbx_files_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_fbx_files COMMAND tidec_fbx_files_tests)
    set_tests_properties(tidec_fbx_files PROPERTIES TIMEOUT 180)
    add_executable(tidec_asset_cache_tests asset_cache_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_asset_cache_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(tidec_asset_cache_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_asset_cache COMMAND tidec_asset_cache_tests)
    add_executable(tidec_mesh_files_tests mesh_files_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_mesh_files_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(tidec_mesh_files_tests PRIVATE tide tidec_core) # The engine for the harness's clock
    add_test(NAME tidec_mesh_files COMMAND tidec_mesh_files_tests)
    # A mesh's levels of detail on their own (src/mesh_lod.c)
    add_executable(tidec_mesh_lod_tests mesh_lod_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_mesh_lod_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(tidec_mesh_lod_tests PRIVATE MESH_LOD_WORK="${CMAKE_CURRENT_BINARY_DIR}/mesh_lod")
    target_link_libraries(tidec_mesh_lod_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_mesh_lod COMMAND tidec_mesh_lod_tests)
    add_executable(tidec_model_metadata_tests test_model_metadata.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_model_metadata_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(tidec_model_metadata_tests PRIVATE MODEL_METADATA_WORK="${CMAKE_CURRENT_BINARY_DIR}/model_metadata")
    target_link_libraries(tidec_model_metadata_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_model_metadata COMMAND tidec_model_metadata_tests)
    add_executable(tidec_asset_files_tests asset_files_test.c mesh_variants_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_asset_files_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(tidec_asset_files_tests PRIVATE ASSET_FILES_WORK="${CMAKE_CURRENT_BINARY_DIR}/asset_files")
    target_link_libraries(tidec_asset_files_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_asset_files COMMAND tidec_asset_files_tests)
    add_executable(tidec_pack_fingerprint_tests pack_fingerprint_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_pack_fingerprint_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(tidec_pack_fingerprint_tests PRIVATE
        PACK_FINGERPRINT_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/e2e/skinned/model.glb")
    target_link_libraries(tidec_pack_fingerprint_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_pack_fingerprint COMMAND tidec_pack_fingerprint_tests)
    add_executable(tidec_psd_files_tests psd_files_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_psd_files_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(tidec_psd_files_tests PRIVATE PSD_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/e2e/assets/psds")
    target_link_libraries(tidec_psd_files_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_psd_files COMMAND tidec_psd_files_tests)
    add_executable(tidec_image_files_tests image_files_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_image_files_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(tidec_image_files_tests PRIVATE IMAGE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/e2e/assets/images")
    target_link_libraries(tidec_image_files_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_image_files COMMAND tidec_image_files_tests)
    add_executable(tidec_texture_compress_tests texture_compress_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_texture_compress_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(tidec_texture_compress_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_texture_compress COMMAND tidec_texture_compress_tests)
    add_executable(tidec_audio_files_tests audio_files_test.c "${PROJECT_SOURCE_DIR}/tests/test_main.c")
    target_include_directories(tidec_audio_files_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_compile_definitions(tidec_audio_files_tests PRIVATE AUDIO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/e2e/audio_import"
        AUDIO_WORK="${CMAKE_CURRENT_BINARY_DIR}/audio_files")
    target_link_libraries(tidec_audio_files_tests PRIVATE tide tidec_core)
    add_test(NAME tidec_audio_files COMMAND tidec_audio_files_tests)
endif()

# Debuggers stopping in Tide code (debugger/): a game built as tide's debug builds are, with its natvis in its
# .pdb, under Windows' own debugger engine on Windows, and LLDB with the game's LLDB views where there's an LLDB.
# Unoptimized whatever the build type, as tide builds a game to debug it.
if(NOT TIDE_WEB AND NOT TIDE_ANDROID)
    tide_add_game(tidec_debugger_fixture VISUALIZERS SOURCES debugger/fixture.tide NAME game
        HOST "${CMAKE_CURRENT_SOURCE_DIR}/debugger/fixture_main.c")
    target_compile_options(tidec_debugger_fixture PRIVATE -O0 -g)
    if(WIN32 AND NOT MINGW)
        add_executable(tide_dbgeng_drive debugger/dbgeng_drive.c)
        target_link_libraries(tide_dbgeng_drive PRIVATE tide_flags)
        add_test(NAME tidec_debugger_natvis
            COMMAND "${CMAKE_COMMAND}" "-DDRIVE=$<TARGET_FILE:tide_dbgeng_drive>"
                "-DPROGRAM=$<TARGET_FILE:tidec_debugger_fixture>" "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/debugger/fixture.tide"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/debugger_natvis" -P "${CMAKE_CURRENT_SOURCE_DIR}/debugger/natvis_test.cmake")
    endif()
    # LLDB on Linux, where CI installs it beside clang; elsewhere with -DTIDE_LLDB=<lldb>. Windows' LLDB can't
    # read the heaps' thread-locals, which the natvis test covers there.
    if(NOT WIN32 AND NOT APPLE AND NOT DEFINED TIDE_LLDB)
        string(REGEX MATCH "^[0-9]+" clang_major "${CMAKE_C_COMPILER_VERSION}")
        find_program(TIDE_LLDB NAMES lldb lldb-${clang_major} HINTS "$ENV{LLVM_ROOT}/bin")
    endif()
    if(TIDE_LLDB)
        add_test(NAME tidec_debugger_lldb
            COMMAND "${CMAKE_COMMAND}" "-DLLDB=${TIDE_LLDB}" "-DPROGRAM=$<TARGET_FILE:tidec_debugger_fixture>"
                "-DVIEWS=${CMAKE_CURRENT_BINARY_DIR}/tide/tidec_debugger_fixture/game_lldb.py"
                "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/debugger/fixture.tide"
                "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/debugger_lldb" -P "${CMAKE_CURRENT_SOURCE_DIR}/debugger/lldb_test.cmake")
    endif()
endif()
