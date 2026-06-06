#include "gl_shader.h"
#include "sys_log.h"
#include "shader_definitions.h"

GLuint fragBlur = 0;
GLuint fragMulti = 0;
GLuint fragBasicTex = 0;   
GLuint fragBasicColor = 0; 
GLuint fragScanlineMultiply = 0;
GLuint fragStarPoint = 0;

// Write errors
static void write_shader_error(GLuint obj, const char* label, bool isProgram)
{
    GLint length = 0;
    if (isProgram)
        glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &length);
    else
        glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &length);

    if (length > 0) {
        char* message = (char*)malloc(length);
        if (isProgram)
            glGetProgramInfoLog(obj, length, nullptr, message);
        else
            glGetShaderInfoLog(obj, length, nullptr, message);
        LOG_INFO("Shader error in %s:\n%s", label, message);
        free(message);
    }
}

// Compile + link shader
static GLuint create_shader_program(const char* vertSrc, const char* fragSrc)
{
    GLint status;
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vertSrc, nullptr);
    glCompileShader(vert);
    glGetShaderiv(vert, GL_COMPILE_STATUS, &status);
    if (!status) write_shader_error(vert, "Vertex", false);

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fragSrc, nullptr);
    glCompileShader(frag);
    glGetShaderiv(frag, GL_COMPILE_STATUS, &status);
    if (!status) write_shader_error(frag, "Fragment", false);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) write_shader_error(prog, "Program", true);
    else LOG_INFO("Shader linked OK");

    glDeleteShader(vert);
    glDeleteShader(frag);

    return prog;
}

void bind_shader(GLuint program) {
    glUseProgram(program);
}

void unbind_shader() {
    glUseProgram(0);
}

void delete_shader(GLuint* program) {
    if (program && *program) {
        glDeleteProgram(*program);
        *program = 0;
    }
}

// Uniform helpers
static GLint get_uniform_loc(GLuint program, const char* name) {
    return glGetUniformLocation(program, name);
}

void set_uniform1i(GLuint program, const char* name, int value) {
    glUniform1i(get_uniform_loc(program, name), value);
}

void set_uniform1f(GLuint program, const char* name, float value) {
    glUniform1f(get_uniform_loc(program, name), value);
}

void set_uniform2f(GLuint program, const char* name, float x, float y) {
    glUniform2f(get_uniform_loc(program, name), x, y);
}

void set_uniform3f(GLuint program, const char* name, float x, float y, float z) {
    glUniform3f(get_uniform_loc(program, name), x, y, z);
}

void set_uniform4f(GLuint program, const char* name, float x, float y, float z, float w) {
    glUniform4f(get_uniform_loc(program, name), x, y, z, w);
}

void set_uniform_mat4f(GLuint program, const char* name, const float* matrix) {
    glUniformMatrix4fv(get_uniform_loc(program, name), 1, GL_FALSE, matrix);
}


int init_shader()
{
    LOG_INFO("Shader Init Start");
    fragBlur = create_shader_program(vertText, fragText);
    fragMulti = create_shader_program(texvertText, texfragText);

    // Compile and link the new basic shaders
    fragBasicTex = create_shader_program(basicTexVert, basicTexFrag);
    fragBasicColor = create_shader_program(basicColorVert, basicColorFrag);

    // Scanline multiply overlay
    fragScanlineMultiply = create_shader_program(scanlineMultiplyVert, scanlineMultiplyFrag);

    //Gui Stars Shader
    fragStarPoint = create_shader_program(starPointVert, starPointFrag);
    return 1;
}
