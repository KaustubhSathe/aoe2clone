// =============================================================================
// INCLUDES
// Windows & WIC first (WIC provides PNG decoding via the Windows Imaging Component).
// GLAD must come before GLFW so OpenGL function pointers are loaded in the right order.
// GLM provides vector/matrix math. ImGui provides the HUD overlay.
// =============================================================================
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// =============================================================================
// FORWARD DECLARATIONS
// Declared here so main() can register them as GLFW callbacks before they are
// defined at the bottom of the file.
// =============================================================================
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow* window);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);

// =============================================================================
// EXTERN GLOBALS
// Defined in global scope below main(). Declared extern here so the helper
// functions inside the anonymous namespace can read them without being passed
// as parameters on every call.
// =============================================================================
extern unsigned int SCR_WIDTH;
extern unsigned int SCR_HEIGHT;
extern float cameraX;
extern float cameraY;
extern float zoom;

// =============================================================================
// CONSTANTS, DATA TYPES & GAME LOGIC
// Everything inside this anonymous namespace is private to this translation
// unit (like a "file-private" scope). It contains:
//   - Compile-time constants that define the map and animation layout
//   - Plain data structs that represent game objects
//   - Pure helper functions (texture loading, coordinate math, selection logic)
// =============================================================================
namespace
{
// The map is a square grid of this many tiles per side (200x200 = 40,000 tiles total).
constexpr int GRID_SIZE = 200;

// Half the width of a single isometric diamond tile in world-space pixels.
// Full tile width = 96px. Used to convert between tile coords and world coords.
constexpr float TILE_HALF_WIDTH = 48.0f;

// Half the height of a single isometric diamond tile in world-space pixels.
// Full tile height = 48px. Used to convert between tile coords and world coords.
constexpr float TILE_HALF_HEIGHT = 24.0f;

// The diagonal distance between adjacent tile centers in world space.
// Computed as sqrt(TILE_HALF_WIDTH^2 + TILE_HALF_HEIGHT^2) ~= 53.67.
// Used to set villager move speed so it feels tile-relative.
constexpr float TILE_WORLD_STEP = 53.66563f;

// Number of facing directions in the walk spritesheet (N, NNE, NE, E, ... all 16 compass slices).
constexpr int WALK_DIRECTION_COUNT = 16;

// Number of animation frames per facing direction in the walk spritesheet.
// Total frames = WALK_DIRECTION_COUNT * WALK_FRAMES_PER_DIRECTION = 480.
constexpr int WALK_FRAMES_PER_DIRECTION = 30;

// How fast the walk animation plays back, in frames per second.
// At 15 FPS the full 30-frame walk cycle takes 2 seconds, matching AoE2's original speed.
constexpr float WALK_ANIMATION_FPS = 15.0f;

// How close the cursor must be to a villager's screen position (in pixels) to count as a click hit.
constexpr float CLICK_SELECT_RADIUS = 26.0f;

// How many pixels the mouse must move while held before a click becomes a drag-select box.
// Prevents accidental box-selections when the player just clicks.
constexpr float DRAG_THRESHOLD = 4.0f;

// World-space offset applied when rendering pine tree sprites.
// Shifts the sprite 14px left so the visual trunk aligns with the tile center.
constexpr glm::vec2 PINE_RENDER_OFFSET = glm::vec2(-20.0f, -10.0f);

// Holds a single loaded sprite frame: its OpenGL texture handle, the frame'ss
// index within its animation sequence, and its pixel dimensions.
struct TextureFrame
{
    GLuint texture = 0;    // OpenGL texture object ID — passed to glBindTexture
    int frameIndex = 0;    // The numeric index baked into the source filename (e.g. "_04" -> 4)
    int width = 0;         // Texture width in pixels
    int height = 0;        // Texture height in pixels
};

// A complete animation clip: an ordered list of TextureFrames that are
// cycled through to produce walking, idle, or any other animation.
struct AnimationSet
{
    std::vector<TextureFrame> frames; // All frames in playback order
};

// Represents the player-controlled villager unit.
struct Villager
{
    glm::vec2 position = glm::vec2(0.0f);              // Current world-space position
    glm::vec2 targetPosition = glm::vec2(0.0f);        // Destination world-space position (set on right-click)
    glm::vec2 facingDirection = glm::vec2(1.0f, 0.0f); // Normalized direction vector used to pick the correct animation row
    bool selected = false;                              // True when this unit has been clicked or drag-selected
    bool moving = false;                                // True while the unit is travelling toward targetPosition
    float moveSpeed = TILE_WORLD_STEP * 0.8f * 1.7f * 100.0f;  // World-space units per second (~72.7 units/sec)
    float walkAnimTimer = 0.0f;                         // Accumulates elapsed time to know when to advance the animation frame
    int walkFrameIndex = 0;                             // Current frame within the 30-frame walk cycle
    std::deque<glm::vec2> waypointQueue;               // Shift+RClick queued destinations — popped in order after each arrival
};

// Represents a static pine tree obstacle placed on the map.
struct PineTree
{
    glm::ivec2 tile = glm::ivec2(0);     // Tile-space grid coordinate — used for collision/pathfinding
    glm::vec2 position = glm::vec2(0.0f); // World-space position (pre-computed from tile via tile_to_world)
    bool selected = false;                // True when this tree is the currently selected object
};

// Tracks the state of the mouse drag-select box.
struct SelectionState
{
    bool dragging = false;                     // True from left mouse down until left mouse up
    bool moved = false;                        // True once the cursor has moved more than DRAG_THRESHOLD pixels
    glm::dvec2 startScreen = glm::dvec2(0.0); // Screen position where the drag began
    glm::dvec2 currentScreen = glm::dvec2(0.0); // Screen position of the cursor right now
};

// Top-level container for all mutable game state.
// A single instance lives in main() and a raw pointer (gAppState) is shared with GLFW callbacks.
struct AppState
{
    Villager villager;                                // The one player-controlled unit
    std::vector<PineTree> pineTrees;                  // All trees on the map (obstacles + selectable objects)
    glm::vec2 pineTreeSpriteSize = glm::vec2(108.0f, 162.0f); // Pixel dimensions of the pine tree sprite (set after texture load)
    SelectionState selection;                         // Current mouse drag-select state
    glm::dvec2 cursorScreen = glm::dvec2(0.0);       // Last recorded cursor position in screen space
    int selectedTreeIndex = -1;                       // Index into pineTrees of the selected tree, or -1 if none
    
    std::vector<bool> explored;                       // True if the tile has been explored at any point
    std::vector<bool> visible;                        // True if the tile is currently in line of sight
    std::vector<float> tileVisibilities;              // Flattened GPU array (1.0 = visible, 0.5 = fog, 0.0 = unexplored)

