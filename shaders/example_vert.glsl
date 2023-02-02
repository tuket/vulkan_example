#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_tc;

layout(location = 0) out vec2 v_tc;

void main()
{
    gl_Position = vec4(a_pos, 0, 1);
    v_tc = a_tc;
}