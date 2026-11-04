# Turns a file into C: its bytes as the array NAME, in OUTPUT. For what the
# platform layer carries inside it, like its font (platform/CMakeLists.txt).
#
#   cmake -DINPUT=<file> -DOUTPUT=<file.c> -DNAME=<name> [-DAPPEND=<file.inc>] -P cmake/embed.cmake
#
# APPEND's C goes after the array, in the same object: what names the array
# and has to come with it wherever it goes (the bold font's tide_font).
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR size "${digits} / 2")
# 16 bytes a line
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "    \\1\n"
    bytes "${bytes}")
get_filename_component(file "${INPUT}" NAME)
set(after "")
if(APPEND)
    file(READ "${APPEND}" after)
endif()
file(WRITE "${OUTPUT}" "// ${file}, ${size} bytes: made by cmake/embed.cmake\nconst unsigned char ${NAME}[${size}] = {\n${bytes}\n};\nconst unsigned int ${NAME}_size = ${size}u;\n${after}")
