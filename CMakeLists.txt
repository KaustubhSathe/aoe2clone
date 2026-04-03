cmake_minimum_required(VERSION 3.14)
project(AOE2CLONE VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ── GoogleTest ─────────────────────────────────────────────────────────────────
include(FetchContent)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.16.0
)
FetchContent_MakeAvailable(googletest)

# ── GLM ───────────────────────────────────────────────────────────────────────
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        0af55ccecd98d4e5a8d1fad7de25ba429d60e863
)
FetchContent_MakeAvailable(glm)

# ── GLFW ──────────────────────────────────────────────────────────────────────
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# ── Dear ImGui ────────────────────────────────────────────────────────────────
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.9
)
FetchContent_MakeAvailable(imgui)

# ImGui has no CMakeLists, so build it as an INTERFACE/STATIC target manually.
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
target_link_libraries(imgui PUBLIC glfw)

# ── GLAD (kept local — generated source, not a versioned repo) ────────────────
set(GLAD_DIR ${CMAKE_SOURCE_DIR}/third_party/glad)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    main.cpp
    src/Core/Globals.cpp
    src/Core/Engine.cpp
    src/Math/CoordinateSystem.cpp
    src/Graphics/RendererHelpers.cpp
    src/Game/GameLogicHelpers.cpp
    src/Game/Pathfinding.cpp
    src/Game/GameLoop.cpp
    ${GLAD_DIR}/src/glad.c
)

add_executable(aoe2 ${SOURCES})

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(aoe2 PRIVATE
    ${GLAD_DIR}/include
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(aoe2 PRIVATE
    imgui
    glfw
    glm::glm
    ole32
    windowscodecs
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()

set(TEST_SOURCES
    tests/CoordinateSystem_test.cpp
    tests/GameLogicHelpers_test.cpp
    tests/Pathfinding_test.cpp
    src/Math/CoordinateSystem.cpp
    src/Core/Globals.cpp
    src/Game/GameLogicHelpers.cpp
    src/Game/Pathfinding.cpp
)

add_executable(aoe2_tests ${TEST_SOURCES})
target_link_libraries(aoe2_tests PRIVATE
    gtest_main
    glm::glm
    glfw
)
target_include_directories(aoe2_tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    third_party/glad/include
)
include(GoogleTest)
gtest_discover_tests(aoe2_tests)
