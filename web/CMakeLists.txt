cmake_minimum_required(VERSION 3.14)
project(AOE2CLONE VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(ROOT_DIR ${CMAKE_SOURCE_DIR}/..)

# ── GLM ───────────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        0af55ccecd98d4e5a8d1fad7de25ba429d60e863
)
FetchContent_MakeAvailable(glm)

# ── Dear ImGui ────────────────────────────────────────────────────────────────
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.9
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

# ── stb_image ─────────────────────────────────────────────────────────────────
set(STB_DIR ${ROOT_DIR}/third_party/stb)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    ${ROOT_DIR}/main.cpp
    ${ROOT_DIR}/src/Core/Globals.cpp
    ${ROOT_DIR}/src/Core/Engine.cpp
    ${ROOT_DIR}/src/Math/CoordinateSystem.cpp
    ${ROOT_DIR}/src/Graphics/RendererHelpers.cpp
    ${ROOT_DIR}/src/Game/GameLogicHelpers.cpp
    ${ROOT_DIR}/src/Game/Pathfinding.cpp
    ${ROOT_DIR}/src/Game/GameLoop.cpp
)

add_executable(aoe2 ${SOURCES})

# Output as HTML
set_target_properties(aoe2 PROPERTIES SUFFIX ".html")

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(aoe2 PRIVATE
    ${ROOT_DIR}
    ${STB_DIR}
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(aoe2 PRIVATE
    imgui
    glm::glm
)

# ── Emscripten linker flags ──────────────────────────────────────────────────
set_target_properties(aoe2 PROPERTIES
    LINK_FLAGS "\
        -s USE_GLFW=3 \
        -s MAX_WEBGL_VERSION=2 \
        -s FULL_ES3=1 \
        -s ALLOW_MEMORY_GROWTH=1 \
        --preload-file ${ROOT_DIR}/assets@assets \
        --preload-file ${ROOT_DIR}/src/shaders@src/shaders \
        -s INITIAL_MEMORY=67108864 \
        --pre-js ${CMAKE_SOURCE_DIR}/pre.js \
        --shell-file ${ROOT_DIR}/shell_minimal.html \
    "
)
