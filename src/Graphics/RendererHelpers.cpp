#include "RendererHelpers.h"
#include "../Core/Constants.h"

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#endif

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"

// ============================================================================
// Shader helpers (cross-platform)
// ============================================================================

GLuint compile_shader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE)
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(static_cast<size_t>(std::max(logLength, 1)), '\0');
        if (logLength > 0)
        {
            glGetShaderInfoLog(shader, logLength, nullptr, infoLog.data());
        }
        std::cerr << "Shader compile failed: " << infoLog << std::endl;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint create_program(const char* vertexSource, const char* fragmentSource)
{
    const GLuint vertexShader = compile_shader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compile_shader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0)
        {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader != 0)
        {
            glDeleteShader(fragmentShader);
        }
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_FALSE)
    {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string infoLog(static_cast<size_t>(std::max(logLength, 1)), '\0');
        if (logLength > 0)
        {
            glGetProgramInfoLog(program, logLength, nullptr, infoLog.data());
        }
        std::cerr << "Program link failed: " << infoLog << std::endl;
        glDeleteProgram(program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

std::string read_file_to_string(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << path << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

#ifdef __EMSCRIPTEN__
    // Replace GLSL version for WebGL2 compatibility
    const std::string version330 = "#version 330 core";
    const std::string version300es = "#version 300 es";
    size_t pos = source.find(version330);
    if (pos != std::string::npos)
    {
        source.replace(pos, version330.length(), version300es);
    }
    // Add precision qualifier after version for fragment shaders
    if (path.extension() == ".fs")
    {
        pos = source.find('\n', pos);
        if (pos != std::string::npos)
        {
            source.insert(pos + 1, "precision mediump float;\n");
        }
    }
#endif

    return source;
}

GLuint create_program_from_files(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
{
    std::string vertexSource = read_file_to_string(vertexPath);
    std::string fragmentSource = read_file_to_string(fragmentPath);
    if (vertexSource.empty() || fragmentSource.empty())
    {
        std::cerr << "Shader source missing. Vertex: " << vertexPath << " Fragment: " << fragmentPath << std::endl;
        return 0;
    }
    return create_program(vertexSource.c_str(), fragmentSource.c_str());
}

// ============================================================================
// Texture loading — stb_image (cross-platform)
// ============================================================================

bool load_texture_from_png(
    const std::filesystem::path& imagePath,
    TextureFrame& outFrame,
    bool trimTransparentBounds)
{
    int width = 0, height = 0, channels = 0;
    // Request RGBA output
    unsigned char* pixels = stbi_load(imagePath.string().c_str(), &width, &height, &channels, 4);
    if (!pixels)
    {
        return false;
    }

    glGenTextures(1, &outFrame.texture);
    glBindTexture(GL_TEXTURE_2D, outFrame.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels);

    outFrame.width = static_cast<int>(width);
    outFrame.height = static_cast<int>(height);
    outFrame.uvMin = glm::vec2(0.0f, 0.0f);
    outFrame.uvMax = glm::vec2(1.0f, 1.0f);

    if (trimTransparentBounds && width > 0 && height > 0)
    {
        int minX = width;
        int minY = height;
        int maxX = 0;
        int maxY = 0;
        bool foundOpaquePixel = false;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const size_t pixelIndex = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
                const unsigned char red = pixels[pixelIndex + 0];
                const unsigned char green = pixels[pixelIndex + 1];
                const unsigned char blue = pixels[pixelIndex + 2];
                const unsigned char alpha = pixels[pixelIndex + 3];
                const bool isDebugRedBorder = (alpha > 8 && red == 255 && green == 0 && blue == 0);
                if (alpha > 8 && !isDebugRedBorder)
                {
                    foundOpaquePixel = true;
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
        }

        if (foundOpaquePixel)
        {
            const float fullWidth = static_cast<float>(width);
            const float fullHeight = static_cast<float>(height);
            outFrame.width = static_cast<int>(maxX - minX + 1);
            outFrame.height = static_cast<int>(maxY - minY + 1);
            outFrame.uvMin = glm::vec2(static_cast<float>(minX) / fullWidth, static_cast<float>(minY) / fullHeight);
            outFrame.uvMax = glm::vec2(static_cast<float>(maxX + 1) / fullWidth, static_cast<float>(maxY + 1) / fullHeight);
        }
    }

    stbi_image_free(pixels);
    return true;
}

std::vector<TextureFrame> load_frame_directory(const std::filesystem::path& assetDirectory)
{
    std::vector<std::filesystem::path> pngFiles;
    for (const auto& entry : std::filesystem::directory_iterator(assetDirectory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".png")
        {
            pngFiles.push_back(entry.path());
        }
    }

    std::sort(
        pngFiles.begin(),
        pngFiles.end(),
        [](const std::filesystem::path& left, const std::filesystem::path& right)
        {
            return left.filename().string() < right.filename().string();
        });

    if (pngFiles.empty())
    {
        return {};
    }

    std::vector<TextureFrame> frames;
    frames.reserve(pngFiles.size());

    for (const auto& pngFile : pngFiles)
    {
        const std::string fileName = pngFile.stem().string();
        const std::size_t separator = fileName.find_last_of('_');
        if (separator == std::string::npos)
        {
            continue;
        }

        TextureFrame frameData;
        frameData.frameIndex = std::stoi(fileName.substr(separator + 1));
        if (load_texture_from_png(pngFile, frameData))
        {
            frames.push_back(frameData);
        }
    }

    std::sort(
        frames.begin(),
        frames.end(),
        [](const TextureFrame& left, const TextureFrame& right)
        {
            return left.frameIndex < right.frameIndex;
        });

    if (frames.size() > 1 && frames.front().frameIndex == 0 && frames.back().frameIndex == static_cast<int>(frames.size()) - 1)
    {
        glDeleteTextures(1, &frames.back().texture);
        frames.pop_back();
    }

    return frames;
}

std::optional<TextureFrame> load_frame_by_index(const std::filesystem::path& assetDirectory, int targetFrameIndex)
{
    const std::vector<TextureFrame> frames = load_frame_directory(assetDirectory);
    for (const TextureFrame& frame : frames)
    {
        if (frame.frameIndex == targetFrameIndex)
        {
            return frame;
        }
    }

    for (const TextureFrame& frame : frames)
    {
        glDeleteTextures(1, &frame.texture);
    }
    return std::nullopt;
}

// ============================================================================
// Animation direction helpers (cross-platform)
// ============================================================================

int facing_index_from_direction(const glm::vec2& direction, int frameCount)
{
    if (frameCount <= 0) return 0;

    const glm::vec2 normalized = glm::length(direction) > 0.0001f ? glm::normalize(direction) : glm::vec2(1.0f, 0.0f);
    const float screenDegrees = glm::degrees(std::atan2(-normalized.y, normalized.x));
    float clockwiseDegrees = std::fmod(screenDegrees, 360.0f);
    if (clockwiseDegrees < 0.0f) clockwiseDegrees += 360.0f;

    const float step = 360.0f / static_cast<float>(frameCount);
    return static_cast<int>(std::round(clockwiseDegrees / step)) % frameCount;
}

int walk_direction_group_from_direction(const glm::vec2& direction)
{
    const glm::vec2 normalized = glm::length(direction) > 0.0001f ? glm::normalize(direction) : glm::vec2(1.0f, 0.0f);
    const float screenDegrees = glm::degrees(std::atan2(-normalized.y, normalized.x));
    float clockwiseDegrees = std::fmod(screenDegrees, 360.0f);
    if (clockwiseDegrees < 0.0f) clockwiseDegrees += 360.0f;

    const float step = 360.0f / static_cast<float>(WALK_DIRECTION_COUNT);
    return static_cast<int>(std::round(clockwiseDegrees / step)) % WALK_DIRECTION_COUNT;
}

int walk_animation_index(const glm::vec2& direction, int gaitFrame, int frameCount)
{
    if (frameCount <= 0) return 0;
    const int directionGroup = walk_direction_group_from_direction(direction);
    const int rawIndex = directionGroup * WALK_FRAMES_PER_DIRECTION + (gaitFrame % WALK_FRAMES_PER_DIRECTION);
    return rawIndex % frameCount;
}

// ============================================================================
// Cursor creation
// ============================================================================

GLFWcursor* create_cursor_from_png(const std::filesystem::path& imagePath, int xhot, int yhot)
{
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(imagePath.string().c_str(), &width, &height, &channels, 4);
    if (!pixels)
    {
        return nullptr;
    }

    GLFWimage glfwImage;
    glfwImage.width = static_cast<int>(width);
    glfwImage.height = static_cast<int>(height);
    glfwImage.pixels = pixels;

    GLFWcursor* cursor = glfwCreateCursor(&glfwImage, xhot, yhot);

    stbi_image_free(pixels);
    return cursor;
}
