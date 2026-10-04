# Runs tidels over stdin and stdout, the way an editor does, with framed
# messages, and checks its replies and its exit code.
#
# Its input is a file, so every message has arrived before the first is
# handled, and tidels reads ahead (compiler/lsp/main.c): the two changes are
# analysed once, as the second left the text, and the request that the
# message after it cancels isn't worked out.
#
# cmake -DTIDELS=<tidels> -DOUT=<scratch dir> -P stdio_test.cmake

# One variable per message: a CMake list would split them at semicolons.
set(message1 [[{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}]])
set(message2 [[{"jsonrpc":"2.0","method":"initialized","params":{}}]])
set(message3 [[{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///a.tide","languageId":"tide","version":1,"text":"component Body { float2 position; }\nsystem Main() { Spawn(Body); }\nsystem Move(mut Body body)\n{\n    body.\n}\n"}}}]])
set(message4 [[{"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":"file:///a.tide"},"position":{"line":4,"character":9}}}]])
set(message5 [[{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///a.tide","version":2},"contentChanges":[{"text":"component Body { float2 position; }\nscene Main { }\nsystem Move(mut Body body)\n{\n    body.pos = 1;\n}\n"}]}}]])
set(message6 [[{"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":"file:///a.tide","version":3},"contentChanges":[{"text":"component Body { float2 position; }\nscene Main { }\nsystem Move(mut Body body)\n{\n    body.spin = 1;\n}\n"}]}}]])
set(message7 [[{"jsonrpc":"2.0","id":4,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///a.tide"},"position":{"line":4,"character":6}}}]])
set(message8 [[{"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":4}}]])
set(message9 [[{"jsonrpc":"2.0","id":3,"method":"shutdown"}]])
set(message10 [[{"jsonrpc":"2.0","method":"exit"}]])

set(input "")
foreach(name message1 message2 message3 message4 message5 message6 message7 message8 message9 message10)
    string(LENGTH "${${name}}" length)
    string(APPEND input "Content-Length: ${length}\n\n${${name}}")
endforeach()
# Headers end with \r\n on every platform. file(WRITE) would translate newlines
# on Windows only.
file(CONFIGURE OUTPUT "${OUT}/stdio_input.txt" CONTENT "${input}" @ONLY NEWLINE_STYLE CRLF)

execute_process(
    COMMAND "${TIDELS}"
    INPUT_FILE "${OUT}/stdio_input.txt"
    OUTPUT_VARIABLE output
    RESULT_VARIABLE result
    TIMEOUT 20)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "tidels exited with ${result}\n${output}")
endif()
foreach(expected "\"capabilities\"" "publishDiagnostics" "\"label\":\"position\"" "\"id\":3,\"result\":null"
        "Body has no field 'spin'" "\"id\":4,\"error\":{\"code\":-32800")
    string(FIND "${output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "tidels didn't reply with ${expected}\n${output}")
    endif()
endforeach()
string(FIND "${output}" "invalid JSON" invalid)
if(NOT invalid EQUAL -1)
    message(FATAL_ERROR "tidels couldn't read a message\n${output}")
endif()
string(FIND "${output}" "Body has no field 'pos'" first_change)
if(NOT first_change EQUAL -1)
    message(FATAL_ERROR "tidels analysed a change that another had already replaced\n${output}")
endif()
