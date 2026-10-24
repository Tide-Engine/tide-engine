#pragma once

#include "document.h"
#include "sys.h"

typedef enum editor_play_state {
    EDITOR_PLAY_STOPPED = 0,
    EDITOR_PLAY_BUILDING,
    EDITOR_PLAY_RUNNING,
    EDITOR_PLAY_FINISHED,
    EDITOR_PLAY_FAILED
} editor_play_state;

typedef struct editor_play_options {
    const char *cli; // NULL: tide beside this editor.
    // Normal game arguments, terminated by NULL. Copied at Start. Hidden runs
    // require --frames, --capture or --script; NULL opens the owner's game window.
    const char *const *arguments;
} editor_play_options;

// Zero-initialize. All strings and process state belong to this helper, so a
// document refresh cannot change a build or a running game's captured source.
// Stop before replacing/closing the active document. The game runs independently
// of authoring refreshes; starting again requires Stop or a completed game.
typedef struct editor_play {
    editor_play_state state;
    sys_process *process;
    uint64_t revision;
    int exit_code;
    char message[4096];
    char *directory, *executable, *log;
    char *project, *source, *scene;
    char **arguments;
    int argument_count;
} editor_play;

bool editor_play_start(editor_play *p, const editor_document *d, const editor_play_options *options);
// Nonblocking at zero; a positive wait is capped at 50 ms. A successful build
// launches the normal game host, including its session, systems and all views.
void editor_play_poll(editor_play *p, int wait_ms);
// Stops the compiler/game tree and removes this play's private artifacts.
// Authored text, recovery, project sources and startup settings are untouched.
void editor_play_stop(editor_play *p);
void editor_play_close(editor_play *p);
