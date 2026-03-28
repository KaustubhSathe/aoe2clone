cmake_minimum_required(VERSION 3.10)
project(AOE2CLONE VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include(FetchContent)
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        0af55ccecd98d4e5a8d1fad7de25ba429d60e863
)
FetchContent_MakeAvailable(glm)

# Sources
set(SOURCES
    main.cpp
    C:/Users/ANT-PC/Downloads/glad/src/glad.c
)

add_executable(aoe2 ${SOURCES})

# Include directories
target_include_directories(aoe2 PRIVATE
    C:/Users/ANT-PC/Downloads/glad/include
    C:/Users/ANT-PC/Downloads/glfw-3.4/glfw-3.4/include
)

# Libraries
target_link_directories(aoe2 PRIVATE
    C:/Users/ANT-PC/Downloads/glfw-3.4/glfw-3.4/build/src/Debug
)
target_link_libraries(aoe2 PRIVATE glfw3 glm::glm)
