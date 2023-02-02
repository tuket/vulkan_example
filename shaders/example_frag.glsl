#version 450
#pragma shader_stage(fragment)

layout(location = 0) out vec4 o_color;

layout(location = 0) in vec2 v_tc;

layout(set = 0, binding = 0) uniform sampler2D u_colorTex; 

void main()
{
    o_color = texture(u_colorTex, v_tc);
}