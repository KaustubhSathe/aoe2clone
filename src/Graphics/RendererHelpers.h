#pragma once

#include "../Core/Types.h"
#include <filesystem>
#include <vector>
#include <optional>
#include <glm/glm.hpp>
#include <glad/glad.h>

// OpenGL Shader Compilation
GLuint compile_shader(GLenum type, const char* source);
GLuint create_program(const char* vertexSource, const char* fragmentSource);

// WIC Texture Loading
std::vector<TextureFrame> load_frame_directory(const std::filesystem::path& assetDirectory);
std::optional<TextureFrame> load_frame_by_index(const std::filesystem::path& assetDirectory, int targetFrameIndex);

// Animation direction lookup
int facing_index_from_direction(const glm::vec2& direction, int frameCount);
int walk_direction_group_from_direction(const glm::vec2& direction);
int walk_animation_index(const glm::vec2& direction, int gaitFrame, int frameCount);
