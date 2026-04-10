#pragma once

#include "../Core/Types.h"
#include <filesystem>
#include <vector>
#include <optional>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// OpenGL Shader Compilation
std::string read_file_to_string(const std::filesystem::path& path);
GLuint compile_shader(GLenum type, const char* source);
GLuint create_program(const char* vertexSource, const char* fragmentSource);
GLuint create_program_from_files(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);

// WIC Texture Loading
std::vector<TextureFrame> load_frame_directory(const std::filesystem::path& assetDirectory);
std::optional<TextureFrame> load_frame_by_index(const std::filesystem::path& assetDirectory, int targetFrameIndex);
bool load_texture_from_png(const std::filesystem::path& imagePath, TextureFrame& outFrame, bool trimTransparentBounds = false);

// Cursor Creation
GLFWcursor* create_cursor_from_png(const std::filesystem::path& imagePath, int xhot, int yhot);

// Animation direction lookup
int facing_index_from_direction(const glm::vec2& direction, int frameCount);
int walk_direction_group_from_direction(const glm::vec2& direction);
int walk_animation_index(const glm::vec2& direction, int gaitFrame, int frameCount);