    int food = 200;    // Resource counts displayed in the HUD
    int wood = 200;
    int stone = 150;
    int gold = 100;
};

AppState* gAppState = nullptr; // Global raw pointer so GLFW callbacks can reach the game state

// -----------------------------------------------------------------------------
// OpenGL & WIC Helpers
// Low-level utilities for compiling shaders and loading PNG textures from disk.
// -----------------------------------------------------------------------------

// Safely calls Release() on a COM pointer and nulls it out.
// Prevents double-release crashes when cleaning up WIC imaging objects.
template <typename T>
void safe_release(T*& pointer)
{
    if (pointer != nullptr)
    {
        pointer->Release();
        pointer = nullptr;
    }
}

// Compiles a GLSL shader from source code and returns its OpenGL ID.
// Prints an error and returns a broken shader handle if compilation fails.
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

// Links a vertex and fragment shader into a complete OpenGL shader program.
// Returns the program ID; prints an error if linking fails.
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

// Loads a single PNG file from disk using WIC (Windows Imaging Component),
// uploads it as a GL_RGBA8 texture, and fills outFrame with the texture ID and dimensions.
// Returns false if any step (decoding, format conversion, upload) fails.
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

// Loads every PNG in a directory as a TextureFrame, sorted by the numeric suffix in their filename
// (e.g. "walk_04.png" gets frameIndex=4). The last frame is dropped if it duplicates frame 0
// (some AoE2 SLD exports include a wrap-around duplicate).
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

// Loads the single frame with a specific frameIndex from a directory.
// Returns std::nullopt and frees all other textures if no matching index is found.
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

// -----------------------------------------------------------------------------
// Animation Helpers
// Convert a world-space direction vector into the correct spritesheet frame index
// for both the multi-frame walk animation and the single-frame-per-direction idle.
// -----------------------------------------------------------------------------

// Given a movement direction vector and the total number of directional frames available,
// returns the frame index that best matches the angle. Used for the idle animation
// which has one frame per direction rather than a full walk cycle.
int facing_index_from_direction(const glm::vec2& direction, int frameCount)
{
    if (frameCount <= 0)
    {
        return 0;
    }

    const glm::vec2 normalized = glm::length(direction) > 0.0001f ? glm::normalize(direction) : glm::vec2(1.0f, 0.0f);
    const float screenDegrees = glm::degrees(std::atan2(-normalized.y, normalized.x));
    float clockwiseDegrees = std::fmod(screenDegrees, 360.0f);
    if (clockwiseDegrees < 0.0f)
    {
        clockwiseDegrees += 360.0f;
    }

    const float step = 360.0f / static_cast<float>(frameCount);
    return static_cast<int>(std::round(clockwiseDegrees / step)) % frameCount;
}

// Maps a direction vector to one of the 16 compass groups used in the walk spritesheet.
// Returns a value in [0, WALK_DIRECTION_COUNT) that acts as a row index into the sheet.
int walk_direction_group_from_direction(const glm::vec2& direction)
{
    const glm::vec2 normalized = glm::length(direction) > 0.0001f ? glm::normalize(direction) : glm::vec2(1.0f, 0.0f);
    const float screenDegrees = glm::degrees(std::atan2(-normalized.y, normalized.x));
    float clockwiseDegrees = std::fmod(screenDegrees, 360.0f);
    if (clockwiseDegrees < 0.0f)
    {
        clockwiseDegrees += 360.0f;
    }

    const float step = 360.0f / static_cast<float>(WALK_DIRECTION_COUNT);
    return static_cast<int>(std::round(clockwiseDegrees / step)) % WALK_DIRECTION_COUNT;
}

// Combines direction and current gait step into a single flat index into the walk animation frame array.
// Layout: [direction0_frame0 ... direction0_frame29 | direction1_frame0 ... | ...]
int walk_animation_index(const glm::vec2& direction, int gaitFrame, int frameCount)
{
    if (frameCount <= 0)
    {
        return 0;
    }

    const int directionGroup = walk_direction_group_from_direction(direction);
    const int rawIndex = directionGroup * WALK_FRAMES_PER_DIRECTION + (gaitFrame % WALK_FRAMES_PER_DIRECTION);
    return rawIndex % frameCount;
}

// -----------------------------------------------------------------------------
// Coordinate Conversion
// Three coordinate spaces are used:
//   Tile space  - integer grid indices (glm::ivec2), used for logic & pathfinding
//   World space - float 2D plane (glm::vec2), used for physics & rendering positions
//   Screen space - pixel coordinates (glm::dvec2), origin top-left, used for UI & input
// -----------------------------------------------------------------------------

// Converts a tile-space grid coordinate to a world-space 2D position.
// The isometric projection staggers tiles diagonally:
//   world.x = (tile.x - tile.y) * TILE_HALF_WIDTH
//   world.y = -(tile.x + tile.y) * TILE_HALF_HEIGHT
glm::vec2 tile_to_world(const glm::ivec2& tile)
{
    return glm::vec2(
        static_cast<float>(tile.x - tile.y) * TILE_HALF_WIDTH,
        -static_cast<float>(tile.x + tile.y) * TILE_HALF_HEIGHT);
}

// Converts a world-space position back into tile-space grid coordinates.
// Returns std::nullopt if the position falls outside the [0, GRID_SIZE) bounds.
std::optional<glm::ivec2> world_to_tile(const glm::vec2& worldPosition)
{
    const float a = worldPosition.x / TILE_HALF_WIDTH;
    const float b = -worldPosition.y / TILE_HALF_HEIGHT;
    const int tileX = static_cast<int>(std::round((a + b) * 0.5f));
    const int tileY = static_cast<int>(std::round((b - a) * 0.5f));
    if (tileX < 0 || tileX >= GRID_SIZE || tileY < 0 || tileY >= GRID_SIZE)
    {
        return std::nullopt;
    }

    return glm::ivec2(tileX, tileY);
}

// Converts a screen-space pixel position (origin top-left) to world-space,
// accounting for camera offset and zoom level.
glm::vec2 screen_to_world(const glm::dvec2& screenPosition)
{
    const float worldX = cameraX + (static_cast<float>(screenPosition.x) - static_cast<float>(SCR_WIDTH) * 0.5f) / zoom;
    const float worldY = cameraY + (static_cast<float>(SCR_HEIGHT) * 0.5f - static_cast<float>(screenPosition.y)) / zoom;
    return glm::vec2(worldX, worldY);
}

// Converts a world-space position to screen-space pixel coordinates,
// accounting for camera offset and zoom level.
glm::vec2 world_to_screen(const glm::vec2& worldPosition)
{
    const float screenX = (worldPosition.x - cameraX) * zoom + static_cast<float>(SCR_WIDTH) * 0.5f;
    const float screenY = static_cast<float>(SCR_HEIGHT) * 0.5f - (worldPosition.y - cameraY) * zoom;
    return glm::vec2(screenX, screenY);
}

// -----------------------------------------------------------------------------
// Selection & Game Logic Helpers
// Hit-testing functions for click and drag-box selection, plus collision queries.
// -----------------------------------------------------------------------------

// Returns true if a screen-space point falls inside the current drag-select rectangle.
// The rectangle is defined by selection.startScreen and selection.currentScreen.
bool point_in_drag_rect(const glm::vec2& point, const SelectionState& selection)
{
    const double minX = std::min(selection.startScreen.x, selection.currentScreen.x);
    const double maxX = std::max(selection.startScreen.x, selection.currentScreen.x);
    const double minY = std::min(selection.startScreen.y, selection.currentScreen.y);
    const double maxY = std::max(selection.startScreen.y, selection.currentScreen.y);
    return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
}

// Returns true if the cursor is within CLICK_SELECT_RADIUS pixels of the villager's screen position.
// Used for single-click selection of the villager.
bool villager_hit_test_screen(const glm::vec2& villagerScreenPosition, const glm::dvec2& cursorScreen)
{
    const glm::vec2 delta = glm::vec2(static_cast<float>(cursorScreen.x), static_cast<float>(cursorScreen.y)) - villagerScreenPosition;
    return glm::length(delta) <= CLICK_SELECT_RADIUS;
}

// Returns true if the cursor falls within the axis-aligned bounding box of a pine tree's sprite.
// The bounding box is computed from the tree's world position plus PINE_RENDER_OFFSET.
bool tree_hit_test_screen(const PineTree& tree, const glm::dvec2& cursorScreen, const glm::vec2& spriteSize)
{
    const glm::vec2 spriteOrigin = tree.position + PINE_RENDER_OFFSET;
    const glm::vec2 screenBottomLeft = world_to_screen(spriteOrigin + glm::vec2(-0.5f * spriteSize.x, 0.0f));
    const glm::vec2 screenTopRight = world_to_screen(spriteOrigin + glm::vec2(0.5f * spriteSize.x, spriteSize.y));
    const float minX = std::min(screenBottomLeft.x, screenTopRight.x);
    const float maxX = std::max(screenBottomLeft.x, screenTopRight.x);
    const float minY = std::min(screenBottomLeft.y, screenTopRight.y);
    const float maxY = std::max(screenBottomLeft.y, screenTopRight.y);
    return static_cast<float>(cursorScreen.x) >= minX && static_cast<float>(cursorScreen.x) <= maxX &&
        static_cast<float>(cursorScreen.y) >= minY && static_cast<float>(cursorScreen.y) <= maxY;
}

// Deselects everything: the villager and all trees.
void clear_selection(AppState& appState)
{
    appState.villager.selected = false;
    appState.selectedTreeIndex = -1;
    for (PineTree& tree : appState.pineTrees)
    {
        tree.selected = false;
    }
}

// Returns true if a pine tree occupies the given tile coordinate.
// Used to prevent the villager from walking into tree tiles.
bool is_tree_tile_blocked(const AppState& appState, const glm::ivec2& tile)
{
    return std::any_of(
        appState.pineTrees.begin(),
        appState.pineTrees.end(),
        [&](const PineTree& tree)
        {
            return tree.tile == tile;
        });
}

// Returns a list of world-space positions for every tile that is blocked by a tree.
// Used to build the instanced GPU buffer that renders darker tiles under trees.
std::vector<glm::vec2> blocked_tile_translations(const AppState& appState)
{
    std::vector<glm::vec2> blockedTiles;
    blockedTiles.reserve(appState.pineTrees.size());
    for (const PineTree& tree : appState.pineTrees)
    {
        blockedTiles.push_back(tile_to_world(tree.tile));
    }
    return blockedTiles;
}
} // namespace

