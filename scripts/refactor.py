import os
import re

main_cpp_path = os.path.join(os.path.dirname(__file__), '..', 'main.cpp')
gameloop_path = os.path.join(os.path.dirname(__file__), '..', 'src', 'Game', 'GameLoop.cpp')
new_main_path = os.path.join(os.path.dirname(__file__), '..', 'src', 'main.cpp')

with open(main_cpp_path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

def extract_lines(start_regex, end_regex):
    start_idx = -1
    end_idx = -1
    for i, line in enumerate(lines):
        if start_idx == -1 and re.search(start_regex, line):
            start_idx = i
        elif start_idx != -1 and re.search(end_regex, line):
            end_idx = i
            break
    if start_idx == -1 or end_idx == -1:
        return []
    return lines[start_idx:end_idx]

# Extract UpdateSimulation
update_lines = extract_lines(r'for \(Villager& v : appState\.villagers\)', r'        // ---------------------------------------------------------------------')
# The end regex matches the start of "Render Pass" comment. But we need to make sure we get the right block.
# Actually let's use exact line numbers based on our previous view_file
# 1420 to 1765
update_code = "".join(lines[1419:1765])

# Replace bare GPU variables in UpdateSimulation
update_code = update_code.replace("visibilityVBO", "engine.gpu.visibilityVBO")

# Extract RenderScene
# 1776 to 2070
render_code = "".join(lines[1775:2071])
# Replacements for RenderScene
gpu_vars = [
    "tileShaderProgram", "tileProjLoc", "tileViewLoc", "tileVAO", "tileColorLoc",
    "blockedTileTranslations", "blockedTileVAO", "outlineVAO", "spriteShaderProgram",
    "spriteProjLoc", "spriteViewLoc", "spriteVAO", "spriteSizeLoc", "spriteVisLoc",
    "spritePosLoc", "overlayShaderProgram", "overlayProjLoc", "overlayViewLoc",
    "overlayOffsetLoc", "overlayColorLoc", "pineBoundsVAO", "selectionVAO",
    "rectVBO", "rectVAO", "pineTreeScale"
]

for var in gpu_vars:
    if var == "pineTreeScale":
        render_code = render_code.replace("pineTreeScale", "appState.pineTreeSpriteSize")
    elif var == "blockedTileTranslations":
        render_code = render_code.replace("blockedTileTranslations", "engine.blockedTileTranslations")
    elif var == "walkAnimation":
        render_code = render_code.replace("walkAnimation", "engine.walkAnimation")
    elif var == "idleAnimation":
        render_code = render_code.replace("idleAnimation", "engine.idleAnimation")
    elif var == "pineTreeFrame":
        render_code = render_code.replace("pineTreeFrame", "engine.pineTreeFrame")
    elif var == "townCenterFrame":
        render_code = render_code.replace("townCenterFrame", "engine.townCenterFrame")
    else:
        # Avoid replacing parts of words
        render_code = re.sub(r'\b' + var + r'\b', f"engine.gpu.{var}", render_code)

render_code = render_code.replace("walkAnimation", "engine.walkAnimation")
render_code = render_code.replace("idleAnimation", "engine.idleAnimation")
render_code = render_code.replace("pineTreeFrame", "engine.pineTreeFrame")
render_code = render_code.replace("townCenterFrame", "engine.townCenterFrame")

# Extract RenderUI
# 2077 to 2364
ui_code = "".join(lines[2076:2364])

# Generate GameLoop.cpp
gameloop_cpp = f"""#include "GameLoop.h"
#include "../Core/Constants.h"
#include "../Core/Globals.h"
#include "../Math/CoordinateSystem.h"
#include "Pathfinding.h"
#include "GameLogicHelpers.h"
#include "../Graphics/RendererHelpers.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

void UpdateSimulation(EngineState& engine, AppState& appState)
{{
{update_code}
}}

void RenderScene(EngineState& engine, AppState& appState)
{{
{render_code}
}}

void RenderUI(EngineState& engine, AppState& appState)
{{
{ui_code}
}}
"""

with open(gameloop_path, 'w', encoding='utf-8') as f:
    f.write(gameloop_cpp)

print("GameLoop.cpp generated successfully.")
