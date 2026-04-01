// =============================================================================
// INCLUDES
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

#include <iostream>
#include <vector>

#include "src/Core/Types.h"
#include "src/Core/Constants.h"
#include "src/Core/Globals.h"
#include "src/Core/Engine.h"
#include "src/Math/CoordinateSystem.h"
#include "src/Graphics/RendererHelpers.h"
#include "src/Game/GameLogicHelpers.h"
#include "src/Game/Pathfinding.h"
#include "src/Game/GameLoop.h"


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


// Shader files are dynamically loaded at runtime avoiding large hardcoded string definitions.

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

    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
    SCR_WIDTH = mode->width;
    SCR_HEIGHT = mode->height;
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "AoE2 Clone - Isometric Platform", primaryMonitor, nullptr);
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
    EngineState engine;
    appState.explored.resize(GRID_SIZE * GRID_SIZE, false);
    appState.visible.resize(GRID_SIZE * GRID_SIZE, false);
    appState.tileVisibilities.resize(GRID_SIZE * GRID_SIZE, 0.0f);
    appState.minimapPixels.resize(GRID_SIZE * GRID_SIZE, 0xFF000000); // Opaque black background

    glGenTextures(1, &appState.minimapTexture);
    glBindTexture(GL_TEXTURE_2D, appState.minimapTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Setting up the texture using RGBA format. 
    // Data is provided directly from our CPU-side pixel buffer, which we'll update and push via glTexSubImage2D later.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GRID_SIZE, GRID_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, appState.minimapPixels.data());

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

    engine.gpu.tileShaderProgram = create_program_from_files("src/shaders/tile.vs", "src/shaders/tile.fs");
    engine.gpu.spriteShaderProgram = create_program_from_files("src/shaders/sprite.vs", "src/shaders/sprite.fs");
    engine.gpu.overlayShaderProgram = create_program_from_files("src/shaders/overlay.vs", "src/shaders/overlay.fs");

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
    engine.translations.reserve(GRID_SIZE * GRID_SIZE);

    for (int y = 0; y < GRID_SIZE; y++)
    {
        for (int x = 0; x < GRID_SIZE; x++)
        {
            const float screenX = static_cast<float>(x - y) * TILE_HALF_WIDTH;
            const float screenY = -static_cast<float>(x + y) * TILE_HALF_HEIGHT;
            engine.translations.push_back(glm::vec2(screenX, screenY));
        }
    }

    // -------------------------------------------------------------------------
    // Asset Loading
    // Load all sprite PNGs from disk into GPU textures.
    // Walk animation: 480 frames (16 directions x 30 frames each).
    // Idle animation: one frame per direction.
    // Pine tree: a single static frame (frame index 4 from the SLD folder).
    // -------------------------------------------------------------------------
    engine.walkAnimation.frames = load_frame_directory(std::filesystem::path("assets") / "u_vil_male_villager_walkA_x1.sld");
    engine.idleAnimation.frames = load_frame_directory(std::filesystem::path("assets") / "u_vil_male_villager_idleA_x1.sld");
    if (engine.walkAnimation.frames.empty())
    {
        std::cerr << "No villager walk frames were loaded\n";
    }
    if (engine.idleAnimation.frames.empty())
    {
        std::cerr << "No villager idle frames were loaded\n";
    }

    engine.pineTreeFrame = load_frame_by_index(std::filesystem::path("assets") / "n_tree_pine_x1.sld", 4);
    if (!engine.pineTreeFrame.has_value())
    {
        std::cerr << "Failed to load pine tree frame image_1x1_04.png\n";
    }

    engine.townCenterFrame = load_frame_by_index(std::filesystem::path("assets") / "b_dark_town_center_age1_x2.sld", 0);
    if (!engine.townCenterFrame.has_value())
    {
        std::cerr << "Failed to load town center frame image_1x1_0.png\n";
    }
    else
    {
        // A 4x4 tile grid width is 8 * TILE_HALF_WIDTH. We scale the sprite to precisely span the 4x4 base.
        const float targetTCWidth = 8.0f * TILE_HALF_WIDTH;
        const float scaleRatio = targetTCWidth / static_cast<float>(engine.townCenterFrame->width);
        appState.townCenterSpriteSize = glm::vec2(targetTCWidth, static_cast<float>(engine.townCenterFrame->height) * scaleRatio);
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

    // -------------------------------------------------------------------------
    // Player Starting Units Placement
    // Spawn 1 Town Center and 3 Villagers
    // -------------------------------------------------------------------------
    TownCenter tc;
    tc.tile = glm::ivec2(GRID_SIZE / 2, GRID_SIZE / 2);
    // Align base 4x4 center accurately in world-space
    float cx = static_cast<float>(tc.tile.x) + 1.5f;
    float cy = static_cast<float>(tc.tile.y) + 1.5f;
    tc.position = tile_to_world(glm::vec2(cx, cy));
    tc.hasGatherPoint = true;
    tc.gatherPoint = tile_to_world(glm::ivec2(tc.tile.x - 1, tc.tile.y + 6));
    appState.townCenters.push_back(tc);

    for (int i = 0; i < 3; i++)
    {
        Villager v;
        // Spawn them a few tiles south of the Town Center, which is 4x4 and ends at +3
        glm::ivec2 vTile = glm::ivec2((GRID_SIZE / 2) - 1 + i, (GRID_SIZE / 2) + 5);
        v.position = tile_to_world(vTile);
        v.targetPosition = v.position;
        appState.villagers.push_back(v);
    }
    
    // Jump camera to Town Center
    cameraX = tc.position.x;
    cameraY = tc.position.y;
    engine.blockedTileTranslations = blocked_tile_translations(appState);

    // -------------------------------------------------------------------------
    // GPU Buffer Setup
    // Upload all geometry into OpenGL VAOs and VBOs. Everything is static
    // (GL_STATIC_DRAW) except the drag-select rectangle which is updated every
    // frame the user is dragging (GL_DYNAMIC_DRAW).
    // VAOs store the vertex layout; VBOs store the actual vertex data.
    // -------------------------------------------------------------------------
    glGenVertexArrays(1, &engine.gpu.tileVAO);
    glGenBuffers(1, &engine.gpu.tileVBO);
    glGenVertexArrays(1, &engine.gpu.outlineVAO);
    glGenBuffers(1, &engine.gpu.outlineVBO);
    glGenBuffers(1, &engine.gpu.instanceVBO);
    glGenBuffers(1, &engine.gpu.visibilityVBO);
    glGenVertexArrays(1, &engine.gpu.blockedTileVAO);
    glGenBuffers(1, &engine.gpu.blockedTileVBO);
    glGenBuffers(1, &engine.gpu.blockedInstanceVBO);

    glBindVertexArray(engine.gpu.tileVAO);
    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.tileVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tileVertices), tileVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, engine.translations.size() * sizeof(glm::vec2), engine.translations.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    glVertexAttribDivisor(1, 1);

    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.visibilityVBO);
    glBufferData(GL_ARRAY_BUFFER, appState.tileVisibilities.size() * sizeof(float), appState.tileVisibilities.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(engine.gpu.outlineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.outlineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tileOutline), tileOutline, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.instanceVBO);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    glVertexAttribDivisor(1, 1);

    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.visibilityVBO);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
    glVertexAttribDivisor(2, 1);

    glBindVertexArray(engine.gpu.blockedTileVAO);
    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.blockedTileVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tileVertices), tileVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.blockedInstanceVBO);
    glBufferData(GL_ARRAY_BUFFER, engine.blockedTileTranslations.size() * sizeof(glm::vec2), engine.blockedTileTranslations.data(), GL_STATIC_DRAW);
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

    glGenVertexArrays(1, &engine.gpu.spriteVAO);
    glGenBuffers(1, &engine.gpu.spriteVBO);
    glBindVertexArray(engine.gpu.spriteVAO);
    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.spriteVBO);
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

    glGenVertexArrays(1, &engine.gpu.selectionVAO);
    glGenBuffers(1, &engine.gpu.selectionVBO);
    glBindVertexArray(engine.gpu.selectionVAO);
    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.selectionVBO);
    glBufferData(GL_ARRAY_BUFFER, selectionCircle.size() * sizeof(float), selectionCircle.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glGenVertexArrays(1, &engine.gpu.rectVAO);
    glGenBuffers(1, &engine.gpu.rectVBO);
    glBindVertexArray(engine.gpu.rectVAO);
    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.rectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // Cache uniform locations for fast access inside the render loop.
    // glGetUniformLocation is slow — do it once here rather than every frame.
    engine.gpu.tileColorLoc = glGetUniformLocation(engine.gpu.tileShaderProgram, "uColor");
    engine.gpu.tileProjLoc = glGetUniformLocation(engine.gpu.tileShaderProgram, "uProjection");
    engine.gpu.tileViewLoc = glGetUniformLocation(engine.gpu.tileShaderProgram, "uView");

    engine.gpu.spriteProjLoc = glGetUniformLocation(engine.gpu.spriteShaderProgram, "uProjection");
    engine.gpu.spriteViewLoc = glGetUniformLocation(engine.gpu.spriteShaderProgram, "uView");
    engine.gpu.spritePosLoc = glGetUniformLocation(engine.gpu.spriteShaderProgram, "uSpritePos");
    engine.gpu.spriteSizeLoc = glGetUniformLocation(engine.gpu.spriteShaderProgram, "uSpriteSize");
    engine.gpu.spriteVisLoc = glGetUniformLocation(engine.gpu.spriteShaderProgram, "uVisibility");

    engine.gpu.overlayProjLoc = glGetUniformLocation(engine.gpu.overlayShaderProgram, "uProjection");
    engine.gpu.overlayViewLoc = glGetUniformLocation(engine.gpu.overlayShaderProgram, "uView");
    engine.gpu.overlayOffsetLoc = glGetUniformLocation(engine.gpu.overlayShaderProgram, "uOffset");
    engine.gpu.overlayColorLoc = glGetUniformLocation(engine.gpu.overlayShaderProgram, "uColor");

    glUseProgram(engine.gpu.spriteShaderProgram);
    glUniform1i(glGetUniformLocation(engine.gpu.spriteShaderProgram, "uTexture"), 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const glm::vec2 spriteScale(72.0f, 72.0f);
    const glm::vec2 pineTreeScale = engine.pineTreeFrame.has_value()
        ? glm::vec2(static_cast<float>(engine.pineTreeFrame->width), static_cast<float>(engine.pineTreeFrame->height))
        : glm::vec2(108.0f, 162.0f);
    appState.pineTreeSpriteSize = pineTreeScale;

    const float spriteBoundsOutline[] = {
        -0.5f * pineTreeScale.x, 0.0f,
         0.5f * pineTreeScale.x, 0.0f,
         0.5f * pineTreeScale.x, pineTreeScale.y,
        -0.5f * pineTreeScale.x, pineTreeScale.y
    };

    glGenVertexArrays(1, &engine.gpu.pineBoundsVAO);
    glGenBuffers(1, &engine.gpu.pineBoundsVBO);
    glBindVertexArray(engine.gpu.pineBoundsVAO);
    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.pineBoundsVBO);
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

        UpdateSimulation(engine, appState);
        RenderScene(engine, appState);
        RenderUI(engine, appState);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    // CLEANUP
    // Free all GPU resources, shut down ImGui, and terminate GLFW.
    // Order matters: delete GPU objects before destroying the GL context.
    // =========================================================================
    for (const TextureFrame& frame : engine.walkAnimation.frames)
    {
        glDeleteTextures(1, &frame.texture);
    }
    for (const TextureFrame& frame : engine.idleAnimation.frames)
    {
        glDeleteTextures(1, &frame.texture);
    }
    if (engine.pineTreeFrame.has_value())
    {
        glDeleteTextures(1, &engine.pineTreeFrame->texture);
    }

    glDeleteVertexArrays(1, &engine.gpu.tileVAO);
    glDeleteVertexArrays(1, &engine.gpu.outlineVAO);
    glDeleteVertexArrays(1, &engine.gpu.spriteVAO);
    glDeleteVertexArrays(1, &engine.gpu.selectionVAO);
    glDeleteVertexArrays(1, &engine.gpu.rectVAO);
    glDeleteVertexArrays(1, &engine.gpu.pineBoundsVAO);
    glDeleteVertexArrays(1, &engine.gpu.blockedTileVAO);
    glDeleteBuffers(1, &engine.gpu.tileVBO);
    glDeleteBuffers(1, &engine.gpu.outlineVBO);
    glDeleteBuffers(1, &engine.gpu.instanceVBO);
    glDeleteBuffers(1, &engine.gpu.spriteVBO);
    glDeleteBuffers(1, &engine.gpu.selectionVBO);
    glDeleteBuffers(1, &engine.gpu.rectVBO);
    glDeleteBuffers(1, &engine.gpu.pineBoundsVBO);
    glDeleteBuffers(1, &engine.gpu.blockedTileVBO);
    glDeleteBuffers(1, &engine.gpu.blockedInstanceVBO);
    glDeleteProgram(engine.gpu.tileShaderProgram);
    glDeleteProgram(engine.gpu.spriteShaderProgram);
    glDeleteProgram(engine.gpu.overlayShaderProgram);
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
        if (gAppState->selection.dragging && gAppState->selection.moved)
        {
            clear_selection(*gAppState);
            for (Villager& v : gAppState->villagers)
            {
                if (v.isGarrisoned) continue;
                const glm::vec2 vsp = world_to_screen(v.position);
                v.selected = point_in_drag_rect(vsp, gAppState->selection);
            }
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
                int clickedTCIndex = -1;
                for (int i = 0; i < static_cast<int>(gAppState->townCenters.size()); ++i)
                {
                    const TownCenter& tc = gAppState->townCenters[i];
                    const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
                    if (gAppState->tileVisibilities[tcIndex] > 0.0f && town_center_hit_test_screen(tc, gAppState->cursorScreen, gAppState->townCenterSpriteSize))
                    {
                        clickedTCIndex = i;
                        break;
                    }
                }
                
                if (clickedTCIndex >= 0)
                {
                    gAppState->townCenters[static_cast<size_t>(clickedTCIndex)].selected = true;
                }
                else
                {
                    for (Villager& v : gAppState->villagers)
                    {
                        if (v.isGarrisoned) continue;
                        const glm::vec2 vsp = world_to_screen(v.position);
                        if (villager_hit_test_screen(vsp, gAppState->cursorScreen))
                        {
                            v.selected = true;
                            break; // Select only one villager on a single click
                        }
                    }
                }
            }
        }

        gAppState->selection.dragging = false;
        gAppState->selection.moved = false;
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    {
        bool anySelected = false;
        for (const Villager& v : gAppState->villagers) 
        {
            if (v.selected && !v.isGarrisoned) anySelected = true;
        }

        TownCenter* selectedTC = nullptr;
        if (!anySelected)
        {
            for (TownCenter& tc : gAppState->townCenters)
            {
                if (tc.selected)
                {
                    selectedTC = &tc;
                    break;
                }
            }
        }

        if (anySelected)
        {
            int garrisonTCIndex = -1;
            if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS)
            {
                for (int i = 0; i < static_cast<int>(gAppState->townCenters.size()); ++i)
                {
                    const TownCenter& tc = gAppState->townCenters[i];
                    const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
                    if (gAppState->tileVisibilities[tcIndex] > 0.0f && town_center_hit_test_screen(tc, gAppState->cursorScreen, gAppState->townCenterSpriteSize))
                    {
                        garrisonTCIndex = i;
                        break;
                    }
                }
            }

            if (garrisonTCIndex >= 0)
            {
                const TownCenter& targetTc = gAppState->townCenters[garrisonTCIndex];
                for (Villager& v : gAppState->villagers)
                {
                    if (!v.selected || v.isGarrisoned) continue;
                    
                    float bestDist = 1e9f;
                    glm::vec2 bestTarget = targetTc.position;
                    
                    for (int x = -1; x <= 4; ++x) {
                        for (int y = -1; y <= 4; ++y) {
                            if (x >= 0 && x <= 3 && y >= 0 && y <= 3) continue;
                            glm::ivec2 p(targetTc.tile.x + x, targetTc.tile.y + y);
                            if (p.x >= 0 && p.x < GRID_SIZE && p.y >= 0 && p.y < GRID_SIZE && !is_tile_blocked(*gAppState, p)) {
                                float dist = glm::length(tile_to_world(p) - v.position);
                                if (dist < bestDist) {
                                    bestDist = dist;
                                    bestTarget = tile_to_world(p);
                                }
                            }
                        }
                    }

                    std::vector<glm::vec2> path = find_path(*gAppState, v.position, bestTarget);
                    v.waypointQueue.clear();
                    v.isMovingToGarrison = true;
                    v.targetTcIndex = garrisonTCIndex;
                    if (path.size() > 1) {
                        v.targetPosition = path[1];
                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                        v.moving = true;
                        for (size_t i = 2; i < path.size(); ++i) {
                            v.waypointQueue.push_back(path[i]);
                        }
                    } else if (path.size() == 1 && glm::length(path[0] - v.position) > 1.0f) {
                        v.targetPosition = path[0];
                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                        v.moving = true;
                    } else {
                        if (targetTc.garrisonCount < targetTc.maxGarrison) {
                            gAppState->townCenters[garrisonTCIndex].garrisonCount++;
                            v.isGarrisoned = true;
                            v.garrisonTcIndex = garrisonTCIndex;
                            v.selected = false;
                            v.moving = false;
                            v.isMovingToGarrison = false;
                        }
                    }
                }
            }
            else
            {
                const glm::vec2 worldTarget = screen_to_world(gAppState->cursorScreen);
                const std::optional<glm::ivec2> targetTile = world_to_tile(worldTarget);
                if (!targetTile.has_value() || is_tile_blocked(*gAppState, *targetTile))
            {
                // Invalid tile — cancel all movement.
                for (Villager& v : gAppState->villagers)
                {
                    if (v.selected)
                    {
                        v.targetPosition = v.position;
                        v.waypointQueue.clear();
                        v.moving = false;
                    }
                }
                return;
            }

            int selectedCount = 0;
            for (const Villager& v : gAppState->villagers)
            {
                if (v.selected) selectedCount++;
            }
            
            std::vector<glm::ivec2> groupDestinations = find_group_destinations(*gAppState, *targetTile, selectedCount);
            const bool shiftHeld = (mods & GLFW_MOD_SHIFT) != 0;

            int destIndex = 0;
            for (Villager& v : gAppState->villagers)
            {
                if (!v.selected) continue;

                glm::vec2 finalTarget = (destIndex < groupDestinations.size()) 
                    ? tile_to_world(groupDestinations[destIndex++]) 
                    : tile_to_world(*targetTile);
                
                glm::vec2 startPos = v.position;
                if (shiftHeld && v.moving)
                {
                    startPos = v.waypointQueue.empty() ? v.targetPosition : v.waypointQueue.back();
                }

                std::vector<glm::vec2> path = find_path(*gAppState, startPos, finalTarget);

                if (shiftHeld && v.moving)
                {
                    if (!path.empty())
                    {
                        // Skip the first node as it matches startPos exactly
                        for (size_t i = 1; i < path.size(); ++i)
                        {
                            v.waypointQueue.push_back(path[i]);
                        }
                    }
                }
                else
                {
                    v.waypointQueue.clear();
                    if (path.size() > 1) // First node is startPos, so path must have >= 2 to move
                    {
                        v.targetPosition = path[1];
                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                        v.moving = true;
                        for (size_t i = 2; i < path.size(); ++i)
                        {
                            v.waypointQueue.push_back(path[i]);
                        }
                    }
                    else if (path.size() == 1 && glm::length(path[0] - v.position) > 1.0f)
                    {
                        // Immediate adjacent or floating point move
                        v.targetPosition = path[0];
                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                        v.moving = true;
                    }
                    else
                    {
                        v.targetPosition = v.position;
                        v.moving = false;
                    }
                }
            }
        }
        }
        else if (selectedTC)
        {
            int clickedTCIndex = -1;
            for (int i = 0; i < static_cast<int>(gAppState->townCenters.size()); ++i)
            {
                const TownCenter& tc = gAppState->townCenters[i];
                const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
                if (gAppState->tileVisibilities[tcIndex] > 0.0f && town_center_hit_test_screen(tc, gAppState->cursorScreen, gAppState->townCenterSpriteSize))
                {
                    clickedTCIndex = i;
                    break;
                }
            }
            
            if (clickedTCIndex >= 0 && &gAppState->townCenters[clickedTCIndex] == selectedTC) {
                selectedTC->gatherPointIsSelf = true;
                selectedTC->hasGatherPoint = false;
            } else {
                selectedTC->gatherPointIsSelf = false;
                const glm::vec2 worldTarget = screen_to_world(gAppState->cursorScreen);
                const std::optional<glm::ivec2> targetTile = world_to_tile(worldTarget);
                if (targetTile.has_value() && !is_tile_blocked(*gAppState, *targetTile))
                {
                    selectedTC->hasGatherPoint = true;
                    selectedTC->gatherPoint = tile_to_world(*targetTile);
                }
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
