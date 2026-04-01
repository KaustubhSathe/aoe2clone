import os
import re

main_cpp_path = os.path.join(os.path.dirname(__file__), '..', 'main.cpp')
with open(main_cpp_path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

new_main_content = []

new_main_content.append("""// =============================================================================
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

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow* window);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
""")

# Lines 902 to 1004 are shaders
for line in lines[903:1004]:
    new_main_content.append(line)

new_main_content.append(lines[1004]) # int main()
new_main_content.append(lines[1005]) # {

# from line 1006 to 1227 (initialization up to GPU buffers)
for line in lines[1006:1227]:
    if "AppState appState;" in line:
        new_main_content.append("    AppState appState;\n")
        new_main_content.append("    EngineState engine;\n")
    elif "walkAnimation." in line:
        new_main_content.append(line.replace("walkAnimation", "engine.walkAnimation"))
    elif "idleAnimation." in line:
        new_main_content.append(line.replace("idleAnimation", "engine.idleAnimation"))
    elif "pineTreeFrame" in line:
        new_main_content.append(line.replace("pineTreeFrame", "engine.pineTreeFrame"))
    elif "townCenterFrame" in line:
        new_main_content.append(line.replace("townCenterFrame", "engine.townCenterFrame"))
    elif "AnimationSet walkAnimation;" in line or "AnimationSet idleAnimation;" in line:
        continue # removed, they are inside engine
    elif "translations.reserve" in line or "translations.push_back" in line:
        new_main_content.append(line.replace("translations", "engine.translations"))
    elif "std::vector<glm::vec2> blockedTileTranslations = blocked_tile_translations(appState);" in line:
        new_main_content.append(line.replace("std::vector<glm::vec2> blockedTileTranslations", "engine.blockedTileTranslations"))
    elif "std::vector<glm::vec2> translations;" in line:
        continue # removed
    else:
        new_main_content.append(line)

# For the rest of initialization before while block (1227 to 1401)
gpu_vars = [
    ("tileVAO", "engine.gpu.tileVAO"),
    ("tileVBO", "engine.gpu.tileVBO"),
    ("outlineVAO", "engine.gpu.outlineVAO"),
    ("outlineVBO", "engine.gpu.outlineVBO"),
    ("instanceVBO", "engine.gpu.instanceVBO"),
    ("visibilityVBO", "engine.gpu.visibilityVBO"),
    ("blockedTileVAO", "engine.gpu.blockedTileVAO"),
    ("blockedTileVBO", "engine.gpu.blockedTileVBO"),
    ("blockedInstanceVBO", "engine.gpu.blockedInstanceVBO"),
    ("spriteVAO", "engine.gpu.spriteVAO"),
    ("spriteVBO", "engine.gpu.spriteVBO"),
    ("selectionVAO", "engine.gpu.selectionVAO"),
    ("selectionVBO", "engine.gpu.selectionVBO"),
    ("rectVAO", "engine.gpu.rectVAO"),
    ("rectVBO", "engine.gpu.rectVBO"),
    ("pineBoundsVAO", "engine.gpu.pineBoundsVAO"),
    ("pineBoundsVBO", "engine.gpu.pineBoundsVBO"),
    ("tileShaderProgram", "engine.gpu.tileShaderProgram"),
    ("spriteShaderProgram", "engine.gpu.spriteShaderProgram"),
    ("overlayShaderProgram", "engine.gpu.overlayShaderProgram"),
    ("tileColorLoc", "engine.gpu.tileColorLoc"),
    ("tileProjLoc", "engine.gpu.tileProjLoc"),
    ("tileViewLoc", "engine.gpu.tileViewLoc"),
    ("spriteProjLoc", "engine.gpu.spriteProjLoc"),
    ("spriteViewLoc", "engine.gpu.spriteViewLoc"),
    ("spritePosLoc", "engine.gpu.spritePosLoc"),
    ("spriteSizeLoc", "engine.gpu.spriteSizeLoc"),
    ("spriteVisLoc", "engine.gpu.spriteVisLoc"),
    ("overlayProjLoc", "engine.gpu.overlayProjLoc"),
    ("overlayViewLoc", "engine.gpu.overlayViewLoc"),
    ("overlayOffsetLoc", "engine.gpu.overlayOffsetLoc"),
    ("overlayColorLoc", "engine.gpu.overlayColorLoc")
]

for line in lines[1227:1401]:
    if line.strip().startswith("unsigned int ") and " = 0;" in line and any(var[0] in line for var in gpu_vars):
        continue # skip variable declaration
    l = line
    for var, replacement in gpu_vars:
        l = l.replace(f"&{var}", f"&{replacement}")
        # avoid replacing partial matches like "selectionVAO" inside "selectionVAO2"
        l = re.sub(r'\b' + var + r'\b', replacement, l)
    
    # other replacements
    l = l.replace("translations", "engine.translations")
    l = l.replace("blockedTileTranslations", "engine.blockedTileTranslations")
    l = l.replace("pineTreeFrame", "engine.pineTreeFrame")
    new_main_content.append(l)


new_main_content.append("""
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
""")

# Loop finishes at 2372, so Cleanup from 2373 to 2418
for line in lines[2373:2419]:
    l = line
    for var, replacement in gpu_vars:
        l = l.replace(f"&{var}", f"&{replacement}")
        l = re.sub(r'\b' + var + r'\b', replacement, l)
        
    l = l.replace("walkAnimation", "engine.walkAnimation")
    l = l.replace("idleAnimation", "engine.idleAnimation")
    l = l.replace("pineTreeFrame", "engine.pineTreeFrame")
    new_main_content.append(l)

# Callbacks from 2419 to end
for line in lines[2419:]:
    new_main_content.append(line)

with open(main_cpp_path, 'w', encoding='utf-8') as f:
    f.writelines(new_main_content)

print("Main cpp generated successfully.")