// =============================================================================
// GLOBAL STATE
// These live in global scope so GLFW callbacks (which can't take user data
// easily) and the helper functions above can access them via extern.
// =============================================================================
unsigned int SCR_WIDTH = 1280;  // Window / framebuffer width in pixels
unsigned int SCR_HEIGHT = 720;  // Window / framebuffer height in pixels

float cameraX = 0.0f;          // World-space X position of the camera (center of screen)
float cameraY = -4800.0f;      // World-space Y position — starts near map center (tile 100,100)
float cameraSpeed = 1000.0f;   // How fast the camera pans per second (world units)
float zoom = 1.0f;             // Zoom multiplier: >1 zooms in, <1 zooms out

float deltaTime = 0.0f;        // Seconds elapsed since the previous frame
float lastFrame = 0.0f;        // Timestamp of the previous frame (from glfwGetTime)

// =============================================================================
// GLSL SHADER SOURCES
// Inline GLSL strings compiled at runtime. Three shader programs are used:
//   Tile shader   - draws instanced isometric tile diamonds (filled + outline)
//   Sprite shader - draws textured quads for villager and tree sprites
//   Overlay shader - draws untextured colored shapes (selection circle, drag box)
// =============================================================================
const char* tileVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aOffset;
    layout (location = 2) in float aVisibility;

    uniform mat4 uProjection;
    uniform mat4 uView;

    out float vVisibility;

    void main()
    {
        vec2 worldPos = aPos + aOffset;
        gl_Position = uProjection * uView * vec4(worldPos, 0.0, 1.0);
        vVisibility = aVisibility;
    }
)";

const char* tileFragmentShaderSource = R"(
    #version 330 core
    in float vVisibility;
    out vec4 FragColor;
    uniform vec4 uColor;

    void main()
    {
        FragColor = uColor * vec4(vVisibility, vVisibility, vVisibility, 1.0);
    }
)";

const char* spriteVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aUV;

    uniform mat4 uProjection;
    uniform mat4 uView;
    uniform vec2 uSpritePos;
    uniform vec2 uSpriteSize;

    out vec2 vUV;

    void main()
    {
        vec2 worldPos = uSpritePos + (aPos * uSpriteSize);
        gl_Position = uProjection * uView * vec4(worldPos, 0.0, 1.0);
        vUV = aUV;
    }
)";

const char* spriteFragmentShaderSource = R"(
    #version 330 core
    in vec2 vUV;
    out vec4 FragColor;

    uniform sampler2D uTexture;
    uniform float uVisibility;

    void main()
    {
        vec4 color = texture(uTexture, vUV);
        if (color.a < 0.01)
        {
            discard;
        }
        FragColor = color * vec4(uVisibility, uVisibility, uVisibility, 1.0);
    }
)";

const char* overlayVertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;

    uniform mat4 uProjection;
    uniform mat4 uView;
    uniform vec2 uOffset;

    void main()
    {
        gl_Position = uProjection * uView * vec4(aPos + uOffset, 0.0, 1.0);
    }
)";

const char* overlayFragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    uniform vec4 uColor;

    void main()
    {
        FragColor = uColor;
    }
)";

