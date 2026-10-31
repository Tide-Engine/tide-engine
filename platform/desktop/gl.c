// OpenGL's functions on desktop, looked up in the window's context (see
// src/gl.h): the system's library only names the oldest of them.

#include "gl.h"

#include <stdio.h>
#include <string.h>

tide_gl_functions tide_gl;

bool tide_gl_load(tide_gl_proc (*find)(const char *name))
{
    bool found = true;
#define LOAD(name)                                                                                                     \
    tide_gl.name = (__typeof__(tide_gl.name))find("gl" #name);                                                         \
    if (!tide_gl.name) {                                                                                               \
        fprintf(stderr, "tide: this machine's OpenGL has no gl" #name "\n");                                           \
        found = false;                                                                                                 \
    }
#define LOAD_V(name, parameters, arguments) LOAD(name)
#define LOAD_R(type, name, parameters, arguments) LOAD(name)
    TIDE_GL_FUNCTIONS(LOAD_V, LOAD_R)
#undef LOAD_V
#undef LOAD_R
#undef LOAD
    // Missing timing support leaves its stats line unavailable; drawing can
    // still use all the functions above.
#define LOAD_TIMER(name, parameters, arguments) tide_gl.name = (__typeof__(tide_gl.name))find("gl" #name);
    TIDE_GL_TIMER_FUNCTIONS(LOAD_TIMER, LOAD_TIMER)
#undef LOAD_TIMER
    // Separate bindings are optional: a function address alone does not say
    // whether this context supports the function.
#define LOAD_BINDING_V(name, parameters, arguments) tide_gl.name = (__typeof__(tide_gl.name))find("gl" #name);
#define LOAD_BINDING_R(type, name, parameters, arguments) LOAD_BINDING_V(name, parameters, arguments)
    TIDE_GL_VERTEX_BINDING_FUNCTIONS(LOAD_BINDING_V, LOAD_BINDING_R)
#undef LOAD_BINDING_V
#undef LOAD_BINDING_R
    tide_gl.vertex_binding = false;
    if (found && tide_gl.BindVertexBuffer && tide_gl.VertexAttribFormat && tide_gl.VertexAttribIFormat &&
        tide_gl.VertexAttribBinding && tide_gl.VertexBindingDivisor) {
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        tide_gl.vertex_binding = major > 4 || (major == 4 && minor >= 3);
        if (!tide_gl.vertex_binding && tide_gl.GetStringi) {
            GLint count = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &count);
            for (GLint i = 0; i < count; i++) {
                const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
                if (extension && !strcmp(extension, "GL_ARB_vertex_attrib_binding")) {
                    tide_gl.vertex_binding = true;
                    break;
                }
            }
        }
    }
    return found;
}
