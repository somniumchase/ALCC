#define GLFW_INCLUDE_NONE

#include "shuhu.h"

#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <windows.h>

#include <random>

/*********************************************/

static GLuint VAO, VBO;
static GLuint quadVAO, quadVBO;
static GLuint fbo, fbo_texture;

static float speed = 1.0f;
static int substeps = 32;
static bool is_swap_interval = true;

static float vertices[] = {
  -0.5f, -0.5f, 0.0f,
   0.5f, -0.5f, 0.0f,
   0.0f,  0.5f, 0.0f,
};

static float quad_vertices[] = {
  -1.0f,  1.0f,  0.0f, 1.0f,
  -1.0f, -1.0f,  0.0f, 0.0f,
   1.0f, -1.0f,  1.0f, 0.0f,

  -1.0f,  1.0f,  0.0f, 1.0f,
   1.0f, -1.0f,  1.0f, 0.0f,
   1.0f,  1.0f,  1.0f, 1.0f
};

static void resize_fbo(int width, int height) {
  if (fbo_texture == 0 || width == 0 || height == 0) return;
  glBindTexture(GL_TEXTURE_2D, fbo_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

static auto glfw_framebuffer_size_callback = [](GLFWwindow* window, int width, int height) {
  glViewport(0, 0, width, height);
  resize_fbo(width, height);
};

static auto glfw_input_callback = [](GLFWwindow* window, int key, int scancode, int action, int mods) {
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
     glfwSetWindowShouldClose(window, true);
  }
};

static auto get_random_float = [gen = std::mt19937(std::random_device {}())]
(float min, float max) mutable -> float {
  std::uniform_real_distribution<float> dis(min, max);
  return dis(gen);
};

#include <iostream>
#include <vector>

GLuint create_shader_program(const GLchar* vs_src, const GLchar* fs_src) {
  auto check_compile_errors = [](GLuint shader, std::string type) {
    GLint success;
    GLchar infoLog[1024];
    if (type != "PROGRAM") {
      glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
      if (!success) {
        glGetShaderInfoLog(shader, 1024, NULL, infoLog);
        std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
      }
    } else {
      glGetProgramiv(shader, GL_LINK_STATUS, &success);
      if (!success) {
        glGetProgramInfoLog(shader, 1024, NULL, infoLog);
        std::cerr << "ERROR::PROGRAM_LINKING_ERROR of type: " << type << "\n" << infoLog << "\n -- --------------------------------------------------- -- " << std::endl;
      }
    }
  };

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vs_src, nullptr);
  glCompileShader(vs);
  check_compile_errors(vs, "VERTEX");

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fs_src, nullptr);
  glCompileShader(fs);
  check_compile_errors(fs, "FRAGMENT");

  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  check_compile_errors(program, "PROGRAM");

  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // ImGui setup
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  io.IniFilename = NULL;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 21.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
  ImGui::StyleColorsDark();

  float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());

  GLFWwindow* window = glfwCreateWindow(800 * main_scale, 600 * main_scale, "shuhu", nullptr, nullptr);
  glfwMakeContextCurrent(window);
  gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
  glfwSwapInterval(is_swap_interval);
  glfwSetFramebufferSizeCallback(window, glfw_framebuffer_size_callback);
  glfwSetKeyCallback(window, glfw_input_callback);

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 460");

  GLuint shader_program = create_shader_program(vertex_shader_source, fragment_shader_source);
  GLuint screen_program = create_shader_program(quad_vertex_shader_source, screen_fragment_shader_source);
  GLuint fade_program = create_shader_program(quad_vertex_shader_source, fade_fragment_shader_source);

  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);
  glBindVertexArray(VAO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(0);

  glGenVertexArrays(1, &quadVAO);
  glGenBuffers(1, &quadVBO);
  glBindVertexArray(quadVAO);
  glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glGenTextures(1, &fbo_texture);

  int fb_width, fb_height;
  glfwGetFramebufferSize(window, &fb_width, &fb_height);
  glBindTexture(GL_TEXTURE_2D, fbo_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fb_width, fb_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_texture, 0);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // 性能优化：明确禁用不需要的深度测试
  glDisable(GL_DEPTH_TEST);

  double last_time = glfwGetTime();
  double last_frame_time = glfwGetTime(); // 用于计算帧间距


  while (!glfwWindowShouldClose(window)) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("shuhu - 控制台");
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "帧率：%.2f fps", io.Framerate);
    ImGui::Separator();

    ImGui::BulletText("OpenGL %s", glGetString(GL_VERSION));
    ImGui::BulletText("GLSL %s", glGetString(GL_SHADING_LANGUAGE_VERSION));
    ImGui::Separator();

    ImGui::Checkbox("垂直同步", &is_swap_interval);
    if (is_swap_interval)
      glfwSwapInterval(1);
    else
      glfwSwapInterval(0);

    ImGui::SliderInt("平滑步数", &substeps, 1, 100);
    ImGui::SliderFloat("时间速率", &speed, 1.f, 10.f);

    ImGui::End();
    ImGui::Render();

    double current_time = glfwGetTime();
    double delta_time = current_time - last_frame_time;
    last_frame_time = current_time;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    // a. FBO 衰减层
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(fade_program);
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // b. 绘制当前帧几何体 (使用实例化优化)
    glUseProgram(shader_program);
    glUniform1f(glGetUniformLocation(shader_program, "u_time"), (float)current_time * speed);
    glUniform1f(glGetUniformLocation(shader_program, "u_dt"), (float)delta_time * speed);

    // 设置子步数量以填补高速运动造成的断层视觉差
    glUniform1i(glGetUniformLocation(shader_program, "u_substeps"), substeps);

    // 传入分辨率供抗锯齿抖动计算使用
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    glUniform2f(glGetUniformLocation(shader_program, "u_resolution"), (float)fb_width, (float)fb_height);

    glBindVertexArray(VAO);
    // 关键优化：一次性绘制多个实例，填补上一帧到这一帧之间的轨迹
    glDrawArraysInstanced(GL_TRIANGLES, 0, 3, substeps);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 屏幕呈现
    glDisable(GL_BLEND);
    glUseProgram(screen_program);
    glBindVertexArray(quadVAO);
    glBindTexture(GL_TEXTURE_2D, fbo_texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);
  glDeleteVertexArrays(1, &quadVAO);
  glDeleteBuffers(1, &quadVBO);
  glDeleteProgram(shader_program);
  glDeleteProgram(screen_program);
  glDeleteProgram(fade_program);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &fbo_texture);

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();
}
