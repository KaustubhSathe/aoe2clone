#include "RendererHelpers.h"
#include "../Core/Constants.h"

#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#include <iostream>
#include <algorithm>

template <typename T>
void safe_release(T*& pointer)
{
    if (pointer != nullptr)
    {
        pointer->Release();
        pointer = nullptr;
    }
}

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
        std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        std::cerr << "Shader compilation failed:\n" << log << '\n';
    }

    return shader;
}

GLuint create_program(const char* vertexSource, const char* fragmentSource)
{
    const GLuint vertexShader = compile_shader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compile_shader(GL_FRAGMENT_SHADER, fragmentSource);

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
        std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        std::cerr << "Program link failed:\n" << log << '\n';
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

#include <fstream>
#include <sstream>

std::string read_file_to_string(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open shader file: " << path.string() << '\n';
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint create_program_from_files(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
{
    std::string vertexSource = read_file_to_string(vertexPath);
    std::string fragmentSource = read_file_to_string(fragmentPath);
    if (vertexSource.empty() || fragmentSource.empty())
    {
        std::cerr << "Failed to load shaders: " << vertexPath.string() << " / " << fragmentPath.string() << '\n';
        return 0;
    }
    return create_program(vertexSource.c_str(), fragmentSource.c_str());
}

bool load_texture_from_png(
    const std::filesystem::path& imagePath,
    IWICImagingFactory* imagingFactory,
    TextureFrame& outFrame)
{
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    const HRESULT decoderResult = imagingFactory->CreateDecoderFromFilename(
        imagePath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder);
    if (FAILED(decoderResult))
    {
        std::cerr << "Failed to decode texture: " << imagePath.string() << '\n';
        return false;
    }

    HRESULT result = decoder->GetFrame(0, &frame);
    if (FAILED(result))
    {
        safe_release(decoder);
        std::cerr << "Failed to read PNG frame: " << imagePath.string() << '\n';
        return false;
    }

    result = imagingFactory->CreateFormatConverter(&converter);
    if (FAILED(result))
    {
        safe_release(frame);
        safe_release(decoder);
        std::cerr << "Failed to create WIC format converter\n";
        return false;
    }

    result = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(result))
    {
        safe_release(converter);
        safe_release(frame);
        safe_release(decoder);
        std::cerr << "Failed to convert PNG format: " << imagePath.string() << '\n';
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    converter->GetSize(&width, &height);

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    result = converter->CopyPixels(
        nullptr,
        width * 4,
        static_cast<UINT>(pixels.size()),
        pixels.data());
    if (FAILED(result))
    {
        safe_release(converter);
        safe_release(frame);
        safe_release(decoder);
        std::cerr << "Failed to copy PNG pixels: " << imagePath.string() << '\n';
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
        GL_BGRA,
        GL_UNSIGNED_BYTE,
        pixels.data());

    outFrame.width = static_cast<int>(width);
    outFrame.height = static_cast<int>(height);
    safe_release(converter);
    safe_release(frame);
    safe_release(decoder);
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

    IWICImagingFactory* imagingFactory = nullptr;
    const HRESULT factoryResult = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&imagingFactory));
    if (FAILED(factoryResult))
    {
        std::cerr << "Failed to initialize WIC imaging factory\n";
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
        if (load_texture_from_png(pngFile, imagingFactory, frameData))
        {
            frames.push_back(frameData);
        }
    }

    safe_release(imagingFactory);

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
