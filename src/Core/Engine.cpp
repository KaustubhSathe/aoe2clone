#include "Engine.h"
#include "Globals.h"
#include "Constants.h"
#include "../Math/CoordinateSystem.h"
#include "../Game/GameLogicHelpers.h"
#include "../Game/Pathfinding.h"
#include <imgui.h>
#include <algorithm>

// =============================================================================
// GLFW CALLBACKS
// Called by GLFW on input and window events. They are registered in main()
// via glfwSet*Callback and run on the main thread between glfwPollEvents calls.
// =============================================================================

// Reads keyboard state every frame and moves the camera accordingly.
// Mouse edge panning: when cursor is near screen edges, camera moves in that direction.
// Camera speed is scaled by (1/zoom) so panning feels consistent at all zoom levels.
void processInput(GLFWwindow* window)
{
    // Edge scrolling configuration
    const float EDGE_THRESHOLD = 50.0f;
    const float MAX_EDGE_VELOCITY_MULTIPLIER = 2.5f;

    // Get current mouse position
    double mouseX, mouseY;
    glfwGetCursorPos(window, &mouseX, &mouseY);

    // Calculate how close to each edge (0.0 = at threshold, 1.0 = at corner)
    float leftEdge = (EDGE_THRESHOLD - static_cast<float>(mouseX)) / EDGE_THRESHOLD;
    float rightEdge = (static_cast<float>(mouseX) - (SCR_WIDTH - EDGE_THRESHOLD)) / EDGE_THRESHOLD;
    float topEdge = (EDGE_THRESHOLD - static_cast<float>(mouseY)) / EDGE_THRESHOLD;
    float bottomEdge = (static_cast<float>(mouseY) - (SCR_HEIGHT - EDGE_THRESHOLD)) / EDGE_THRESHOLD;

    // Clamp to [0, 1] range
    leftEdge = std::max(0.0f, std::min(1.0f, leftEdge));
    rightEdge = std::max(0.0f, std::min(1.0f, rightEdge));
    topEdge = std::max(0.0f, std::min(1.0f, topEdge));
    bottomEdge = std::max(0.0f, std::min(1.0f, bottomEdge));

    // Calculate velocity based on proximity to edge
    float edgeVelocity = cameraSpeed * deltaTime * (1.0f / zoom);
    float leftVel = -leftEdge * edgeVelocity * MAX_EDGE_VELOCITY_MULTIPLIER;
    float rightVel = rightEdge * edgeVelocity * MAX_EDGE_VELOCITY_MULTIPLIER;
    float upVel = topEdge * edgeVelocity * MAX_EDGE_VELOCITY_MULTIPLIER;
    float downVel = -bottomEdge * edgeVelocity * MAX_EDGE_VELOCITY_MULTIPLIER;

    // Apply camera movement
    cameraX += leftVel + rightVel;
    cameraY += upVel + downVel;

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

    // Update pending build tile for highlighting (only when a villager is selected)
    bool anyVillagerSelected = false;
    for (const auto& [uuid, v] : gAppState->villagers)
    {
        if (v.selected && !v.isGarrisoned)
        {
            anyVillagerSelected = true;
            break;
        }
    }
    if (anyVillagerSelected && (gAppState->cursorMode == CursorMode::BuildEco || gAppState->cursorMode == CursorMode::BuildMil) && gAppState->selectedBuilding != BuildableBuilding::None)
    {
        glm::vec2 worldPos = screen_to_world(glm::vec2(static_cast<float>(xpos), static_cast<float>(ypos)));
        std::optional<glm::ivec2> tileOpt = world_to_tile(worldPos);
        if (tileOpt.has_value())
        {
            glm::ivec2 tile = tileOpt.value();
            if (tile.x >= 0 && tile.x < GRID_SIZE - 1 && tile.y >= 0 && tile.y < GRID_SIZE - 1)
            {
                // Check if all 4 tiles of the 2x2 building footprint are explored (visibility > 0)
                int tileIndex = tile.y * GRID_SIZE + tile.x;
                int tileIndex1 = tile.y * GRID_SIZE + (tile.x + 1);
                int tileIndex2 = (tile.y + 1) * GRID_SIZE + tile.x;
                int tileIndex3 = (tile.y + 1) * GRID_SIZE + (tile.x + 1);
                bool allExplored = (gAppState->tileVisibilities[tileIndex] > 0.0f &&
                                   gAppState->tileVisibilities[tileIndex1] > 0.0f &&
                                   gAppState->tileVisibilities[tileIndex2] > 0.0f &&
                                   gAppState->tileVisibilities[tileIndex3] > 0.0f);

                gAppState->pendingBuildTile = tile;
                gAppState->canBuildAtPendingTile = allExplored &&
                                                   !is_tile_blocked(*gAppState, tile) &&
                                                   !is_tile_blocked(*gAppState, tile + glm::ivec2(1, 0)) &&
                                                   !is_tile_blocked(*gAppState, tile + glm::ivec2(0, 1)) &&
                                                   !is_tile_blocked(*gAppState, tile + glm::ivec2(1, 1));
            }
            else
            {
                gAppState->pendingBuildTile = glm::ivec2(-1, -1);
                gAppState->canBuildAtPendingTile = false;
            }
        }
        else
        {
            gAppState->pendingBuildTile = glm::ivec2(-1, -1);
            gAppState->canBuildAtPendingTile = false;
        }
    }
    else
    {
        gAppState->pendingBuildTile = glm::ivec2(-1, -1);
        gAppState->canBuildAtPendingTile = false;
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

        // Handle building placement on left click
        if ((gAppState->cursorMode == CursorMode::BuildEco || gAppState->cursorMode == CursorMode::BuildMil) && gAppState->selectedBuilding != BuildableBuilding::None)
        {
            if (gAppState->pendingBuildTile.x >= 0 && gAppState->canBuildAtPendingTile)
            {
                glm::ivec2 tile = gAppState->pendingBuildTile;

                if (gAppState->selectedBuilding == BuildableBuilding::House)
                {
                    if (gAppState->wood >= HOUSE_COST_WOOD)
                    {
                        EntityId villagerUUID = 0;
                        for (auto& [uuid, v] : gAppState->villagers)
                        {
                            if (v.selected && !v.isGarrisoned)
                            {
                                villagerUUID = uuid;
                                break;
                            }
                        }

                        if (villagerUUID != 0)
                        {
                            const bool shiftHeld = (mods & GLFW_MOD_SHIFT) != 0;
                            if (!shiftHeld)
                            {
                                gAppState->villagers.at(villagerUUID).operationQueue.clear();
                            }

                            // Check if any villager is standing on the building tiles
                            std::vector<EntityId> villagersOnBuildingTiles;
                            for (auto& [uuid, vCheck] : gAppState->villagers)
                            {
                                if (vCheck.isGarrisoned) continue;
                                const std::optional<glm::ivec2> vTileOpt = world_to_tile(vCheck.position);
                                if (vTileOpt.has_value())
                                {
                                    glm::ivec2 vTile = vTileOpt.value();
                                    bool onBuilding = (vTile.x >= tile.x && vTile.x < tile.x + 2 &&
                                                      vTile.y >= tile.y && vTile.y < tile.y + 2);
                                    if (onBuilding)
                                    {
                                        villagersOnBuildingTiles.push_back(uuid);
                                    }
                                }
                            }

                            // Move villagers off building tiles first (before house is added to blocked list)
                            for (EntityId vUUID : villagersOnBuildingTiles)
                            {
                                Villager& v = gAppState->villagers.at(vUUID);

                                // Find adjacent tile to move to (using current blocked state, before house is added)
                                float bestDist = 1e9f;
                                glm::vec2 moveTarget = v.position;

                                for (int dx = -1; dx <= 2; ++dx)
                                {
                                    for (int dy = -1; dy <= 2; ++dy)
                                    {
                                        if (dx >= 0 && dx <= 1 && dy >= 0 && dy <= 1) continue;
                                        glm::ivec2 p(tile.x + dx, tile.y + dy);
                                        if (p.x >= 0 && p.x < GRID_SIZE && p.y >= 0 && p.y < GRID_SIZE && !is_tile_blocked(*gAppState, p))
                                        {
                                            float dist = glm::length(tile_to_world(p) - v.position);
                                            if (dist < bestDist)
                                            {
                                                bestDist = dist;
                                                moveTarget = tile_to_world(p);
                                            }
                                        }
                                    }
                                }

                                std::vector<glm::vec2> path = find_path(*gAppState, v.position, moveTarget);
                                
                                // Evacuation must be pushed to the FRONT so the villager moves out of the way immediately.
                                if (path.size() > 1)
                                {
                                    for (size_t i = path.size(); i-- > 1; )
                                    {
                                        QueuedOperation op;
                                        op.type = OperationType::WALK;
                                        op.targetPosition = path[i];
                                        v.operationQueue.insert(v.operationQueue.begin(), op);
                                    }
                                }
                                else if (path.size() == 1)
                                {
                                    QueuedOperation op;
                                    op.type = OperationType::WALK;
                                    op.targetPosition = path[0];
                                    v.operationQueue.insert(v.operationQueue.begin(), op);
                                }
                            }

                            Villager& v = gAppState->villagers.at(villagerUUID);

                            const bool shiftHeldForQueue = (mods & GLFW_MOD_SHIFT) != 0;

                            // If shift is held, queue this build - create ghost house immediately
                            if (shiftHeldForQueue)
                            {
                                // Deduct resources when creating the queued house
                                gAppState->wood -= HOUSE_COST_WOOD;

                                // Create ghost house immediately so it appears at the location
                                House queuedHouse;
                                queuedHouse.uuid = gNextUuid++;
                                queuedHouse.tile = tile;
                                queuedHouse.position = tile_to_world(tile);
                                queuedHouse.hp = 500;
                                queuedHouse.maxHp = 500;
                                queuedHouse.isUnderConstruction = true;
                                queuedHouse.isGhostFoundation = true;
                                queuedHouse.assignedVillagerId = villagerUUID;
                                gAppState->houses[queuedHouse.uuid] = queuedHouse;

                                // Queue the BUILD operation with the house's UUID
                                QueuedOperation buildOp;
                                buildOp.type = OperationType::BUILD;
                                buildOp.buildingId = queuedHouse.uuid;
                                buildOp.targetTile = tile;
                                buildOp.buildingType = BuildableBuilding::House;
                                buildOp.targetPosition = glm::vec2(0.0f);
                                v.operationQueue.push_back(buildOp);

                                gAppState->selectedBuilding = BuildableBuilding::None;
                                gAppState->cursorMode = CursorMode::Normal;
                                gAppState->pendingBuildTile = glm::ivec2(-1, -1);
                                gAppState->selection.dragging = false;
                                gAppState->selection.moved = false;
                                return;
                            }

                            // Not queueing - create house and start building now
                            gAppState->wood -= HOUSE_COST_WOOD;

                            House house;
                            house.uuid = gNextUuid++;
                            house.tile = tile;
                            house.position = tile_to_world(tile);
                            house.hp = 500;
                            house.maxHp = 500;
                            house.isUnderConstruction = true;
                            house.isGhostFoundation = true; // Don't block tiles until villager arrives
                            house.assignedVillagerId = villagerUUID;
                            gAppState->houses[house.uuid] = house;
                            // DON'T rebuild blocked tiles yet - ghost doesn't block

                            // If villager is already building (without shift), abort old build and start new one
                            if (v.isBuilding)
                            {
                                // Abort the current building
                                if (!v.operationQueue.empty() && v.operationQueue.front().type == OperationType::BUILD)
                                {
                                    EntityId oldBuildingId = v.operationQueue.front().buildingId;
                                    if (oldBuildingId != 0 && gAppState->houses.count(oldBuildingId) > 0)
                                    {
                                        House& oldHouse = gAppState->houses.at(oldBuildingId);
                                        oldHouse.isUnderConstruction = true;
                                        oldHouse.isGhostFoundation = true;
                                        oldHouse.buildProgress = 0.0f;
                                        oldHouse.assignedVillagerId = 0;
                                    }
                                }
                                v.isBuilding = false;
                            }

                            // Add BUILD operation to the queue
                            QueuedOperation buildOp;
                            buildOp.type = OperationType::BUILD;
                            buildOp.buildingId = house.uuid;
                            buildOp.targetTile = tile;
                            buildOp.targetPosition = glm::vec2(0.0f); // Will be set when processing
                            v.operationQueue.push_back(buildOp);

                            gAppState->selectedBuilding = BuildableBuilding::None;
                            gAppState->cursorMode = CursorMode::Normal;
                            gAppState->pendingBuildTile = glm::ivec2(-1, -1);
                            gAppState->selection.dragging = false;
                            gAppState->selection.moved = false;
                            return;
                        }
                    }
                }
            }
            gAppState->selectedBuilding = BuildableBuilding::None;
            gAppState->cursorMode = CursorMode::Normal;
            gAppState->pendingBuildTile = glm::ivec2(-1, -1);
            gAppState->selection.dragging = false;
            gAppState->selection.moved = false;
            return;
        }

        if (gAppState->selection.dragging && gAppState->selection.moved)
        {
            clear_selection(*gAppState);
            for (auto& [uuid, v] : gAppState->villagers)
            {
                if (v.isGarrisoned) continue;
                const glm::vec2 vsp = world_to_screen(v.position);
                v.selected = point_in_drag_rect(vsp, gAppState->selection);
            }
        }
        else
        {
            clear_selection(*gAppState);

            EntityId clickedTreeUUID = 0;
            for (auto& [uuid, tree] : gAppState->pineTrees)
            {
                const int treeIndex = tree.tile.y * GRID_SIZE + tree.tile.x;
                if (gAppState->tileVisibilities[treeIndex] > 0.0f && tree_hit_test_screen(tree, gAppState->cursorScreen, gAppState->pineTreeSpriteSize))
                {
                    clickedTreeUUID = uuid;
                    break;
                }
            }

            if (clickedTreeUUID != 0)
            {
                gAppState->selectedTreeId = clickedTreeUUID;
                gAppState->pineTrees.at(clickedTreeUUID).selected = true;
            }
            else
            {
                EntityId clickedTCUUID = 0;
                for (auto& [uuid, tc] : gAppState->townCenters)
                {
                    const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
                    if (gAppState->tileVisibilities[tcIndex] > 0.0f && town_center_hit_test_screen(tc, gAppState->cursorScreen, gAppState->townCenterSpriteSize))
                    {
                        clickedTCUUID = uuid;
                        break;
                    }
                }

                if (clickedTCUUID != 0)
                {
                    gAppState->townCenters.at(clickedTCUUID).selected = true;
                }
                else
                {
                    EntityId clickedHouseUUID = 0;
                    for (auto& [uuid, house] : gAppState->houses)
                    {
                        const int houseIndex = house.tile.y * GRID_SIZE + house.tile.x;
                        if (gAppState->tileVisibilities[houseIndex] > 0.0f && house_hit_test_screen(house, gAppState->cursorScreen, gAppState->houseSpriteSize))
                        {
                            clickedHouseUUID = uuid;
                            break;
                        }
                    }

                    if (clickedHouseUUID != 0)
                    {
                        gAppState->houses.at(clickedHouseUUID).selected = true;
                    }
                    else
                    {
                        for (auto& [uuid, v] : gAppState->villagers)
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
            EntityId garrisonTCUUID = 0;
            for (auto& [uuid, tc] : gAppState->townCenters)
            {
                const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
                if (gAppState->tileVisibilities[tcIndex] > 0.0f && town_center_hit_test_screen(tc, gAppState->cursorScreen, gAppState->townCenterSpriteSize))
                {
                    garrisonTCUUID = uuid;
                    break;
                }
            }

            if (garrisonTCUUID != 0)
            {
                // Find any selected villagers
                bool anySelected = false;
                for (const auto& [uuid, v] : gAppState->villagers)
                {
                    if (v.selected && !v.isGarrisoned) anySelected = true;
                }

                if (anySelected)
                {
                    const TownCenter& targetTc = gAppState->townCenters.at(garrisonTCUUID);
                    for (auto& [uuid, v] : gAppState->villagers)
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
                        v.operationQueue.clear();
                        v.isMovingToGarrison = true;
                        v.targetTcId = garrisonTCUUID;
                        if (path.size() > 1) {
                            v.targetPosition = path[1];
                            v.facingDirection = glm::normalize(v.targetPosition - v.position);
                            v.moving = true;
                            for (size_t i = 2; i < path.size(); ++i) {
                                QueuedOperation op;
                                op.type = OperationType::WALK;
                                op.targetPosition = path[i];
                                v.operationQueue.push_back(op);
                            }
                        } else if (path.size() == 1 && glm::length(path[0] - v.position) > 1.0f) {
                            v.targetPosition = path[0];
                            v.facingDirection = glm::normalize(v.targetPosition - v.position);
                            v.moving = true;
                        } else {
                            if (targetTc.garrisonCount < targetTc.maxGarrison) {
                                gAppState->townCenters.at(garrisonTCUUID).garrisonCount++;
                                v.isGarrisoned = true;
                                v.garrisonTcId = garrisonTCUUID;
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
        for (const auto& [uuid, v] : gAppState->villagers)
        {
            if (v.selected && !v.isGarrisoned) anySelected = true;
        }

        TownCenter* selectedTC = nullptr;
        if (!anySelected)
        {
            for (auto& [tcUUID, tc] : gAppState->townCenters)
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
            EntityId garrisonTCUUID = 0;
            if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS)
            {
                for (auto& [uuid, tc] : gAppState->townCenters)
                {
                    const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
                    if (gAppState->tileVisibilities[tcIndex] > 0.0f && town_center_hit_test_screen(tc, gAppState->cursorScreen, gAppState->townCenterSpriteSize))
                    {
                        garrisonTCUUID = uuid;
                        break;
                    }
                }
            }

            if (garrisonTCUUID != 0)
            {
                const TownCenter& targetTc = gAppState->townCenters.at(garrisonTCUUID);
                for (auto& [uuid, v] : gAppState->villagers)
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
                    v.operationQueue.clear();
                    v.isMovingToGarrison = true;
                    v.targetTcId = garrisonTCUUID;
                    if (path.size() > 1) {
                        v.targetPosition = path[1];
                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                        v.moving = true;
                        for (size_t i = 2; i < path.size(); ++i) {
                            QueuedOperation op;
                            op.type = OperationType::WALK;
                            op.targetPosition = path[i];
                            v.operationQueue.push_back(op);
                        }
                    } else if (path.size() == 1 && glm::length(path[0] - v.position) > 1.0f) {
                        v.targetPosition = path[0];
                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                        v.moving = true;
                    } else {
                        if (targetTc.garrisonCount < targetTc.maxGarrison) {
                            gAppState->townCenters.at(garrisonTCUUID).garrisonCount++;
                            v.isGarrisoned = true;
                            v.garrisonTcId = garrisonTCUUID;
                            v.selected = false;
                            v.moving = false;
                            v.isMovingToGarrison = false;
                        }
                    }
                }
            }
            else
            {
                // Check if right-clicking on an incomplete house to resume building
                for (auto& [houseUUID, house] : gAppState->houses)
                {
                    const int houseTileIndex = house.tile.y * GRID_SIZE + house.tile.x;
                    if (house.isUnderConstruction && gAppState->tileVisibilities[houseTileIndex] > 0.0f && house_hit_test_screen(house, gAppState->cursorScreen, gAppState->houseSpriteSize))
                    {
                        // Find a selected villager to assign to building
                        for (auto& [vUUID, v] : gAppState->villagers)
                        {
                            if (v.selected && !v.isGarrisoned)
                            {
                                // Check if villager is already building something else or has queued operations
                                bool villagerBusy = v.isBuilding || !v.operationQueue.empty();
                                if (villagerBusy)
                                {
                                    // If shift is held, queue this build instead of overriding
                                    if ((mods & GLFW_MOD_SHIFT) != 0)
                                    {
                                        // Queue the build operation to villager's queue
                                        QueuedOperation buildOp;
                                        buildOp.type = OperationType::BUILD;
                                        buildOp.buildingId = houseUUID;
                                        buildOp.targetTile = house.tile;
                                        buildOp.buildingType = BuildableBuilding::None; // House already exists
                                        buildOp.targetPosition = glm::vec2(0.0f);
                                        v.operationQueue.push_back(buildOp);
                                        continue; // Don't override, just queue and continue
                                    }

                                    // Without shift, cancel previous building and start new one
                                    if (v.isBuilding && !v.operationQueue.empty()
                                        && v.operationQueue.front().type == OperationType::BUILD)
                                    {
                                        EntityId oldBId = v.operationQueue.front().buildingId;
                                        if (oldBId != 0 && gAppState->houses.count(oldBId) > 0)
                                        {
                                            gAppState->houses.at(oldBId).assignedVillagerId = 0;
                                        }
                                    }
                                    // Clear queued operations and reset building state since we're overriding
                                    v.operationQueue.clear();
                                    v.isBuilding = false;
                                }

                                // Set up building task (only when not shift-queueing, since we continued above)
                                house.assignedVillagerId = vUUID;

                                // Find adjacent tile to building
                                float bestDist = 1e9f;
                                glm::vec2 buildTarget = house.position;
                                for (int dx = -1; dx <= 2; ++dx)
                                {
                                    for (int dy = -1; dy <= 2; ++dy)
                                    {
                                        if (dx >= 0 && dx <= 1 && dy >= 0 && dy <= 1) continue;
                                        glm::ivec2 p(house.tile.x + dx, house.tile.y + dy);
                                        if (p.x >= 0 && p.x < GRID_SIZE && p.y >= 0 && p.y < GRID_SIZE && !is_tile_blocked(*gAppState, p))
                                        {
                                            float dist = glm::length(tile_to_world(p) - v.position);
                                            if (dist < bestDist)
                                            {
                                                bestDist = dist;
                                                buildTarget = tile_to_world(p);
                                            }
                                        }
                                    }
                                }

                                // If villager is not already at build target, set up movement
                                const float distToTarget = glm::length(buildTarget - v.position);
                                v.operationQueue.clear();
                                if (distToTarget > 1.0f)
                                {
                                    std::vector<glm::vec2> path = find_path(*gAppState, v.position, buildTarget);
                                    if (path.size() > 1)
                                    {
                                        v.targetPosition = path[1];
                                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                                        v.moving = true;
                                        v.isBuilding = false; // Will start building when arrived
                                        for (size_t j = 2; j < path.size(); ++j)
                                        {
                                            QueuedOperation op;
                                            op.type = OperationType::WALK;
                                            op.targetPosition = path[j];
                                            v.operationQueue.push_back(op);
                                        }
                                        // Queue the BUILD operation at the end (after all WALK ops)
                                        QueuedOperation buildOp;
                                        buildOp.type = OperationType::BUILD;
                                        buildOp.buildingId = houseUUID;
                                        buildOp.targetTile = house.tile;
                                        buildOp.buildingType = BuildableBuilding::None;
                                        buildOp.targetPosition = glm::vec2(0.0f);
                                        v.operationQueue.push_back(buildOp);
                                    }
                                    else if (path.size() == 1)
                                    {
                                        v.targetPosition = path[0];
                                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                                        v.moving = !glm::all(glm::equal(v.targetPosition, v.position));
                                        v.isBuilding = false;
                                        // Queue the BUILD operation
                                        QueuedOperation buildOp;
                                        buildOp.type = OperationType::BUILD;
                                        buildOp.buildingId = houseUUID;
                                        buildOp.targetTile = house.tile;
                                        buildOp.buildingType = BuildableBuilding::None;
                                        buildOp.targetPosition = glm::vec2(0.0f);
                                        v.operationQueue.push_back(buildOp);
                                    }
                                    else
                                    {
                                        // Can't find path, start building anyway
                                        v.moving = false;
                                        v.isBuilding = true;
                                        v.builderFrameIndex = 0;
                                        v.builderAnimTimer = 0.0f;
                                    }
                                }
                                else
                                {
                                    // Villager is already at build site, start building immediately
                                    v.moving = false;
                                    v.isBuilding = true;
                                    v.builderFrameIndex = 0;
                                    v.builderAnimTimer = 0.0f;
                                }
                                break;
                            }
                        }
                        return;
                    }
                }

                const glm::vec2 worldTarget = screen_to_world(gAppState->cursorScreen);
                const std::optional<glm::ivec2> targetTile = world_to_tile(worldTarget);
                if (!targetTile.has_value() || is_tile_blocked(*gAppState, *targetTile))
            {
                // Invalid tile — cancel all movement.
                for (auto& [uuid, v] : gAppState->villagers)
                {
                    if (v.selected)
                    {
                        v.targetPosition = v.position;
                        v.operationQueue.clear();
                        v.moving = false;
                    }
                }
                return;
            }

            int selectedCount = 0;
            for (const auto& [uuid, v] : gAppState->villagers)
            {
                if (v.selected) selectedCount++;
            }
            
            std::vector<glm::ivec2> groupDestinations = find_group_destinations(*gAppState, *targetTile, selectedCount);
            const bool shiftHeld = (mods & GLFW_MOD_SHIFT) != 0;

            int destIndex = 0;
            for (auto& [vUUID, v] : gAppState->villagers)
            {
                if (!v.selected) continue;

                // Abort current tasks if shift is NOT held
                if (!shiftHeld)
                {
                    // If currently building, clear the assignment on the house
                    if (v.isBuilding && !v.operationQueue.empty()
                        && v.operationQueue.front().type == OperationType::BUILD)
                    {
                        EntityId bId = v.operationQueue.front().buildingId;
                        if (bId != 0 && gAppState->houses.count(bId) > 0)
                        {
                            gAppState->houses.at(bId).assignedVillagerId = 0;
                        }
                        v.isBuilding = false;
                    }

                    v.operationQueue.clear();
                }

                glm::vec2 finalTarget = (destIndex < groupDestinations.size())
                    ? tile_to_world(groupDestinations[destIndex++])
                    : tile_to_world(*targetTile);

                glm::vec2 startPos = v.position;
                if (shiftHeld)
                {
                    if (!v.operationQueue.empty())
                    {
                        bool foundPos = false;
                        for (auto it = v.operationQueue.rbegin(); it != v.operationQueue.rend(); ++it)
                        {
                            if (it->type == OperationType::WALK)
                            {
                                startPos = it->targetPosition;
                                foundPos = true;
                                break;
                            }
                            else if (it->type == OperationType::BUILD)
                            {
                                startPos = tile_to_world(it->targetTile);
                                foundPos = true;
                                break;
                            }
                        }
                        if (!foundPos && (v.moving || v.isBuilding)) startPos = v.targetPosition;
                    }
                    else if (v.moving || v.isBuilding)
                    {
                        startPos = v.targetPosition;
                    }
                }

                std::vector<glm::vec2> path = find_path(*gAppState, startPos, finalTarget);

                if (shiftHeld)
                {
                    if (!path.empty())
                    {
                        // Skip the first node as it matches startPos exactly
                        for (size_t i = 1; i < path.size(); ++i)
                        {
                            QueuedOperation op;
                            op.type = OperationType::WALK;
                            op.targetPosition = path[i];
                            v.operationQueue.push_back(op);
                        }
                    }
                }
                else
                {
                    // v.operationQueue is already cleared above
                    if (path.size() > 1) // First node is startPos, so path must have >= 2 to move
                    {
                        v.targetPosition = path[1];
                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                        v.moving = true;
                        for (size_t i = 2; i < path.size(); ++i)
                        {
                            QueuedOperation op;
                            op.type = OperationType::WALK;
                            op.targetPosition = path[i];
                            v.operationQueue.push_back(op);
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
            EntityId clickedTCUUID = 0;
            for (auto& [uuid, tc] : gAppState->townCenters)
            {
                const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
                if (gAppState->tileVisibilities[tcIndex] > 0.0f && town_center_hit_test_screen(tc, gAppState->cursorScreen, gAppState->townCenterSpriteSize))
                {
                    clickedTCUUID = uuid;
                    break;
                }
            }

            if (clickedTCUUID != 0 && &gAppState->townCenters.at(clickedTCUUID) == selectedTC) {
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

void rebuild_blocked_tiles(EngineState& engine, AppState& appState)
{
	engine.blockedTileTranslations = blocked_tile_translations(appState);
	glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.blockedInstanceVBO);
	glBufferData(GL_ARRAY_BUFFER, engine.blockedTileTranslations.size() * sizeof(glm::vec2), engine.blockedTileTranslations.data(), GL_STATIC_DRAW);
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
        else if (gAppState->cursorMode == CursorMode::BuildEco)
        {
            std::cout << "Escape - exiting build eco mode" << std::endl;
            gAppState->cursorMode = CursorMode::Normal;
            gAppState->selectedBuilding = BuildableBuilding::None;
        }
        else if (gAppState->cursorMode == CursorMode::BuildMil)
        {
            std::cout << "Escape - exiting build mil mode" << std::endl;
            gAppState->cursorMode = CursorMode::Normal;
            gAppState->selectedBuilding = BuildableBuilding::None;
        }
        else
        {
            gAppState->cursorMode = CursorMode::Normal;
            clear_selection(*gAppState);
        }
    }

    if (key == GLFW_KEY_Q && action == GLFW_PRESS)
    {
        if (gAppState->cursorMode == CursorMode::Normal)
        {
            std::cout << "Q key pressed - entering build eco mode" << std::endl;
            gAppState->cursorMode = CursorMode::BuildEco;
            gAppState->selectedBuilding = BuildableBuilding::None;
        }
        else if (gAppState->cursorMode == CursorMode::BuildEco)
        {
            std::cout << "Q key pressed - selecting house" << std::endl;
            gAppState->selectedBuilding = BuildableBuilding::House;
        }
    }

    if (key == GLFW_KEY_W && action == GLFW_PRESS)
    {
        if (gAppState->cursorMode == CursorMode::Normal)
        {
            std::cout << "W key pressed - entering build mil mode" << std::endl;
            gAppState->cursorMode = CursorMode::BuildMil;
            gAppState->selectedBuilding = BuildableBuilding::None;
        }
    }

    if (key == GLFW_KEY_G && action == GLFW_PRESS)
    {
        std::cout << "G key pressed - stopping selected villagers" << std::endl;
        for (auto& [uuid, v] : gAppState->villagers)
        {
            if (v.selected)
            {
                v.operationQueue.clear();
                v.moving = false;
                v.targetPosition = v.position;
            }
        }
    }

    if (key == GLFW_KEY_DELETE && action == GLFW_PRESS)
    {
        std::vector<EntityId> housesToDelete;
        for (auto& [uuid, house] : gAppState->houses)
        {
            if (house.selected)
            {
                housesToDelete.push_back(uuid);
            }
        }
        for (EntityId houseUUID : housesToDelete)
        {
            House& house = gAppState->houses.at(houseUUID);
            // Reset villager if it was building this house
            if (house.assignedVillagerId != 0 && gAppState->villagers.count(house.assignedVillagerId) > 0)
            {
                Villager& v = gAppState->villagers.at(house.assignedVillagerId);
                v.isBuilding = false;
                // Remove any BUILD ops referencing this house from the villager's queue
                v.operationQueue.erase(
                    std::remove_if(v.operationQueue.begin(), v.operationQueue.end(),
                        [houseUUID](const QueuedOperation& op) {
                            return op.type == OperationType::BUILD && op.buildingId == houseUUID;
                        }),
                    v.operationQueue.end()
                );
            }

            // Remove the house
            gAppState->houses.erase(houseUUID);
            rebuild_blocked_tiles(*gEngine, *gAppState);
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
