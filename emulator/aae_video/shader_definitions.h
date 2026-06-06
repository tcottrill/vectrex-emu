#pragma once

// Old Style Blur Shader
// VS
const char* vertText = R"glsl(
#version 330 compatibility

out vec2 TexCoord;

void main()
{
    // Pass through the texture coordinate
    TexCoord = gl_MultiTexCoord0.xy;

    // Use ftransform() for compatibility with fixed-function pipeline
    gl_Position = ftransform();
}
)glsl";

// FS
const char* fragText = R"glsl(
#version 330 compatibility

uniform sampler2D colorMap;
uniform float width;
uniform float height;

in vec2 TexCoord;
out vec4 FragColor; // Modern GLSL uses custom output variables instead of gl_FragColor

void main()
{
    float step_w = 1.0 / width;
    float step_h = 1.0 / height;

    vec2 offset[9] = vec2[](
        vec2(-step_w, -step_h), vec2(0.0, -step_h), vec2(step_w, -step_h),
        vec2(-step_w, 0.0),     vec2(0.0, 0.0),     vec2(step_w, 0.0),
        vec2(-step_w, step_h),  vec2(0.0, step_h),  vec2(step_w, step_h)
    );

    float kernel[9] = float[](
        1.0/17.0, 2.0/17.0, 1.0/17.0,
        2.0/17.0, 4.0/17.0, 2.0/17.0,
        1.0/17.0, 2.0/17.0, 1.0/17.0
    );

    vec4 sum = vec4(0.0);
    for (int i = 0; i < 9; i++)
    {
        // texture2D is deprecated in modern GLSL, replaced by texture()
        vec4 tmp = texture(colorMap, TexCoord + offset[i]);
        sum += tmp * kernel[i];
    }

    FragColor = sum * 1.12;
}
)glsl";

// Multitexturing Combining Shaders
// VS
const char* texvertText = R"glsl(
#version 330 compatibility

out vec2 TexCoord0;
out vec2 TexCoord1;

void main(void)
{
    // Pass texture coordinates for multiple units
    TexCoord0 = gl_MultiTexCoord0.xy;
    TexCoord1 = gl_MultiTexCoord1.xy;

    // Use fixed-function transform
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)glsl";

// FS
const char* texfragText = R"glsl(
#version 330 compatibility

uniform int usefb;
uniform int useglow;
uniform float glowamt;
uniform int brighten; // Note: Maybe can be removed

uniform sampler2D mytex2; // Vectors
uniform sampler2D mytex3; // Glow
uniform sampler2D mytex4; // Feedback

in vec2 TexCoord0;
out vec4 FragColor;

void main(void)
{
    float bval = 1.0;
    vec2 uv = TexCoord0;

    vec4 texval2 = texture(mytex2, uv); // Vectors
    vec4 texval3 = texture(mytex3, uv); // Glow
    vec4 texval4 = texture(mytex4, uv); // Feedback
   
    vec4 result = texval2 * bval;

    if (useglow > 0) result += texval3 * glowamt;
    if (usefb > 0)   result += texval4 * 0.25;

    FragColor = result;
}
)glsl";

// ---------------------------------------------------------
// Basic Texture Shader (Replaces fixed-function texturing)
// ---------------------------------------------------------
const char* basicTexVert = R"glsl(
#version 330 compatibility

out vec2 TexCoord;
out vec4 VertColor;

void main()
{
    TexCoord = gl_MultiTexCoord0.xy;
    VertColor = gl_Color;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)glsl";

const char* basicTexFrag = R"glsl(
#version 330 compatibility

uniform sampler2D u_texture;

in vec2 TexCoord;
in vec4 VertColor;
out vec4 FragColor;

void main()
{
    FragColor = texture(u_texture, TexCoord) * VertColor;
}
)glsl";

// ---------------------------------------------------------
// Basic Color Shader (Replaces fixed-function colored quads)
// ---------------------------------------------------------
const char* basicColorVert = R"glsl(
#version 330 compatibility

out vec4 VertColor;

void main()
{
    VertColor = gl_Color;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)glsl";

const char* basicColorFrag = R"glsl(
#version 330 compatibility

in vec4 VertColor;
out vec4 FragColor;

void main()
{
    FragColor = VertColor;
}
)glsl";


// ---------------------------------------------------------
// Scanline Multiply Shader
// Tiles a scanline texture over a fullscreen quad using
// GL_REPEAT-style UV math, then multiplies it against the
// existing framebuffer contents via blending (DST_COLOR, ZERO).
// Replaces the fixed-function glBegin/glEnd scanline overlay.
// ---------------------------------------------------------
const char* scanlineMultiplyVert = R"glsl(
#version 330 compatibility

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inTex;

uniform mat4 u_projection;

out vec2 TexCoord;

void main()
{
    gl_Position = u_projection * vec4(inPos, 0.0, 1.0);
    TexCoord    = inTex;
}
)glsl";

const char* scanlineMultiplyFrag = R"glsl(
#version 330 compatibility

uniform sampler2D u_scanTex;

in vec2 TexCoord;
out vec4 FragColor;

void main()
{
    FragColor = texture(u_scanTex, TexCoord);
}
)glsl";


// ---------------------------------------------------------
// Star Point Shader (VBO/VAO point rendering for GUI stars)
// ---------------------------------------------------------
const char* starPointVert = R"glsl(
#version 330 compatibility

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

out vec4 VertColor;

void main()
{
    VertColor = aColor;
    gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 0.0, 1.0);
}
)glsl";

const char* starPointFrag = R"glsl(
#version 330 compatibility

in vec4 VertColor;
out vec4 FragColor;

void main()
{
    FragColor = VertColor;
}
)glsl";






// New 330 Blur Shaders
/*
const char* vertText = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 modelViewProjection;

void main()
{
    TexCoord = aTexCoord;
    gl_Position = modelViewProjection * vec4(aPos, 1.0);
}
)glsl";

const char* fragText = R"glsl(
#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D colorMap;
uniform float width;
uniform float height;

void main()
{
    float step_w = 1.0 / width;
    float step_h = 1.0 / height;

    vec2 offset[9] = vec2[](
        vec2(-step_w, -step_h), vec2(0.0, -step_h), vec2(step_w, -step_h),
        vec2(-step_w, 0.0),     vec2(0.0, 0.0),     vec2(step_w, 0.0),
        vec2(-step_w, step_h),  vec2(0.0, step_h),  vec2(step_w, step_h)
    );

    float kernel[9] = float[](
        1.0/17.0, 2.0/17.0, 1.0/17.0,
        2.0/17.0, 4.0/17.0, 2.0/17.0,
        1.0/17.0, 2.0/17.0, 1.0/17.0
    );

    vec4 sum = vec4(0.0);
    for (int i = 0; i < 9; i++)
    {
        vec4 tmp = texture(colorMap, TexCoord + offset[i]);
        sum += tmp * kernel[i];
    }

    FragColor = sum * 1.12;
}
)glsl";

*/