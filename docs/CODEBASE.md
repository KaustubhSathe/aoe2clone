# AoE2 Clone - Codebase Documentation

A comprehensive technical documentation of the AoE2 Clone real-time strategy game project.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Directory Structure](#2-directory-structure)
3. [Source Files Reference](#3-source-files-reference)
4. [Build System](#4-build-system)
5. [Rendering Architecture](#5-rendering-architecture)
6. [Game Engine Architecture](#6-game-engine-architecture)
7. [Coordinate System](#7-coordinate-system)
8. [Game Systems](#8-game-systems)
9. [Dependencies](#9-dependencies)
10. [Testing](#10-testing)

---

## 1. Project Overview

**AoE2 Clone** is a real-time strategy (RTS) game inspired by Age of Empires II, built from scratch using modern C++ and graphics APIs.

### Key Features

- **Isometric 2D rendering** with depth-sorted sprites
- **Grid-based map** (200x200 tiles) with fog of war
- **Unit management** - villagers with pathfinding, selection, and commands
- **Building system** - town centers, houses with construction stages
- **Real-time UI** with resource tracking, population, and selection info
- **Minimap** with real-time updates
- **Keyboard shortcuts** for building and unit commands

### Technical Stack

| Component | Technology |
|-----------|------------|
| Language | C++20 |
| Graphics API | OpenGL 3.3 Core Profile |
| Window/Input | GLFW 3.4 |
| UI Framework | Dear ImGui v1.91.9 |
| Math Library | GLM |
| Testing | Google Test v1.16.0 |
| Platform | Windows |

---

## 2. Directory Structure

```
aoe2clone/
├── CMakeLists.txt              # Build system configuration
├── IMPLEMENTATION_PLAN.md      # Development roadmap
├── main.cpp                    # Application entry point
├── imgui.ini                   # ImGui UI configuration
│
├── src/
│   ├── Core/                   # Engine core components
│   │   ├── Engine.h
│   │   ├── Engine.cpp
│   │   ├── Globals.h
│   │   ├── Globals.cpp
│   │   ├── Types.h
│   │   └── Constants.h
│   ├── Math/
│   │   ├── CoordinateSystem.h
│   │   └── CoordinateSystem.cpp
│   ├── Graphics/
│   │   ├── RendererHelpers.h
│   │   └── RendererHelpers.cpp
│   ├── Game/
│   │   ├── GameLoop.h
│   │   ├── GameLoop.cpp
│   │   ├── GameLogicHelpers.h
│   │   ├── GameLogicHelpers.cpp
│   │   ├── Pathfinding.h
│   │   └── Pathfinding.cpp
│   └── shaders/
│       ├── tile.vs             # Tile vertex shader
│       ├── tile.fs             # Tile fragment shader
│       ├── sprite.vs           # Sprite vertex shader
│       ├── sprite.fs           # Sprite fragment shader
│       ├── overlay.vs          # Overlay vertex shader
│       └── overlay.fs          # Overlay fragment shader
│
├── third_party/
│   └── glad/                   # OpenGL loader (pre-generated)
│
├── tests/
│   ├── CoordinateSystem_test.cpp
│   ├── GameLogicHelpers_test.cpp
│   └── Pathfinding_test.cpp
│
└── assets/                     # Game sprites and icons
    ├── u_vil_male_*            # Villager animations
    ├── n_tree_*                # Pine tree sprites
    ├── town_center/            # Town center sprites
    ├── house/                  # House construction stages
    ├── actions_icons/          # UI action icons
    └── buildings_icons/        # Building selection icons
```

---

## 3. Source Files Reference

### 3.1 Core Module (`src/Core/`)

| File | Description |
|------|-------------|
| **Types.h** | Core data structures: `Villager`, `TownCenter`, `House`, `PineTree`, `AppState`, `TextureFrame`, `EntityId`, and enums |
| **Constants.h** | Game constants: grid size (200x200), tile dimensions (48x24), animation FPS, resource costs |
| **Globals.h/cpp** | Global state variables: camera position, zoom level, delta time, pointers to `AppState` and `EngineState` |
| **Engine.h/cpp** | GLFW callbacks (mouse, keyboard, resize), input processing, garrison/build modes |

### 3.2 Math Module (`src/Math/`)

| File | Description |
|------|-------------|
| **CoordinateSystem.h/cpp** | Isometric coordinate transformations: `tile_to_world()`, `world_to_tile()`, `screen_to_world()`, `world_to_screen()` |

### 3.3 Graphics Module (`src/Graphics/`)

| File | Description |
|------|-------------|
| **RendererHelpers.h/cpp** | OpenGL shader compilation, WIC texture loading (Windows), PNG loading, cursor creation, animation index calculations |

### 3.4 Game Module (`src/Game/`)

| File | Description |
|------|-------------|
| **GameLoop.h/cpp** | `UpdateSimulation()` - game logic; `RenderScene()` - isometric rendering; `RenderUI()` - ImGui panels |
| **GameLogicHelpers.h/cpp** | Hit testing, selection box, tile blocking, drag selection rectangle |
| **Pathfinding.h/cpp** | A* pathfinding algorithm, diagonal movement support, group destination calculation |

### 3.5 Main Entry Point

| File | Description |
|------|-------------|
| **main.cpp** | Initializes GLFW, GLAD, ImGui, loads assets, sets up GPU buffers, runs game loop |

---

## 4. Build System

### CMakeLists.txt Configuration

```cmake
cmake_minimum_required(VERSION 3.14)
project(AOE2CLONE VERSION 1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
```

### Dependencies (FetchContent)

| Dependency | Version | Purpose |
|------------|---------|---------|
| Google Test | v1.16.0 | Unit testing framework |
| GLM | latest | Vector/matrix math |
| GLFW | 3.4 | Window/input handling |
| Dear ImGui | v1.91.9 | UI library |

### Local Dependencies

| Dependency | Location | Purpose |
|------------|----------|---------|
| GLAD | `third_party/glad/` | OpenGL 3.3 function loader |

### Build Targets

1. **aoe2** - Main game executable
2. **aoe2_tests** - Unit test executable

### Link Libraries

- `imgui`
- `glfw`
- `glm::glm`
- `ole32` (Windows COM)
- `windowscodecs` (WIC texture loading)

---

## 5. Rendering Architecture

### 5.1 Graphics Pipeline

The renderer uses modern OpenGL 3.3 with a multi-pass approach:

```
┌─────────────────────────────────────────────────────────────┐
│                    Render Per Frame                         │
├─────────────────────────────────────────────────────────────┤
│  1. Clear - Black background                                │
│  2. Render Tiles - 200x200 isometric grid (instanced)      │
│  3. Render Blocked Tiles - Dark green (trees/buildings)    │
│  4. Render Tile Outlines - Green borders                   │
│  5. Render Sprites - Depth-sorted back-to-front:           │
│     - Pine trees                                            │
│     - Town centers                                          │
│     - Houses                                               │
│     - Villagers                                            │
│  6. Render Overlays - Selections, waypoints, health bars   │
│  7. Render Selection Box - Screen-space drag rectangle     │
│  8. Render ImGui UI - Top bar, bottom panel, minimap       │
└─────────────────────────────────────────────────────────────┘
```

### 5.2 Shaders

#### Tile Shader (`tile.vs` / `tile.fs`)

Renders isometric diamond tiles using **instanced rendering** for performance.

```glsl
// Vertex attributes: 3 VBOs
// - aPos: Base tile position
// - aOffset: Instance offset (tile grid position)
// - aVisibility: Fog of war value (1.0, 0.5, or 0.0)
gl_Position = uProjection * uView * vec4(aPos + aOffset, 0.0, 1.0);
```

#### Sprite Shader (`sprite.vs` / `sprite.fs`)

Renders textured quads for units and buildings.

```glsl
// Fragment: Samples texture and applies fog of war visibility
FragColor = texture(uTexture, vUV) * vec4(uVisibility, uVisibility, uVisibility, 1.0);
```

#### Overlay Shader (`overlay.vs` / `overlay.fs`)

Renders selection circles, waypoints, and health bars.

### 5.3 Texture Loading

- Uses **Windows WIC** (Windows Imaging Component) via COM
- Loads PNG files and converts to OpenGL textures
- Format: `GL_RGBA8` (8 bits per channel)
- Supports sprite sheets via frame indexing

### 5.4 Depth Sorting

Entities are sorted by Y coordinate each frame and rendered back-to-front:
- Higher Y values = closer to camera = rendered later (on top)

---

## 6. Game Engine Architecture

### 6.1 Game Loop Structure

```cpp
while (!glfwWindowShouldClose(window)) {
    // Calculate delta time
    deltaTime = currentFrame - lastFrame;

    // Process input
    processInput(window);

    // ImGui frame setup
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Update and render
    UpdateSimulation(engine, appState);  // Game logic
    RenderScene(engine, appState);       // OpenGL rendering
    RenderUI(engine, appState);          // ImGui UI

    // ImGui render
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Swap buffers and poll events
    glfwSwapBuffers(window);
    glfwPollEvents();
}
```

### 6.2 State Management

#### Global State (`Globals.cpp`)

| Variable | Type | Purpose |
|----------|------|---------|
| `gAppState` | `AppState*` | Pointer to game state |
| `gEngine` | `EngineState*` | Pointer to rendering state |
| `cameraX/cameraY` | `float` | Camera position in world space |
| `zoom` | `float` | Camera zoom (0.25x - 10x) |
| `deltaTime` | `float` | Frame delta time in seconds |

#### AppState Structure

```cpp
struct AppState {
    // Entity containers
    std::unordered_map<EntityId, Villager> villagers;
    std::unordered_map<EntityId, PineTree> pineTrees;
    std::unordered_map<EntityId, TownCenter> townCenters;
    std::unordered_map<EntityId, House> houses;

    // Resources
    int food, wood, stone, gold;
    int maxPopulation;
    int currentAge;

    // Fog of war
    std::vector<bool> explored;       // Has been seen before
    std::vector<bool> visible;        // Currently visible
    std::vector<float> tileVisibilities;

    // Selection and input
    SelectionState selection;
    CursorMode cursorMode;
    BuildableBuilding selectedBuilding;
};
```

### 6.3 Key Data Structures

| Struct | Description |
|--------|-------------|
| `Villager` | Unit with position, path, animation state, garrison status |
| `TownCenter` | 4x4 tile building, 15 garrison capacity, training queue |
| `House` | 2x2 tile building, construction progress (0%, 33%, 66%, 100%) |
| `PineTree` | Resource node with hitbox for harvesting |
| `GPUState` | OpenGL VAO/VBO handles, shader programs, uniform locations |
| `EngineState` | Animation sets, sprite textures, icon textures, GPU state |

---

## 7. Coordinate System

### 7.1 Isometric Parameters

| Constant | Value | Description |
|----------|-------|-------------|
| `TILE_HALF_WIDTH` | 48 | Half width of isometric tile |
| `TILE_HALF_HEIGHT` | 24 | Half height of isometric tile |
| `GRID_SIZE` | 200 | Number of tiles per axis |
| `TOTAL_TILES` | 40,000 | Total tiles (200 * 200) |

The 2:1 ratio (width:height = 2:1) is the classic isometric projection.

### 7.2 Coordinate Transformations

```
┌─────────────────────────────────────────────────────────────┐
│                  Coordinate Conversion                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Tile → World:                                               │
│    worldX = (tileX - tileY) * HALF_WIDTH                    │
│    worldY = -(tileX + tileY) * HALF_HEIGHT                  │
│                                                             │
│  World → Tile:                                              │
│    tileX = (worldX / HALF_WIDTH - worldY / HALF_HEIGHT) / 2 │
│    tileY = (worldX / HALF_WIDTH + worldY / HALF_HEIGHT) / 2  │
│                                                             │
│  Screen → World:                                             │
│    1. Subtract screen center and camera offset              │
│    2. Divide by zoom                                        │
│    3. Apply inverse isometric transform                     │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 7.3 Z-Ordering

Entities are sorted by Y coordinate for correct depth:
- Higher Y coordinate = rendered later = appears in front
- This ensures correct overlap when units walk "behind" buildings

---

## 8. Game Systems

### 8.1 Fog of War

Three visibility states:

| State | Value | Meaning |
|-------|-------|---------|
| Hidden | 0.0 | Never seen or currently not visible |
| Explored | 0.5 | Previously seen but not currently visible |
| Visible | 1.0 | Currently in line of sight |

**Line of Sight Radii:**
- Villagers: 4 tiles (circular)
- Town Centers: 8 tiles (circular)

### 8.2 Pathfinding (A* Algorithm)

**Features:**
- Priority queue-based A* implementation
- Diagonal movement support
- Corner cutting prevention (checks adjacent tiles)
- Blocked tile detection (trees, buildings)
- Returns vector of world positions as waypoints

**Group Destination Calculation:**
When multiple units are commanded to move to one location, destinations are spread to avoid stacking.

### 8.3 Building System

**Construction Stages (Houses):**
- 0% - Foundation sprite
- 33% - Frame sprite
- 66% - Near-complete sprite
- 100% - Complete sprite

**Building Sizes:**
| Building | Footprint |
|----------|-----------|
| Town Center | 4x4 tiles |
| House | 2x2 tiles |

**Ghost Building:**
Before placement, a transparent "ghost" version appears at cursor position showing validity (green = valid, red = invalid).

### 8.4 Input Modes

| Mode | Description |
|------|-------------|
| `Normal` | Default cursor, selection |
| `Garrison` | Custom cursor, right-click to garrison villagers |
| `BuildEco` | Economic building placement (Q key) |
| `BuildMil` | Military building placement (W key) |

### 8.5 Camera Controls

| Input | Action |
|-------|--------|
| Mouse at screen edges | Pan camera (50px threshold) |
| Scroll wheel | Zoom (0.25x to 10x range) |
| Q | Build Town Center |
| W | Build House |
| T | Toggle garrison mode |
| G | Stop selected units |
| ESC | Deselect all |

### 8.6 Resources

| Resource | Starting Amount |
|----------|-----------------|
| Food | 200 |
| Wood | 200 |
| Stone | 0 |
| Gold | 0 |
| Max Population | 10 |
| Current Population | 0 |

---

## 9. Dependencies

### 9.1 External Dependencies (FetchContent)

| Library | Version | Purpose |
|---------|---------|---------|
| **Google Test** | v1.16.0 | Unit testing framework |
| **GLM** | latest | Math (vectors, matrices, transforms) |
| **GLFW** | 3.4 | Window management and input |
| **Dear ImGui** | v1.91.9 | Immediate-mode UI |

### 9.2 Local Dependencies

| Library | Location | Purpose |
|---------|----------|---------|
| **GLAD** | `third_party/glad/` | OpenGL 3.3 function loader |

### 9.3 System Libraries (Windows)

| Library | Purpose |
|---------|---------|
| **ole32** | COM initialization |
| **windowscodecs** | WIC texture loading |
| **gdi32** | Device context for cursor loading |

---

## 10. Testing

### 10.1 Test Framework

The project uses **Google Test** for unit testing.

### 10.2 Test Coverage

| Test File | Coverage |
|-----------|----------|
| `CoordinateSystem_test.cpp` | Tile/world coordinate roundtrip conversions, boundary checks |
| `GameLogicHelpers_test.cpp` | Hit testing, polygon tests, tile blocking |
| `Pathfinding_test.cpp` | A* pathfinding, blocked routes, group destinations |

### 10.3 Running Tests

```bash
./build/aoe2_tests
```

---

## Appendix A: Asset Structure

```
assets/
├── u_vil_male_villager_walkA_x1.sld/   # 480 frames (16 directions x 30 frames)
├── u_vil_male_villager_idleA_x1.sld/  # 16 frames (1 per direction)
├── u_vil_male_builder_taskA_x2.sld/   # Builder animation
├── n_tree_pine_x1.sld/               # Tree sprite (frame 4)
├── town_center/
│   └── b_afri_town_center_age2_x1.png
├── house/
│   └── image_1x1_*.png               # 4 construction stages
├── actions_icons/                    # UI button icons
└── buildings_icons/                   # Building selection icons
```

## Appendix B: Configuration Files

| File | Purpose |
|------|---------|
| `imgui.ini` | ImGui window positions/sizes persistence |
| `IMPLEMENTATION_PLAN.md` | Development roadmap |

---

*Documentation generated for AoE2 Clone project*