int main()
{
    // -------------------------------------------------------------------------
    // Initialization: COM, GLFW, glad, ImGui
    // Boot up all the libraries the engine depends on before doing anything else.
    // -------------------------------------------------------------------------
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult))
    {
        std::cerr << "Failed to initialize COM\n";
        return -1;
    }

    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        CoUninitialize();
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "AoE2 Clone - Isometric Platform", nullptr, nullptr);
    if (window == nullptr)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        CoUninitialize();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        CoUninitialize();
        return -1;
    }

    AppState appState;
    appState.explored.resize(GRID_SIZE * GRID_SIZE, false);
    appState.visible.resize(GRID_SIZE * GRID_SIZE, false);
    appState.tileVisibilities.resize(GRID_SIZE * GRID_SIZE, 0.0f);
    gAppState = &appState;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.84f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    const GLuint tileShaderProgram = create_program(tileVertexShaderSource, tileFragmentShaderSource);
    const GLuint spriteShaderProgram = create_program(spriteVertexShaderSource, spriteFragmentShaderSource);
    const GLuint overlayShaderProgram = create_program(overlayVertexShaderSource, overlayFragmentShaderSource);

    // -------------------------------------------------------------------------
    // Tile Geometry
    // Pre-compute the two diamond shapes (filled + outline) used for every tile,
    // and build a translation table (one world-space offset per tile) for
    // instanced rendering — one draw call renders all 40,000 tiles at once.
    // -------------------------------------------------------------------------
    float tileVertices[] = {
        0.0f, TILE_HALF_HEIGHT,
       -TILE_HALF_WIDTH, 0.0f,
        TILE_HALF_WIDTH, 0.0f,
       -TILE_HALF_WIDTH, 0.0f,
        0.0f, -TILE_HALF_HEIGHT,
        TILE_HALF_WIDTH, 0.0f
    };

    float tileOutline[] = {
        0.0f, TILE_HALF_HEIGHT,
        TILE_HALF_WIDTH, 0.0f,
        0.0f, -TILE_HALF_HEIGHT,
       -TILE_HALF_WIDTH, 0.0f
    };
    std::vector<glm::vec2> translations;
    translations.reserve(GRID_SIZE * GRID_SIZE);

    for (int y = 0; y < GRID_SIZE; y++)
    {
        for (int x = 0; x < GRID_SIZE; x++)
        {
            const float screenX = static_cast<float>(x - y) * TILE_HALF_WIDTH;
            const float screenY = -static_cast<float>(x + y) * TILE_HALF_HEIGHT;
            translations.push_back(glm::vec2(screenX, screenY));
        }
    }

    // -------------------------------------------------------------------------
    // Asset Loading
    // Load all sprite PNGs from disk into GPU textures.
    // Walk animation: 480 frames (16 directions x 30 frames each).
    // Idle animation: one frame per direction.
    // Pine tree: a single static frame (frame index 4 from the SLD folder).
    // -------------------------------------------------------------------------
    AnimationSet walkAnimation;
    walkAnimation.frames = load_frame_directory(std::filesystem::path("assets") / "u_vil_male_villager_walkA_x1.sld");
    AnimationSet idleAnimation;
    idleAnimation.frames = load_frame_directory(std::filesystem::path("assets") / "u_vil_male_villager_idleA_x1.sld");
    if (walkAnimation.frames.empty())
    {
        std::cerr << "No villager walk frames were loaded\n";
    }
    if (idleAnimation.frames.empty())
    {
        std::cerr << "No villager idle frames were loaded\n";
    }

    std::optional<TextureFrame> pineTreeFrame = load_frame_by_index(std::filesystem::path("assets") / "n_tree_pine_x1.sld", 4);
    if (!pineTreeFrame.has_value())
    {
        std::cerr << "Failed to load pine tree frame image_1x1_04.png\n";
    }

    // -------------------------------------------------------------------------
    // Tree Placement
    // Scatter pine trees pseudo-randomly across the map using a hash of (x, y).
    // A clear radius of 18 tiles around the center is kept free so the villager
    // spawns in an open area and can move without immediately being blocked.
    // ~4% of eligible tiles outside the radius get a tree.
    // -------------------------------------------------------------------------
    appState.pineTrees.reserve(220);
    for (int y = 0; y < GRID_SIZE; y++)
    {
        for (int x = 0; x < GRID_SIZE; x++)
        {
            const int dx = x - (GRID_SIZE / 2);
            const int dy = y - (GRID_SIZE / 2);
            if ((dx * dx) + (dy * dy) < 18 * 18)
            {
                continue;
            }

            const unsigned int hash = static_cast<unsigned int>((x * 73856093) ^ (y * 19349663));
            if ((hash % 100) < 4)
            {
                PineTree tree;
                tree.tile = glm::ivec2(x, y);
                tree.position = tile_to_world(tree.tile);
                appState.pineTrees.push_back(tree);
            }
        }
    }

    appState.villager.position = translations[(GRID_SIZE / 2) * GRID_SIZE + (GRID_SIZE / 2)];
    appState.villager.targetPosition = appState.villager.position;
    const std::vector<glm::vec2> blockedTileTranslations = blocked_tile_translations(appState);

    // -------------------------------------------------------------------------
    // GPU Buffer Setup
    // Upload all geometry into OpenGL VAOs and VBOs. Everything is static
    // (GL_STATIC_DRAW) except the drag-select rectangle which is updated every
    // frame the user is dragging (GL_DYNAMIC_DRAW).
    // VAOs store the vertex layout; VBOs store the actual vertex data.
    // -------------------------------------------------------------------------
    unsigned int tileVAO = 0;
    unsigned int tileVBO = 0;
    unsigned int outlineVAO = 0;
    unsigned int outlineVBO = 0;
    unsigned int instanceVBO = 0;
    unsigned int visibilityVBO = 0;
    unsigned int blockedTileVAO = 0;
    unsigned int blockedTileVBO = 0;
    unsigned int blockedInstanceVBO = 0;
    glGenVertexArrays(1, &tileVAO);
    glGenBuffers(1, &tileVBO);
    glGenVertexArrays(1, &outlineVAO);
    glGenBuffers(1, &outlineVBO);
    glGenBuffers(1, &instanceVBO);
    glGenBuffers(1, &visibilityVBO);
    glGenVertexArrays(1, &blockedTileVAO);
    glGenBuffers(1, &blockedTileVBO);
    glGenBuffers(1, &blockedInstanceVBO);

    glBindVertexArray(tileVAO);
    glBindBuffer(GL_ARRAY_BUFFER, tileVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tileVertices), tileVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, translations.size() * sizeof(glm::vec2), translations.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    glVertexAttribDivisor(1, 1);

    glBindBuffer(GL_ARRAY_BUFFER, visibilityVBO);
    glBufferData(GL_ARRAY_BUFFER, appState.tileVisibilities.size() * sizeof(float), appState.tileVisibilities.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(outlineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, outlineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tileOutline), tileOutline, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    glVertexAttribDivisor(1, 1);

    glBindBuffer(GL_ARRAY_BUFFER, visibilityVBO);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(blockedTileVAO);
    glBindBuffer(GL_ARRAY_BUFFER, blockedTileVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tileVertices), tileVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, blockedInstanceVBO);
    glBufferData(GL_ARRAY_BUFFER, blockedTileTranslations.size() * sizeof(glm::vec2), blockedTileTranslations.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    glVertexAttribDivisor(1, 1);

    const float spriteQuad[] = {
       -0.5f, 0.0f, 0.0f, 1.0f,
        0.5f, 0.0f, 1.0f, 1.0f,
        0.5f, 1.0f, 1.0f, 0.0f,
       -0.5f, 0.0f, 0.0f, 1.0f,
        0.5f, 1.0f, 1.0f, 0.0f,
       -0.5f, 1.0f, 0.0f, 0.0f
    };

    unsigned int spriteVAO = 0;
    unsigned int spriteVBO = 0;
    glGenVertexArrays(1, &spriteVAO);
    glGenBuffers(1, &spriteVBO);
    glBindVertexArray(spriteVAO);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(spriteQuad), spriteQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    std::vector<float> selectionCircle;
    constexpr int selectionSegments = 32;
    constexpr float selectionRadiusX = 24.0f;
    constexpr float selectionRadiusY = 12.0f;
    selectionCircle.reserve(selectionSegments * 2);
    for (int i = 0; i < selectionSegments; i++)
    {
        const float angle = (static_cast<float>(i) / static_cast<float>(selectionSegments)) * 2.0f * 3.14159265f;
        selectionCircle.push_back(std::cos(angle) * selectionRadiusX);
        selectionCircle.push_back(std::sin(angle) * selectionRadiusY);
    }

    unsigned int selectionVAO = 0;
    unsigned int selectionVBO = 0;
    glGenVertexArrays(1, &selectionVAO);
    glGenBuffers(1, &selectionVBO);
    glBindVertexArray(selectionVAO);
    glBindBuffer(GL_ARRAY_BUFFER, selectionVBO);
    glBufferData(GL_ARRAY_BUFFER, selectionCircle.size() * sizeof(float), selectionCircle.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    unsigned int rectVAO = 0;
    unsigned int rectVBO = 0;
    glGenVertexArrays(1, &rectVAO);
    glGenBuffers(1, &rectVBO);
    glBindVertexArray(rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // Cache uniform locations for fast access inside the render loop.
    // glGetUniformLocation is slow — do it once here rather than every frame.
    const GLint tileColorLoc = glGetUniformLocation(tileShaderProgram, "uColor");
    const GLint tileProjLoc = glGetUniformLocation(tileShaderProgram, "uProjection");
    const GLint tileViewLoc = glGetUniformLocation(tileShaderProgram, "uView");

    const GLint spriteProjLoc = glGetUniformLocation(spriteShaderProgram, "uProjection");
    const GLint spriteViewLoc = glGetUniformLocation(spriteShaderProgram, "uView");
    const GLint spritePosLoc = glGetUniformLocation(spriteShaderProgram, "uSpritePos");
    const GLint spriteSizeLoc = glGetUniformLocation(spriteShaderProgram, "uSpriteSize");
    const GLint spriteVisLoc = glGetUniformLocation(spriteShaderProgram, "uVisibility");

    const GLint overlayProjLoc = glGetUniformLocation(overlayShaderProgram, "uProjection");
    const GLint overlayViewLoc = glGetUniformLocation(overlayShaderProgram, "uView");
    const GLint overlayOffsetLoc = glGetUniformLocation(overlayShaderProgram, "uOffset");
    const GLint overlayColorLoc = glGetUniformLocation(overlayShaderProgram, "uColor");

    glUseProgram(spriteShaderProgram);
    glUniform1i(glGetUniformLocation(spriteShaderProgram, "uTexture"), 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const glm::vec2 spriteScale(72.0f, 72.0f);
    const glm::vec2 pineTreeScale = pineTreeFrame.has_value()
        ? glm::vec2(static_cast<float>(pineTreeFrame->width), static_cast<float>(pineTreeFrame->height))
        : glm::vec2(108.0f, 162.0f);
    appState.pineTreeSpriteSize = pineTreeScale;

    const float spriteBoundsOutline[] = {
        -0.5f * pineTreeScale.x, 0.0f,
         0.5f * pineTreeScale.x, 0.0f,
         0.5f * pineTreeScale.x, pineTreeScale.y,
        -0.5f * pineTreeScale.x, pineTreeScale.y
    };

    unsigned int pineBoundsVAO = 0;
    unsigned int pineBoundsVBO = 0;
    glGenVertexArrays(1, &pineBoundsVAO);
    glGenBuffers(1, &pineBoundsVBO);
    glBindVertexArray(pineBoundsVAO);
    glBindBuffer(GL_ARRAY_BUFFER, pineBoundsVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(spriteBoundsOutline), spriteBoundsOutline, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // =========================================================================
    // MAIN GAME LOOP
    // Runs once per frame until the window is closed. Each iteration:
    //   1. Compute deltaTime
    //   2. Poll input
    //   3. Update game logic (villager movement & animation)
    //   4. Clear the framebuffer
    //   5. Render: tiles → trees → villager → overlays → HUD
    //   6. Swap buffers
    // =========================================================================
    while (!glfwWindowShouldClose(window))
    {
        const float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ---------------------------------------------------------------------
        // Game Logic Update
        // Move the villager toward its target, advance its walk animation frame,
        // and stop it if it reaches a tree-blocked tile.
        // When a waypoint is reached, the next queued waypoint (if any) is
        // automatically popped and becomes the new target (AoE2-style queuing).
        // ---------------------------------------------------------------------
        if (appState.villager.moving)
        {
            appState.villager.walkAnimTimer += deltaTime;
            const float frameTime = 1.0f / WALK_ANIMATION_FPS;
            while (appState.villager.walkAnimTimer >= frameTime)
            {
                appState.villager.walkAnimTimer -= frameTime;
                appState.villager.walkFrameIndex = (appState.villager.walkFrameIndex + 1) % WALK_FRAMES_PER_DIRECTION;
            }

            const glm::vec2 toTarget = appState.villager.targetPosition - appState.villager.position;
            const float remainingDistance = glm::length(toTarget);
            const float moveDistance = appState.villager.moveSpeed * deltaTime;

            if (remainingDistance <= moveDistance)
            {
                if (remainingDistance > 0.001f)
                {
                    appState.villager.facingDirection = glm::normalize(toTarget);
                }
                appState.villager.position = appState.villager.targetPosition;

                // Pop the next queued waypoint and keep moving, or stop.
                if (!appState.villager.waypointQueue.empty())
                {
                    const glm::vec2 nextWP = appState.villager.waypointQueue.front();
                    appState.villager.waypointQueue.pop_front();
                    const glm::vec2 toNext = nextWP - appState.villager.position;
                    if (glm::length(toNext) > 1.0f)
                    {
                        appState.villager.targetPosition = nextWP;
                        appState.villager.facingDirection = glm::normalize(toNext);
                        appState.villager.moving = true;
                    }
                    else
                    {
                        appState.villager.moving = false;
                    }
                }
                else
                {
                    appState.villager.moving = false;
                }
            }
            else
            {
                const glm::vec2 direction = toTarget / remainingDistance;
                const glm::vec2 nextPosition = appState.villager.position + direction * moveDistance;
                const std::optional<glm::ivec2> nextTile = world_to_tile(nextPosition);
                if (nextTile.has_value() && is_tree_tile_blocked(appState, *nextTile))
                {
                    // Blocked — cancel the entire queue
                    appState.villager.targetPosition = appState.villager.position;
                    appState.villager.waypointQueue.clear();
                    appState.villager.moving = false;
                }
                else
                {
                    appState.villager.facingDirection = direction;
                    appState.villager.position = nextPosition;
                }
            }
        }
        else
        {
            appState.villager.walkAnimTimer = 0.0f;
            appState.villager.walkFrameIndex = 0;
        }


        // ---------------------------------------------------------------------
        // Line of Sight Update (Fog of War)
        // Recompute the 4-tile circular radius around the villager each frame.
        // ---------------------------------------------------------------------
        std::fill(appState.visible.begin(), appState.visible.end(), false);
        const std::optional<glm::ivec2> villagerTile = world_to_tile(appState.villager.position);
        if (villagerTile.has_value())
        {
            const int cx = villagerTile->x;
            const int cy = villagerTile->y;
            const int radius = 4;
            const int radiusSq = radius * radius;
            for (int dy = -radius; dy <= radius; dy++)
            {
                for (int dx = -radius; dx <= radius; dx++)
                {
                    if (dx * dx + dy * dy <= radiusSq)
                    {
                        const int tx = cx + dx;
                        const int ty = cy + dy;
                        if (tx >= 0 && tx < GRID_SIZE && ty >= 0 && ty < GRID_SIZE)
                        {
                            const int index = ty * GRID_SIZE + tx;
                            appState.visible[index] = true;
                            appState.explored[index] = true;
                        }
                    }
                }
            }
        }
        
        for (int i = 0; i < GRID_SIZE * GRID_SIZE; i++)
        {
            if (appState.visible[i])
            {
                appState.tileVisibilities[i] = 1.0f;
            }
            else if (appState.explored[i])
            {
                appState.tileVisibilities[i] = 0.5f;
            }
            else
            {
                appState.tileVisibilities[i] = 0.0f;
            }
        }
        
        glBindBuffer(GL_ARRAY_BUFFER, visibilityVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, appState.tileVisibilities.size() * sizeof(float), appState.tileVisibilities.data());

        // ---------------------------------------------------------------------
        // Render Pass
        // Build the camera matrices and draw everything in back-to-front order:
        //   1. All ground tiles (instanced, one draw call)
        //   2. Darker tiles under trees (instanced)
        //   3. Tile grid outlines (instanced)
        //   4. Pine tree sprites + selection highlights
        //   5. Villager sprite + selection circle
        //   6. Drag-select rectangle (screen-space overlay)
        // ---------------------------------------------------------------------
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const float halfW = (static_cast<float>(SCR_WIDTH) / 2.0f) / zoom;
        const float halfH = (static_cast<float>(SCR_HEIGHT) / 2.0f) / zoom;
        const glm::mat4 projection = glm::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
        const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-cameraX, -cameraY, 0.0f));
        const glm::mat4 screenProjection = glm::ortho(0.0f, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT), 0.0f, -1.0f, 1.0f);
        const glm::mat4 identityView = glm::mat4(1.0f);

        glUseProgram(tileShaderProgram);
        glUniformMatrix4fv(tileProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(tileViewLoc, 1, GL_FALSE, glm::value_ptr(view));

        glBindVertexArray(tileVAO);
        glUniform4f(tileColorLoc, 0.2f, 0.6f, 0.2f, 1.0f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GRID_SIZE * GRID_SIZE);

        if (!blockedTileTranslations.empty())
        {
            glBindVertexArray(blockedTileVAO);
            glUniform4f(tileColorLoc, 0.09f, 0.18f, 0.09f, 1.0f);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(blockedTileTranslations.size()));
        }

        glBindVertexArray(outlineVAO);
        glUniform4f(tileColorLoc, 0.1f, 0.4f, 0.1f, 1.0f);
        glDrawArraysInstanced(GL_LINE_LOOP, 0, 4, GRID_SIZE * GRID_SIZE);

                if (pineTreeFrame.has_value())
                {
            glUseProgram(spriteShaderProgram);
            glUniformMatrix4fv(spriteProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
            glUniformMatrix4fv(spriteViewLoc, 1, GL_FALSE, glm::value_ptr(view));
            glUniform2f(spriteSizeLoc, pineTreeScale.x, pineTreeScale.y);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, pineTreeFrame->texture);
            glBindVertexArray(spriteVAO);

            for (const PineTree& pineTree : appState.pineTrees)
            {
                const int treeIndex = pineTree.tile.y * GRID_SIZE + pineTree.tile.x;
                const float treeVis = appState.tileVisibilities[treeIndex];
                if (treeVis == 0.0f)
                {
                    continue; // Skip rendering unexplored trees
                }

                const glm::vec2 pineTreeOrigin = pineTree.position + PINE_RENDER_OFFSET;
                glUniform1f(spriteVisLoc, treeVis);
                glUniform2f(spritePosLoc, pineTreeOrigin.x, pineTreeOrigin.y);
                glDrawArrays(GL_TRIANGLES, 0, 6);

                if (pineTree.selected)
                {
                    glUseProgram(overlayShaderProgram);
                    glUniformMatrix4fv(overlayProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
                    glUniformMatrix4fv(overlayViewLoc, 1, GL_FALSE, glm::value_ptr(view));
                    glUniform2f(overlayOffsetLoc, pineTreeOrigin.x, pineTreeOrigin.y);
                    glUniform4f(overlayColorLoc, 1.0f, 0.1f, 0.1f, 1.0f);
                    glBindVertexArray(pineBoundsVAO);
                    glDrawArrays(GL_LINE_LOOP, 0, 4);

                    glUniform2f(overlayOffsetLoc, pineTree.position.x + PINE_RENDER_OFFSET.x, pineTree.position.y - 4.0f);
                    glUniform4f(overlayColorLoc, 0.95f, 0.18f, 0.18f, 1.0f);
                    glBindVertexArray(selectionVAO);
                    glDrawArrays(GL_LINE_LOOP, 0, selectionSegments);

                    glUseProgram(spriteShaderProgram);
                    glUniformMatrix4fv(spriteProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
                    glUniformMatrix4fv(spriteViewLoc, 1, GL_FALSE, glm::value_ptr(view));
                    glUniform2f(spriteSizeLoc, pineTreeScale.x, pineTreeScale.y);
                    glUniform1f(spriteVisLoc, treeVis);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, pineTreeFrame->texture);
                    glBindVertexArray(spriteVAO);
                }
            }
        }

        const AnimationSet& activeAnimation = appState.villager.moving ? walkAnimation : idleAnimation;
        if (!activeAnimation.frames.empty())
        {
            const int frameIndex = appState.villager.moving
                ? walk_animation_index(appState.villager.facingDirection, appState.villager.walkFrameIndex, static_cast<int>(walkAnimation.frames.size()))
                : facing_index_from_direction(appState.villager.facingDirection, static_cast<int>(idleAnimation.frames.size()));
            const TextureFrame& activeFrame = activeAnimation.frames[static_cast<size_t>(frameIndex)];
            const glm::vec2 spriteOrigin = appState.villager.position + glm::vec2(0.0f, -TILE_HALF_HEIGHT);

            glUseProgram(spriteShaderProgram);
            glUniformMatrix4fv(spriteProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
            glUniformMatrix4fv(spriteViewLoc, 1, GL_FALSE, glm::value_ptr(view));
            glUniform1f(spriteVisLoc, 1.0f); // Villager is always fully lit
            glUniform2f(spritePosLoc, spriteOrigin.x, spriteOrigin.y);
            glUniform2f(spriteSizeLoc, spriteScale.x, spriteScale.y);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, activeFrame.texture);
            glBindVertexArray(spriteVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            if (appState.villager.selected)
            {
                glUseProgram(overlayShaderProgram);
                glUniformMatrix4fv(overlayProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
                glUniformMatrix4fv(overlayViewLoc, 1, GL_FALSE, glm::value_ptr(view));
                glUniform2f(overlayOffsetLoc, appState.villager.position.x, appState.villager.position.y - 22.0f);
                glUniform4f(overlayColorLoc, 0.1f, 1.0f, 0.1f, 1.0f);
                glBindVertexArray(selectionVAO);
                glDrawArrays(GL_LINE_LOOP, 0, selectionSegments);
            }

            // Draw waypoint markers: current target + all queued waypoints.
            // Each is a small diamond outline; a line connects them in order.
            if (appState.villager.selected && appState.villager.moving)
            {
                // Build the ordered list: current target first, then queued waypoints.
                std::vector<glm::vec2> allWPs;
                allWPs.reserve(1 + appState.villager.waypointQueue.size());
                allWPs.push_back(appState.villager.targetPosition);
                for (const glm::vec2& wp : appState.villager.waypointQueue)
                {
                    allWPs.push_back(wp);
                }

                glUseProgram(overlayShaderProgram);
                glUniformMatrix4fv(overlayProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
                glUniformMatrix4fv(overlayViewLoc, 1, GL_FALSE, glm::value_ptr(view));

                // Draw a small diamond at each waypoint.
                constexpr float wpHW = 8.0f;
                constexpr float wpHH = 4.0f;
                // Reuse a simple 4-point diamond defined inline (LINE_LOOP).
                // We draw it as 4 vertices: top, right, bottom, left.
                // Instead of creating a new VAO, we batch via the existing selectionVAO
                // by scaling via uOffset — but the simplest approach is a temp VBO.
                // We'll use a small static local VBO allocated once.
                static unsigned int wpVAO = 0;
                static unsigned int wpVBO = 0;
                if (wpVAO == 0)
                {
                    const float diamond[] = {
                         0.0f,  wpHH,
                         wpHW,  0.0f,
                         0.0f, -wpHH,
                        -wpHW,  0.0f
                    };
                    glGenVertexArrays(1, &wpVAO);
                    glGenBuffers(1, &wpVBO);
                    glBindVertexArray(wpVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, wpVBO);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(diamond), diamond, GL_STATIC_DRAW);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
                }

                // Draw line strip connecting villager -> each waypoint in order.
                {
                    // Build a temporary line-strip VBO: villager pos + all waypoints.
                    std::vector<float> lineVerts;
                    lineVerts.reserve((1 + allWPs.size()) * 2);
                    lineVerts.push_back(appState.villager.position.x);
                    lineVerts.push_back(appState.villager.position.y);
                    for (const glm::vec2& wp : allWPs)
                    {
                        lineVerts.push_back(wp.x);
                        lineVerts.push_back(wp.y);
                    }
                    static unsigned int lineVAO = 0;
                    static unsigned int lineVBO = 0;
                    if (lineVAO == 0)
                    {
                        glGenVertexArrays(1, &lineVAO);
                        glGenBuffers(1, &lineVBO);
                        glBindVertexArray(lineVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
                        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
                        glEnableVertexAttribArray(0);
                        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
                    }
                    glBindVertexArray(lineVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
                    glBufferData(GL_ARRAY_BUFFER,
                        static_cast<GLsizeiptr>(lineVerts.size() * sizeof(float)),
                        lineVerts.data(), GL_DYNAMIC_DRAW);
                    glUniform2f(overlayOffsetLoc, 0.0f, 0.0f);
                    glUniform4f(overlayColorLoc, 1.0f, 1.0f, 0.3f, 0.55f);
                    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(lineVerts.size() / 2));
                }

                // Draw diamond at each waypoint.
                glBindVertexArray(wpVAO);
                for (size_t i = 0; i < allWPs.size(); ++i)
                {
                    const glm::vec2& wp = allWPs[i];
                    glUniform2f(overlayOffsetLoc, wp.x, wp.y);
                    // First waypoint (current target) is brighter.
                    if (i == 0)
                        glUniform4f(overlayColorLoc, 1.0f, 1.0f, 0.0f, 1.0f);
                    else
                        glUniform4f(overlayColorLoc, 1.0f, 0.85f, 0.1f, 0.75f);
                    glDrawArrays(GL_LINE_LOOP, 0, 4);
                }
            }
        }

        if (appState.selection.dragging && appState.selection.moved)
        {
            const float rectVertices[] = {
                static_cast<float>(appState.selection.startScreen.x), static_cast<float>(appState.selection.startScreen.y),
                static_cast<float>(appState.selection.currentScreen.x), static_cast<float>(appState.selection.startScreen.y),
                static_cast<float>(appState.selection.currentScreen.x), static_cast<float>(appState.selection.currentScreen.y),
                static_cast<float>(appState.selection.startScreen.x), static_cast<float>(appState.selection.currentScreen.y)
            };

            glBindBuffer(GL_ARRAY_BUFFER, rectVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(rectVertices), rectVertices);

            glUseProgram(overlayShaderProgram);
            glUniformMatrix4fv(overlayProjLoc, 1, GL_FALSE, glm::value_ptr(screenProjection));
            glUniformMatrix4fv(overlayViewLoc, 1, GL_FALSE, glm::value_ptr(identityView));
            glUniform2f(overlayOffsetLoc, 0.0f, 0.0f);
            glBindVertexArray(rectVAO);
            glUniform4f(overlayColorLoc, 0.2f, 0.8f, 0.3f, 0.18f);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glUniform4f(overlayColorLoc, 0.4f, 1.0f, 0.5f, 1.0f);
            glDrawArrays(GL_LINE_LOOP, 0, 4);
        }

        // ---------------------------------------------------------------------
        // HUD (Heads-Up Display)
        // Draws the resource bar at the top of the screen using ImGui.
        // ---------------------------------------------------------------------
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 34.0f));
        ImGui::SetNextWindowBgAlpha(0.84f);
        ImGuiWindowFlags resourceFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("Resources", nullptr, resourceFlags);
        ImGui::SetCursorPos(ImVec2(16.0f, 9.0f));
        ImGui::Text("Wood: %d", appState.wood);
        ImGui::SameLine(120.0f);
        ImGui::Text("Food: %d", appState.food);
        ImGui::SameLine(220.0f);
        ImGui::Text("Stone: %d", appState.stone);
        ImGui::SameLine(330.0f);
        ImGui::Text("Gold: %d", appState.gold);
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // =========================================================================
    // CLEANUP
    // Free all GPU resources, shut down ImGui, and terminate GLFW.
    // Order matters: delete GPU objects before destroying the GL context.
    // =========================================================================
    for (const TextureFrame& frame : walkAnimation.frames)
    {
        glDeleteTextures(1, &frame.texture);
    }
    for (const TextureFrame& frame : idleAnimation.frames)
    {
        glDeleteTextures(1, &frame.texture);
    }
    if (pineTreeFrame.has_value())
    {
        glDeleteTextures(1, &pineTreeFrame->texture);
    }

    glDeleteVertexArrays(1, &tileVAO);
    glDeleteVertexArrays(1, &outlineVAO);
    glDeleteVertexArrays(1, &spriteVAO);
    glDeleteVertexArrays(1, &selectionVAO);
    glDeleteVertexArrays(1, &rectVAO);
    glDeleteVertexArrays(1, &pineBoundsVAO);
    glDeleteVertexArrays(1, &blockedTileVAO);
    glDeleteBuffers(1, &tileVBO);
    glDeleteBuffers(1, &outlineVBO);
    glDeleteBuffers(1, &instanceVBO);
    glDeleteBuffers(1, &spriteVBO);
    glDeleteBuffers(1, &selectionVBO);
    glDeleteBuffers(1, &rectVBO);
    glDeleteBuffers(1, &pineBoundsVBO);
    glDeleteBuffers(1, &blockedTileVBO);
    glDeleteBuffers(1, &blockedInstanceVBO);
    glDeleteProgram(tileShaderProgram);
    glDeleteProgram(spriteShaderProgram);
    glDeleteProgram(overlayShaderProgram);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    CoUninitialize();
    return 0;
} // int main()

// =============================================================================
// GLFW CALLBACKS
// Called by GLFW on input and window events. They are registered in main()
// via glfwSet*Callback and run on the main thread between glfwPollEvents calls.
// =============================================================================

// Reads keyboard state every frame and moves the camera accordingly.
// WASD and arrow keys pan the camera; Escape closes the window.
// Camera speed is scaled by (1/zoom) so panning feels consistent at all zoom levels.
void processInput(GLFWwindow* window)
{
    const float velocity = cameraSpeed * deltaTime * (1.0f / zoom);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
    {
        cameraY += velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
    {
        cameraY -= velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
    {
        cameraX -= velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
    {
        cameraX += velocity;
    }
}

// GLFW callback fired when the mouse scroll wheel moves.
// Adjusts zoom multiplicatively so scrolling feels the same at all zoom levels.
// Clamped to [0.1, 10.0] to prevent extreme values.
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    zoom += static_cast<float>(yoffset) * 0.1f * zoom;
    if (zoom < 0.25f)
    {
        zoom = 0.25f;
    }
    if (zoom > 10.0f)
    {
        zoom = 10.0f;
    }
}

// GLFW callback fired every time the mouse moves.
// Updates the stored cursor position and, if a drag is in progress, checks
// whether the cursor has moved past DRAG_THRESHOLD to activate the selection box.
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (gAppState == nullptr)
    {
        return;
    }

    gAppState->cursorScreen = glm::dvec2(xpos, ypos);
    if (gAppState->selection.dragging)
    {
        gAppState->selection.currentScreen = gAppState->cursorScreen;
        const double dragDistance = glm::length(gAppState->selection.currentScreen - gAppState->selection.startScreen);
        if (dragDistance > DRAG_THRESHOLD)
        {
            gAppState->selection.moved = true;
        }
    }
}

// GLFW callback fired on mouse button press and release.
// Left-click: begins a drag-select on press; on release either finalises the drag-select box
//             or performs a point-click selection (tree first, then villager).
// Right-click: if villager is selected, orders it to move to the clicked tile.
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (gAppState == nullptr)
    {
        return;
    }

    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        gAppState->selection.dragging = true;
        gAppState->selection.moved = false;
        gAppState->selection.startScreen = gAppState->cursorScreen;
        gAppState->selection.currentScreen = gAppState->cursorScreen;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        const glm::vec2 villagerScreenPosition = world_to_screen(gAppState->villager.position);

        if (gAppState->selection.dragging && gAppState->selection.moved)
        {
            clear_selection(*gAppState);
            gAppState->villager.selected = point_in_drag_rect(villagerScreenPosition, gAppState->selection);
        }
        else
        {
            clear_selection(*gAppState);

            int clickedTreeIndex = -1;
            for (int i = static_cast<int>(gAppState->pineTrees.size()) - 1; i >= 0; i--)
            {
                const PineTree& tree = gAppState->pineTrees[static_cast<size_t>(i)];
                const int treeIndex = tree.tile.y * GRID_SIZE + tree.tile.x;
                if (gAppState->tileVisibilities[treeIndex] > 0.0f && tree_hit_test_screen(tree, gAppState->cursorScreen, gAppState->pineTreeSpriteSize))
                {
                    clickedTreeIndex = i;
                    break;
                }
            }

            if (clickedTreeIndex >= 0)
            {
                gAppState->selectedTreeIndex = clickedTreeIndex;
                gAppState->pineTrees[static_cast<size_t>(clickedTreeIndex)].selected = true;
            }
            else
            {
                gAppState->villager.selected = villager_hit_test_screen(villagerScreenPosition, gAppState->cursorScreen);
            }
        }

        gAppState->selection.dragging = false;
        gAppState->selection.moved = false;
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS && gAppState->villager.selected)
    {
        const glm::vec2 worldTarget = screen_to_world(gAppState->cursorScreen);
        const std::optional<glm::ivec2> targetTile = world_to_tile(worldTarget);
        if (!targetTile.has_value() || is_tree_tile_blocked(*gAppState, *targetTile))
        {
            // Invalid tile — cancel all movement.
            gAppState->villager.targetPosition = gAppState->villager.position;
            gAppState->villager.waypointQueue.clear();
            gAppState->villager.moving = false;
            return;
        }

        const glm::vec2 moveTarget = tile_to_world(*targetTile);
        const bool shiftHeld = (mods & GLFW_MOD_SHIFT) != 0;

        if (shiftHeld && gAppState->villager.moving)
        {
            // Shift+RClick: append to waypoint queue.
            gAppState->villager.waypointQueue.push_back(moveTarget);
        }
        else
        {
            // Plain RClick or not yet moving: clear queue, set new immediate target.
            gAppState->villager.waypointQueue.clear();
            const glm::vec2 toTarget = moveTarget - gAppState->villager.position;
            if (glm::length(toTarget) > 1.0f)
            {
                gAppState->villager.targetPosition = moveTarget;
                gAppState->villager.facingDirection = glm::normalize(toTarget);
                gAppState->villager.moving = true;
            }
            else
            {
                gAppState->villager.targetPosition = gAppState->villager.position;
                gAppState->villager.moving = false;
            }
        }
    }
}

// GLFW callback fired when the window is resized.
// Updates the global screen dimensions and the OpenGL viewport to match the new size.
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    glViewport(0, 0, width, height);
}
