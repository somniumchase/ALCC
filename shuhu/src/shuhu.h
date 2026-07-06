#pragma once

#include <glad/glad.h>

// ---------------------------------------------------------
// 1. 核心三角形着色器 (加入子步插值与亚像素抖动)
// ---------------------------------------------------------
const GLchar* vertex_shader_source = R"(
#version 460 core
layout (location = 0) in vec3 a_pos;

uniform float u_time;
uniform float u_dt;          // 距离上一帧经过的时间
uniform int u_substeps;      // 子步数量(实例数)
uniform vec2 u_resolution;   // 屏幕分辨率，用于计算像素大小

out vec3 v_pos;

// 简单的伪随机函数，用于生成抖动
float random(vec2 st) {
    return fract(sin(dot(st.xy, vec2(12.9898,78.233))) * 43758.5453123);
}

void main() {
    // 1. 时间插值：填补上一帧到当前帧之间的运动轨迹缝隙
    // gl_InstanceID 为 0 时最旧，为 u_substeps-1 时最新
    float fraction = float(gl_InstanceID) / max(float(u_substeps - 1), 1.0);
    float t = u_time - u_dt * (1.0 - fraction);

    // 2. 基础运动轨迹计算
    vec4 pos = vec4(a_pos.x * sin(t + a_pos.y), a_pos.y * cos(t + a_pos.x), a_pos.z, 1.0);

    // 3. 亚像素级抖动 (Sub-pixel Jitter) 实现免费抗锯齿
    // 给每个重叠的三角形一个不到 1 像素的随机偏移，叠加后边缘会变得柔和
    float jitter_x = (random(vec2(gl_InstanceID, t)) - 0.5) / u_resolution.x;
    float jitter_y = (random(vec2(t, gl_InstanceID)) - 0.5) / u_resolution.y;
    pos.x += jitter_x;
    pos.y += jitter_y;

    gl_Position = pos;
    v_pos = a_pos;
}
)";

const GLchar* fragment_shader_source = R"(
#version 460 core
out vec4 frag_color;
in vec3 v_pos;
uniform float u_time;
uniform vec2 u_resolution;

#define AA 2

//2d rotation matrix
vec2 r(vec2 v,float t){float s=sin(t),c=cos(t);return mat2(c,-s,s,c)*v;}

// ACES tonemap: https://www.shadertoy.com/view/Xc3yzM
vec3 a(vec3 c)
{
mat3 m1=mat3(0.59719,0.07600,0.02840,0.35458,0.90834,0.13383,0.04823,0.01566,0.83777);
mat3 m2=mat3(1.60475,-0.10208,-0.00327,-0.53108,1.10813,-0.07276,-0.07367,-0.00605,1.07602);
vec3 v=m1*c,a=v*(v+0.0245786)-0.000090537,b=v*(0.983729*v+0.4329510)+0.238081;
return m2*(a/b);
}

//Xor's Dot Noise: https://www.shadertoy.com/view/wfsyRX
float no(vec3 p)
{
    const float PHI = 1.618033988;
    const mat3 GOLD = mat3(
    -0.571464913, +0.814921382, +0.096597072,
    -0.278044873, -0.303026659, +0.911518454,
    +0.772087367, +0.494042493, +0.399753815);
    return dot(cos(GOLD * p), sin(PHI * p * GOLD));
}

void main() {
    vec3 color = vec3(0.0);
    for(int m = 0; m < AA; m++)
    for(int n = 0; n < AA; n++)
    {
        vec2 off = (vec2(float(m), float(n)) + 0.5) / float(AA) - 0.5;
        vec2 u = gl_FragCoord.xy + off;
        float i=0.,s,t=u_time;
        vec3 p=vec3(0.),l=vec3(0.),b=vec3(0.),d=vec3(0.);
        p.z=-1.0-.5*sin(t*.1);
        d=normalize(vec3(2.*u-u_resolution.xy,u_resolution.y));
        for(i=0.;i<10.;i++){
            b=p;
            b.xy=r(sin(b.xy*.25),t*.5+b.z*2.);
            s=.001+abs(no(b*20.)/20.-no(b))*.7;
            s=max(s,0.-length(p.xy));
            s+=abs(p.y*.2+sin(p.z*2.+(abs(p.x)*.5)))*.5;
            p+=d*s;
            l+=(1.+1.5*sin(i+length(p.xy*.1)+2.+vec3(3,1.5,.5)))/s;
        }
        color+=a(l*l/5e2);
    }
    frag_color = vec4(color / float(AA*AA), 0.25);
}
)";

// ---------------------------------------------------------
// 2. 屏幕后处理与残影控制着色器
// ---------------------------------------------------------
const GLchar* quad_vertex_shader_source = R"(
#version 460 core
layout (location = 0) in vec2 a_pos;
layout (location = 1) in vec2 a_texCoords;
out vec2 v_texCoords;
void main() {
    gl_Position = vec4(a_pos.x, a_pos.y, 0.0, 1.0);
    v_texCoords = a_texCoords;
}
)";

const GLchar* screen_fragment_shader_source = R"(
#version 460 core
out vec4 frag_color;
in vec2 v_texCoords;
uniform sampler2D screen_texture;
void main() {
    frag_color = texture(screen_texture, v_texCoords);
}
)";

const GLchar* fade_fragment_shader_source = R"(
#version 460 core
out vec4 frag_color;
void main() {
    // 稍微提高了一点点衰减速度，防止8位色彩精度截断导致极弱的残影永不消失
    frag_color = vec4(0.0, 0.0, 0.0, 0.08);
}
)";
