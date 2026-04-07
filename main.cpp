// =============================================================================
// INCLUDES
// =============================================================================
#include "src/Core/Engine.h"
#include "src/Core/Types.h"
#include "src/Core/Constants.h"
#include "src/Core/Globals.h"
#include "src/Math/CoordinateSystem.h"
#include "src/Graphics/RendererHelpers.h"
#include "src/Game/GameLogicHelpers.h"
#include "src/Game/Pathfinding.h"
#include "src/Game/GameLoop.h"

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
    glfwSetKeyCallback(window, key_callback);

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
    gEngine = &engine;
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
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
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

    TextureFrame tcFrame;
    if (load_texture_from_png(std::filesystem::path("assets") / "town_center" / "b_afri_town_center_age2_x1.png", tcFrame))
    {
        engine.townCenterFrame = tcFrame;
        // A 4x4 tile grid width is 8 * TILE_HALF_WIDTH. We scale the sprite to precisely span the 4x4 base.
        const float targetTCWidth = 8.0f * TILE_HALF_WIDTH;
        const float scaleRatio = targetTCWidth / static_cast<float>(engine.townCenterFrame->width);
        appState.townCenterSpriteSize = glm::vec2(targetTCWidth, static_cast<float>(engine.townCenterFrame->height) * scaleRatio);
    }
    else
    {
        std::cerr << "Failed to load town center frame b_afri_town_center_age2_x1.png\n";
    }

    if (!load_texture_from_png(std::filesystem::path("assets") / "actions_icons" / "50721_31.png", engine.buildEconomicIcon))
    {
        std::cerr << "Failed to load build economic icon 50721_31.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "actions_icons" / "50721_32.png", engine.buildMilitaryIcon))
    {
        std::cerr << "Failed to load build military icon 50721_32.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "actions_icons" / "50721_29.png", engine.repairIcon))
    {
        std::cerr << "Failed to load repair icon 50721_29.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "actions_icons" / "50721_70.png", engine.garrisonIcon))
    {
        std::cerr << "Failed to load garrison icon 50721_70.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "actions_icons" / "50721_04.png", engine.stopIcon))
    {
        std::cerr << "Failed to load stop icon 50721_04.png\n";
    }

    // Load economic building icons
    if (!load_texture_from_png(std::filesystem::path("assets") / "buildings_icons" / "50705_35.png", engine.houseIcon))
    {
        std::cerr << "Failed to load house icon 50705_35.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "buildings_icons" / "50705_20.png", engine.millIcon))
    {
        std::cerr << "Failed to load mill icon 50705_20.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "buildings_icons" / "50705_40.png", engine.miningCampIcon))
    {
        std::cerr << "Failed to load mining camp icon 50705_40.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "buildings_icons" / "50705_41.png", engine.lumberCampIcon))
    {
        std::cerr << "Failed to load lumber camp icon 50705_41.png\n";
    }

    // Load military building icons
    if (!load_texture_from_png(std::filesystem::path("assets") / "buildings_icons" / "50705_03.png", engine.barracksIcon))
    {
        std::cerr << "Failed to load barracks icon 50705_03.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "buildings_icons" / "50705_01.png", engine.archeryRangeIcon))
    {
        std::cerr << "Failed to load archery range icon 50705_01.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "buildings_icons" / "50705_24.png", engine.stableIcon))
    {
        std::cerr << "Failed to load stable icon 50705_24.png\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "buildings_icons" / "50705_23.png", engine.siegeWorkshopIcon))
    {
        std::cerr << "Failed to load siege workshop icon 50705_23.png\n";
    }

    // Load builder animation
    engine.builderAnimation.frames = load_frame_directory(std::filesystem::path("assets") / "u_vil_male_builder_taskA_x2.sld");
    if (engine.builderAnimation.frames.empty())
    {
        std::cerr << "No builder task frames were loaded\n";
    }

    // Load house construction stage sprites
    if (!load_texture_from_png(std::filesystem::path("assets") / "house" / "image_1x1_0.png", engine.houseStage0))
    {
        std::cerr << "Failed to load house stage 0\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "house" / "image_1x1_1.png", engine.houseStage1))
    {
        std::cerr << "Failed to load house stage 1\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "house" / "image_1x1_2.png", engine.houseStage2))
    {
        std::cerr << "Failed to load house stage 2\n";
    }
    if (!load_texture_from_png(std::filesystem::path("assets") / "house" / "image_1x1_3.png", engine.houseStage3))
    {
        std::cerr << "Failed to load house stage 3\n";
    }

    // Scale house sprite to span 2x2 tiles (same as town center but half the width/height)
    if (engine.houseStage3.texture != 0)
    {
        const float targetHouseWidth = 4.0f * TILE_HALF_WIDTH; // 2 tiles wide
        const float scaleRatio = targetHouseWidth / static_cast<float>(engine.houseStage3.width);
        appState.houseSpriteSize = glm::vec2(targetHouseWidth, static_cast<float>(engine.houseStage3.height) * scaleRatio);
    }

    // Create garrison cursor - try custom cursor first, fall back to ARROW cursor for testing
    gGarrisonCursor = create_cursor_from_png(std::filesystem::path("assets") / "actions_icons" / "50721_70.png", 0, 0);
    if (gGarrisonCursor == nullptr)
    {
        std::cerr << "Failed to create custom garrison cursor, using ARROW cursor\n";
        gGarrisonCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    }
    if (gGarrisonCursor == nullptr)
    {
        std::cerr << "Failed to create even standard cursor!\n";
    }

    // -------------------------------------------------------------------------
    // Tree Placement
    // Scatter pine trees pseudo-randomly across the map using a hash of (x, y).
    // A clear radius of 18 tiles around the center is kept free so the villager
    // spawns in an open area and can move without immediately being blocked.
    // ~4% of eligible tiles outside the radius get a tree.
    // -------------------------------------------------------------------------
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
                tree.uuid = gNextUuid++;
                tree.tile = glm::ivec2(x, y);
                tree.position = tile_to_world(tree.tile);
                appState.pineTrees[tree.uuid] = tree;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Player Starting Units Placement
    // Spawn 1 Town Center and 3 Villagers
    // -------------------------------------------------------------------------
    TownCenter tc;
    tc.uuid = gNextUuid++;
    tc.tile = glm::ivec2(GRID_SIZE / 2, GRID_SIZE / 2);
    // Align base 4x4 center accurately in world-space
    float cx = static_cast<float>(tc.tile.x) + 1.5f;
    float cy = static_cast<float>(tc.tile.y) + 1.5f;
    tc.position = tile_to_world(glm::vec2(cx, cy));
    tc.hasGatherPoint = true;
    tc.gatherPoint = tile_to_world(glm::ivec2(tc.tile.x - 1, tc.tile.y + 6));
    appState.townCenters[tc.uuid] = tc;

    for (int i = 0; i < 3; i++)
    {
        Villager v;
        v.uuid = gNextUuid++;
        // Spawn them a few tiles south of the Town Center, which is 4x4 and ends at +3
        glm::ivec2 vTile = glm::ivec2((GRID_SIZE / 2) - 1 + i, (GRID_SIZE / 2) + 5);
        v.position = tile_to_world(vTile);
        v.targetPosition = v.position;
        appState.villagers[v.uuid] = v;
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
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // Villager hitbox ellipse (tall/narrow to match villager sprite proportions)
    // Sprite is 72x72 centered at (v.x, v.y - 24), so height=72, width=72
    // Hitbox should cover head to feet: rx=22 (half of sprite half-width 36), ry=36 (half of sprite height 72)
    std::vector<float> hitboxCircle;
    constexpr int hitboxSegments = 32;
    constexpr float hitboxRadiusX = 22.0f;
    constexpr float hitboxRadiusY = 36.0f;
    hitboxCircle.reserve(hitboxSegments * 2);
    for (int i = 0; i < hitboxSegments; i++)
    {
        const float angle = (static_cast<float>(i) / static_cast<float>(hitboxSegments)) * 2.0f * 3.14159265f;
        hitboxCircle.push_back(std::cos(angle) * hitboxRadiusX);
        hitboxCircle.push_back(std::sin(angle) * hitboxRadiusY);
    }
    glGenVertexArrays(1, &engine.gpu.hitboxVAO);
    glGenBuffers(1, &engine.gpu.hitboxVBO);
    glBindVertexArray(engine.gpu.hitboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.hitboxVBO);
    glBufferData(GL_ARRAY_BUFFER, hitboxCircle.size() * sizeof(float), hitboxCircle.data(), GL_STATIC_DRAW);
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
    glDeleteTextures(1, &engine.buildEconomicIcon.texture);
    glDeleteTextures(1, &engine.buildMilitaryIcon.texture);
    glDeleteTextures(1, &engine.repairIcon.texture);
    glDeleteTextures(1, &engine.garrisonIcon.texture);
    glDeleteTextures(1, &engine.stopIcon.texture);
    glDeleteTextures(1, &engine.houseIcon.texture);
    glDeleteTextures(1, &engine.millIcon.texture);
    glDeleteTextures(1, &engine.miningCampIcon.texture);
    glDeleteTextures(1, &engine.lumberCampIcon.texture);
    glDeleteTextures(1, &engine.barracksIcon.texture);
    glDeleteTextures(1, &engine.archeryRangeIcon.texture);
    glDeleteTextures(1, &engine.stableIcon.texture);
    glDeleteTextures(1, &engine.siegeWorkshopIcon.texture);

    if (gGarrisonCursor != nullptr)
    {
        glfwDestroyCursor(gGarrisonCursor);
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
