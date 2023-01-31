#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;

layout(location = 0) out vec4 v_color;

void main()
{
    gl_Position = vec4(a_pos, 0, 1);
    v_color = a_color;
}