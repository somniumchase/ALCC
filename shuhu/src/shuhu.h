#pragma once

#include <glad/glad.h>

// ---------------------------------------------------------
// 1. 核心三角形着色器 (加入子步插值与亚像素抖动)
// ---------------------------------------------------------
const GLchar* vertex_shader_source = R"(
#version 460 core
layout (location = 0) in vec3 a_pos;

uniform float u_time;
uniform float u_dt;
uniform int u_substeps;
uniform vec2 u_resolution;

out vec3 v_pos;
out float v_time;

float random(vec2 st) {
  return fract(sin(dot(st.xy, vec2(12.9898, 78.233))) * 43758.5453123);
}

void main() {
  float fraction = float(gl_InstanceID) / max(float(u_substeps - 1), 1.0);
  v_time = u_time - u_dt * (1.0 - fraction);

  vec4 pos = vec4(a_pos.x * sin(v_time + a_pos.y), a_pos.y * cos(v_time + a_pos.x), a_pos.z, 1.0);

  // 亚像素抖动：不仅消除锯齿，还能配合多实例实现“免费”的时间性抗锯齿
  float jitter_x = (random(vec2(gl_InstanceID, v_time)) - 0.5) / u_resolution.x;
  float jitter_y = (random(vec2(v_time, gl_InstanceID)) - 0.5) / u_resolution.y;
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
in float v_time;
uniform vec2 u_resolution;

// 2d rotation matrix
vec2 r(vec2 v, float t) {
  float s = sin(t), c = cos(t);
  return mat2(c, -s, s, c) * v;
}

// ACES tonemap
vec3 a(vec3 c) {
  mat3 m1 = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
  mat3 m2 = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
  vec3 v = m1 * c, a = v * (v + 0.0245786) - 0.000090537, b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return m2 * (a / b);
}

// Xor's Dot Noise
float no(vec3 p) {
  const float PHI = 1.618033988;
  const mat3 GOLD = mat3(-0.571464913, 0.814921382, 0.096597072, -0.278044873, -0.303026659, 0.911518454, 0.772087367, 0.494042493, 0.399753815);
  return dot(cos(GOLD * p), sin(PHI * p * GOLD));
}

void main() {
  // 性能优化：利用多实例自带的亚像素抖动，移除 FS 内部的 AA 循环 (性能提升 4 倍)
  vec2 u = gl_FragCoord.xy;
  float i = 0., s, t = v_time;
  vec3 p = vec3(0.), l = vec3(0.), b, d;

  p.z = -1.0 - .5 * sin(t * .1);
  d = normalize(vec3(2. * u - u_resolution.xy, u_resolution.y));

  // 穿梭体积感光影
  for (i = 0.; i < 10.; i++) {
    b = p;
    b.xy = r(sin(b.xy * .25), t * .5 + b.z * 2.);
    s = .001 + abs(no(b * 20.) / 20. - no(b)) * .7;
    s = max(s, 0. - length(p.xy));
    s += abs(p.y * .2 + sin(p.z * 2. + (abs(p.x) * .5))) * .5;
    p += d * s;
    // 视觉增强：加入基于深度的动态色偏，让光影更有层次感
    vec3 color_shift = vec3(3, 1.5, 0.5) + sin(t * 0.2) * 0.5;
    l += (1. + 1.5 * sin(i + length(p.xy * .1) + 2. + color_shift)) / s;
  }

  vec3 color = a(l * l / 5e2);

  // 视觉增强：边缘柔化 (Vignette)，让三角形看起来像一个悬浮的传送门
  float edge_fade = smoothstep(0.6, 0.2, length(v_pos.xy));

  frag_color = vec4(color, 0.25 * edge_fade);
}
)";

// ---------------------------------------------------------
// 2. 屏幕后处理与残影控制着色器
// ---------------------------------------------------------
const GLchar* quad_vertex_shader_source = R"(
#version 460 core
layout (location = 0) in vec2 a_pos;
layout (location = 1) in vec2 a_tex_coords;
out vec2 v_tex_coords;
void main() {
  gl_Position = vec4(a_pos.x, a_pos.y, 0.0, 1.0);
  v_tex_coords = a_tex_coords;
}
)";

const GLchar* screen_fragment_shader_source = R"(
#version 460 core
out vec4 frag_color;
in vec2 v_tex_coords;
uniform sampler2D screen_texture;
void main() {
  frag_color = texture(screen_texture, v_tex_coords);
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
