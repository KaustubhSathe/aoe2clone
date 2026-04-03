#include "Engine.h"
#include "Globals.h"
#include "Constants.h"
#include "../Math/CoordinateSystem.h"
#include "../Game/GameLogicHelpers.h"
#include "../Game/Pathfinding.h"
#include <imgui.h>

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

    // Garrison cursor mode is now handled by key_callback
    // This only handles Escape key cancellation in case key callback didn't fire
    if (gAppState != nullptr)
    {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS && gAppState->cursorMode == CursorMode::Garrison)
        {
            gAppState->cursorMode = CursorMode::Normal;
            glfwSetCursor(window, nullptr);
        }
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
        // Exit garrison cursor mode on left click (even if not on anything)
        if (gAppState->cursorMode == CursorMode::Garrison)
        {
            gAppState->cursorMode = CursorMode::Normal;
            glfwSetCursor(window, nullptr);
            return;
        }

        gAppState->selection.dragging = true;
        gAppState->selection.moved = false;
        gAppState->selection.startScreen = gAppState->cursorScreen;
        gAppState->selection.currentScreen = gAppState->cursorScreen;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        // Exit garrison cursor mode on left click release
        if (gAppState->cursorMode == CursorMode::Garrison)
        {
            gAppState->cursorMode = CursorMode::Normal;
            glfwSetCursor(window, nullptr);
            return;
        }

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
        // Handle garrison cursor mode
        if (gAppState->cursorMode == CursorMode::Garrison)
        {
            // Check if clicking on a town center
            int garrisonTCIndex = -1;
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

            if (garrisonTCIndex >= 0)
            {
                // Find any selected villagers
                bool anySelected = false;
                for (const Villager& v : gAppState->villagers)
                {
                    if (v.selected && !v.isGarrisoned) anySelected = true;
                }

                if (anySelected)
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
            }

            // Exit garrison cursor mode
            gAppState->cursorMode = CursorMode::Normal;
            glfwSetCursor(window, nullptr);
            return;
        }

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

// GLFW callback fired on key press and release.
// Handles T key for garrison cursor mode toggle.
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (gAppState == nullptr)
    {
        return;
    }

    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard)
    {
        return;
    }

    if (key == GLFW_KEY_T && action == GLFW_PRESS)
    {
        std::cout << "T key callback fired! Current mode: " << (gAppState->cursorMode == CursorMode::Garrison ? "Garrison" : "Normal") << std::endl;
        if (gAppState->cursorMode == CursorMode::Garrison)
        {
            gAppState->cursorMode = CursorMode::Normal;
            glfwSetCursor(window, nullptr);
            std::cout << "Cursor set to nullptr (Normal mode)" << std::endl;
        }
        else
        {
            gAppState->cursorMode = CursorMode::Garrison;
            if (gGarrisonCursor != nullptr)
            {
                glfwSetCursor(window, gGarrisonCursor);
                std::cout << "Cursor set to garrison cursor" << std::endl;
            }
            else
            {
                std::cout << "ERROR: gGarrisonCursor is null!" << std::endl;
            }
        }
    }

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        if (gAppState->cursorMode == CursorMode::Garrison)
        {
            std::cout << "Escape - exiting garrison mode" << std::endl;
            gAppState->cursorMode = CursorMode::Normal;
            glfwSetCursor(window, nullptr);
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
