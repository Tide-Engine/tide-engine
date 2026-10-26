// Runs a program under Windows' own debugger engine (dbgeng.dll, part of
// Windows, the engine of WinDbg and cdb) and gives it the debugger commands a
// file lists, a line each, printing what it says: natvis_test.cmake checks it.
// `g` goes on until the next stop; a step (`p`, `t`) waits for its end too.
//
//   tide_dbgeng_drive <commands> <program> [arguments...]
#define CINTERFACE
#define INITGUID
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dbgeng.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT(STDAPICALLTYPE *debug_create)(REFIID, PVOID *);

static ULONG STDMETHODCALLTYPE said_ref(IDebugOutputCallbacks *self)
{
    (void)self;
    return 1;
}

static HRESULT STDMETHODCALLTYPE said_query(IDebugOutputCallbacks *self, REFIID iid, PVOID *out)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDebugOutputCallbacks)) {
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE said(IDebugOutputCallbacks *self, ULONG mask, PCSTR text)
{
    (void)self;
    (void)mask;
    fputs(text, stdout);
    return S_OK;
}

static IDebugOutputCallbacksVtbl said_vtbl = {said_query, said_ref, said_ref, said};
static IDebugOutputCallbacks said_to = {&said_vtbl};

// Waits for the program to stop, or end, for at most a minute.
static void wait_stop(IDebugControl4 *control)
{
    const HRESULT hr = control->lpVtbl->WaitForEvent(control, 0, 60000);
    ULONG status = DEBUG_STATUS_NO_DEBUGGEE;
    control->lpVtbl->GetExecutionStatus(control, &status);
    if (status == DEBUG_STATUS_NO_DEBUGGEE) printf("(the program ended)\n");
    else if (hr == S_FALSE) printf("(no stop within a minute)\n");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: tide_dbgeng_drive <commands> <program> [arguments...]\n");
        return 2;
    }
    HMODULE engine = LoadLibraryExA("dbgeng.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    debug_create create = engine ? (debug_create)(void (*)(void))GetProcAddress(engine, "DebugCreate") : NULL;
    IDebugClient5 *client = NULL;
    IDebugControl4 *control = NULL;
    if (!create || FAILED(create(&IID_IDebugClient5, (void **)&client))
        || FAILED(client->lpVtbl->QueryInterface(client, &IID_IDebugControl4, (void **)&control))) {
        fprintf(stderr, "tide_dbgeng_drive: Windows' debugger engine (dbgeng.dll) didn't start\n");
        return 1;
    }
    client->lpVtbl->SetOutputCallbacks(client, &said_to);
    control->lpVtbl->AddEngineOptions(control, DEBUG_ENGOPT_INITIAL_BREAK);
    char command_line[8192] = "";
    for (int i = 2; i < argc; i++) {
        const size_t at = strlen(command_line);
        snprintf(command_line + at, sizeof command_line - at, "\"%s\" ", argv[i]);
    }
    if (FAILED(client->lpVtbl->CreateProcess(client, 0, command_line, DEBUG_ONLY_THIS_PROCESS))) {
        fprintf(stderr, "tide_dbgeng_drive: can't start %s\n", argv[2]);
        return 1;
    }
    control->lpVtbl->WaitForEvent(control, 0, 60000); // The initial break
    FILE *commands = fopen(argv[1], "r");
    if (!commands) {
        fprintf(stderr, "tide_dbgeng_drive: can't read %s\n", argv[1]);
        client->lpVtbl->TerminateProcesses(client);
        return 1;
    }
    char line[4096];
    while (fgets(line, sizeof line, commands)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) continue;
        printf("> %s\n", line);
        if (strcmp(line, "g") == 0) {
            control->lpVtbl->SetExecutionStatus(control, DEBUG_STATUS_GO);
            wait_stop(control);
            continue;
        }
        control->lpVtbl->Execute(control, DEBUG_OUTCTL_THIS_CLIENT, line, DEBUG_EXECUTE_DEFAULT);
        ULONG status = DEBUG_STATUS_NO_DEBUGGEE;
        control->lpVtbl->GetExecutionStatus(control, &status);
        if (status != DEBUG_STATUS_BREAK && status != DEBUG_STATUS_NO_DEBUGGEE) wait_stop(control);
    }
    fclose(commands);
    client->lpVtbl->TerminateProcesses(client);
    client->lpVtbl->EndSession(client, DEBUG_END_PASSIVE);
    return 0;
}
