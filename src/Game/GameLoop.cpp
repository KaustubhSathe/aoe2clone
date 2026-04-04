#include "GameLoop.h"
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
{
        appState.inGameTime += deltaTime * 1.7f;

        for (Villager& v : appState.villagers)
        {
            if (v.moving)
            {
                v.walkAnimTimer += deltaTime;
                const float frameTime = 1.0f / WALK_ANIMATION_FPS;
                while (v.walkAnimTimer >= frameTime)
                {
                    v.walkAnimTimer -= frameTime;
                    v.walkFrameIndex = (v.walkFrameIndex + 1) % WALK_FRAMES_PER_DIRECTION;
                }

                const glm::vec2 toTarget = v.targetPosition - v.position;
                const float remainingDistance = glm::length(toTarget);
                const float moveDistance = v.moveSpeed * deltaTime;

                if (remainingDistance <= moveDistance)
                {
                    if (remainingDistance > 0.001f)
                    {
                        v.facingDirection = glm::normalize(toTarget);
                    }
                    v.position = v.targetPosition;

                    // Pop the next queued waypoint and keep moving, or stop.
                    if (!v.waypointQueue.empty())
                    {
                        const glm::vec2 nextWP = v.waypointQueue.front();
                        v.waypointQueue.pop_front();
                        const glm::vec2 toNext = nextWP - v.position;
                        if (glm::length(toNext) > 1.0f)
                        {
                            v.targetPosition = nextWP;
                            v.facingDirection = glm::normalize(toNext);
                            v.moving = true;
                        }
                        else
                        {
                            v.moving = false;
                        }
                    }
                    else
                    {
                        v.moving = false;
                        if (v.isMovingToGarrison && v.targetTcIndex >= 0 && v.targetTcIndex < appState.townCenters.size())
                        {
                            TownCenter& tcTarget = appState.townCenters[v.targetTcIndex];
                            if (tcTarget.garrisonCount < tcTarget.maxGarrison)
                            {
                                tcTarget.garrisonCount++;
                                v.isGarrisoned = true;
                                v.garrisonTcIndex = v.targetTcIndex;
                                v.selected = false;
                            }
                            v.isMovingToGarrison = false;
                        }
                    }
                }
                else
                {
                    const glm::vec2 direction = toTarget / remainingDistance;
                    const glm::vec2 nextPosition = v.position + direction * moveDistance;
                    const std::optional<glm::ivec2> nextTile = world_to_tile(nextPosition);
                    if (nextTile.has_value() && is_tile_blocked(appState, *nextTile))
                    {
                        // Blocked dynamically! Try to find a new path to the final destination
                        glm::vec2 ultimateDest = v.waypointQueue.empty() ? v.targetPosition : v.waypointQueue.back();
                        std::vector<glm::vec2> newPath = find_path(appState, v.position, ultimateDest);
                        v.waypointQueue.clear();
                        
                        if (newPath.size() > 1)
                        {
                            v.targetPosition = newPath[1];
                            v.facingDirection = glm::normalize(v.targetPosition - v.position);
                            for (size_t i = 2; i < newPath.size(); ++i)
                            {
                                v.waypointQueue.push_back(newPath[i]);
                            }
                        }
                        else
                        {
                            // Can't reach destination, give up
                            v.targetPosition = v.position;
                            v.moving = false;
                        }
                    }
                    else
                    {
                        v.facingDirection = direction;
                        v.position = nextPosition;
                    }
                }
            }
            else
            {
                v.walkAnimTimer = 0.0f;
                v.walkFrameIndex = 0;
            }

            // If villager was walking to a building site, start building when arrived
            if (!v.moving && v.buildingTargetIndex >= 0 && !v.isBuilding)
            {
                v.isBuilding = true;
                v.builderFrameIndex = 0;
                v.builderAnimTimer = 0.0f;
            }

            // Handle building task progress
            if (v.isBuilding && v.buildingTargetIndex >= 0 && v.buildingTargetIndex < static_cast<int>(appState.houses.size()))
            {
                // Per-villager timer for builder animation
                v.builderAnimTimer += deltaTime;
                const float frameTime = 1.0f / WALK_ANIMATION_FPS;
                while (v.builderAnimTimer >= frameTime)
                {
                    v.builderAnimTimer -= frameTime;
                    v.builderFrameIndex = (v.builderFrameIndex + 1) % WALK_FRAMES_PER_DIRECTION;
                }

                House& house = appState.houses[v.buildingTargetIndex];
                if (house.isUnderConstruction && house.buildProgress < 1.0f)
                {
                    house.buildProgress += (deltaTime * 1.7f) / HOUSE_BUILD_TIME;
                    if (house.buildProgress >= 1.0f)
                    {
                        house.buildProgress = 1.0f;
                        house.isUnderConstruction = false;
                        house.assignedVillagerIndex = -1;
                        appState.housePopulationBonus += 5;
                        appState.maxPopulation = 5 + appState.housePopulationBonus;
                        v.isBuilding = false;
                        v.buildingTargetIndex = -1;
                    }
                }
            }
        }

        // ---------------------------------------------------------------------
        // Pending Build Handler - complete build setup when villagers have evacuated
        // ---------------------------------------------------------------------
        if (appState.pendingBuildInfo.villagerIndex >= 0 && appState.pendingBuildInfo.buildingIndex >= 0)
        {
            const int vIndex = appState.pendingBuildInfo.villagerIndex;
            const int bIndex = appState.pendingBuildInfo.buildingIndex;
            const glm::ivec2& tile = appState.pendingBuildInfo.targetTile;

            if (vIndex < static_cast<int>(appState.villagers.size()) &&
                bIndex < static_cast<int>(appState.houses.size()) &&
                !appState.villagers[vIndex].moving)
            {
                Villager& v = appState.villagers[vIndex];
                House& house = appState.houses[bIndex];

                // Convert ghost to real foundation - now it blocks tiles
                house.isGhostFoundation = false;
                rebuild_blocked_tiles(engine, appState);

                // Villager has arrived at evacuation tile - find path to build target
                float bestDist = 1e9f;
                glm::vec2 buildTarget = house.position;

                for (int dx = -1; dx <= 2; ++dx)
                {
                    for (int dy = -1; dy <= 2; ++dy)
                    {
                        if (dx >= 0 && dx <= 1 && dy >= 0 && dy <= 1) continue;
                        glm::ivec2 p(tile.x + dx, tile.y + dy);
                        if (p.x >= 0 && p.x < GRID_SIZE && p.y >= 0 && p.y < GRID_SIZE && !is_tile_blocked(appState, p))
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

                std::vector<glm::vec2> path = find_path(appState, v.position, buildTarget);
                v.waypointQueue.clear();
                if (path.size() > 1)
                {
                    v.targetPosition = path[1];
                    v.facingDirection = glm::normalize(v.targetPosition - v.position);
                    v.moving = true;
                    for (size_t i = 2; i < path.size(); ++i)
                    {
                        v.waypointQueue.push_back(path[i]);
                    }
                }
                else if (path.size() == 1)
                {
                    v.targetPosition = path[0];
                    v.facingDirection = glm::normalize(v.targetPosition - v.position);
                    v.moving = !glm::all(glm::equal(v.targetPosition, v.position));
                }

                // If villager is already at the build target, start building immediately
                if (glm::length(buildTarget - v.position) < 1.0f)
                {
                    v.isBuilding = true;
                    v.builderFrameIndex = 0;
                    v.builderAnimTimer = 0.0f;
                }

                BuildTask task;
                task.villagerIndex = vIndex;
                task.buildingIndex = bIndex;
                task.targetTile = tile;
                appState.buildTasks.push_back(task);

                // Clear pending build
                appState.pendingBuildInfo.villagerIndex = -1;
                appState.pendingBuildInfo.buildingIndex = -1;
            }
        }

        // ---------------------------------------------------------------------
        // Villager Collision Resolution (Soft separation)
        // ---------------------------------------------------------------------
        constexpr float VILLAGER_RADIUS = 12.0f;
        for (size_t i = 0; i < appState.villagers.size(); ++i)
        {
            for (size_t j = i + 1; j < appState.villagers.size(); ++j)
            {
                glm::vec2 delta = appState.villagers[i].position - appState.villagers[j].position;
                float dist = glm::length(delta);
                if (dist < 0.001f)
                {
                    delta = glm::vec2(0.1f, 0.0f);
                    dist = 0.1f;
                }
                
                if (dist < VILLAGER_RADIUS * 2.0f)
                {
                    float overlap = (VILLAGER_RADIUS * 2.0f) - dist;
                    glm::vec2 push = (delta / dist) * overlap * 5.0f * deltaTime;
                    appState.villagers[i].position += push;
                    appState.villagers[j].position -= push;
                }
            }
        }

        // ---------------------------------------------------------------------
        // Process Town Center Logic
        // ---------------------------------------------------------------------
        for (size_t tcIdx = 0; tcIdx < appState.townCenters.size(); ++tcIdx)
        {
            TownCenter& tc = appState.townCenters[tcIdx];
            if (tc.villagerQueueCount > 0)
            {
                // Pause training if population is full
                if (static_cast<int>(appState.villagers.size()) >= appState.maxPopulation)
                {
                    // Don't increment timer when population is full
                }
                else
                {
                    tc.villagerTrainingTimer += deltaTime;
                }

                if (tc.villagerTrainingTimer >= 14.7f)
                {
                    // Check if population limit reached (shouldn't happen due to pause above, but safe check)
                    if (static_cast<int>(appState.villagers.size()) >= appState.maxPopulation)
                    {
                        // Population full, don't create villager - keep it in queue
                        tc.villagerTrainingTimer = 0.0f;
                        continue;
                    }
                    tc.villagerQueueCount--;
                    tc.villagerTrainingTimer = 0.0f;
                    
                    Villager v;
                    glm::ivec2 vTile = glm::ivec2(tc.tile.x - 1, tc.tile.y + 5); // Fallback
                    float bestDist = 1e9f;
                    glm::vec2 targetWorld = tc.hasGatherPoint ? tc.gatherPoint : tc.position;

                    for (int x = -1; x <= 4; ++x)
                    {
                        for (int y = -1; y <= 4; ++y)
                        {
                            if (x >= 0 && x <= 3 && y >= 0 && y <= 3) continue; // Inside TC
                            glm::ivec2 p(tc.tile.x + x, tc.tile.y + y);
                            if (p.x >= 0 && p.x < GRID_SIZE && p.y >= 0 && p.y < GRID_SIZE && !is_tile_blocked(appState, p))
                            {
                                float dist = glm::length(tile_to_world(p) - targetWorld);
                                if (dist < bestDist)
                                {
                                    bestDist = dist;
                                    vTile = p;
                                }
                            }
                        }
                    }

                    v.position = tile_to_world(vTile);
                    if (tc.gatherPointIsSelf && tc.garrisonCount < tc.maxGarrison)
                    {
                        tc.garrisonCount++;
                        v.isGarrisoned = true;
                        v.garrisonTcIndex = static_cast<int>(tcIdx);
                        v.selected = false;
                        v.targetPosition = tc.position;
                        v.position = tc.position;
                    }
                    else if (tc.hasGatherPoint)
                    {
                        const glm::vec2 toGP = tc.gatherPoint - v.position;
                        if (glm::length(toGP) > 1.0f)
                        {
                            std::vector<glm::vec2> path = find_path(appState, v.position, tc.gatherPoint);
                            if (path.size() > 1)
                            {
                                v.targetPosition = path[1];
                                v.facingDirection = glm::normalize(v.targetPosition - v.position);
                                v.moving = true;
                                for (size_t h = 2; h < path.size(); ++h)
                                {
                                    v.waypointQueue.push_back(path[h]);
                                }
                            }
                            else if (path.size() == 1)
                            {
                                v.targetPosition = path[0];
                                v.facingDirection = glm::normalize(v.targetPosition - v.position);
                                v.moving = true;
                            }
                            else
                            {
                                v.targetPosition = v.position;
                            }
                        }
                        else
                        {
                            v.targetPosition = v.position;
                        }
                    }
                    else
                    {
                        v.targetPosition = v.position;
                    }
                    appState.villagers.push_back(v);
                }
            }
            else
            {
                tc.villagerTrainingTimer = 0.0f;
            }
        }


        // ---------------------------------------------------------------------
        // Line of Sight Update (Fog of War)
        // Recompute the 4-tile circular radius around the villager each frame.
        // ---------------------------------------------------------------------
        std::fill(appState.visible.begin(), appState.visible.end(), false);
        
        // Villager visibility
        for (const Villager& v : appState.villagers)
        {
            const std::optional<glm::ivec2> tile = world_to_tile(v.position);
            if (tile.has_value())
            {
                const int cx = tile->x;
                const int cy = tile->y;
                const int radiusSquare = 4 * 4;
                for (int dy = -4; dy <= 4; dy++)
                {
                    for (int dx = -4; dx <= 4; dx++)
                    {
                        if (dx * dx + dy * dy <= radiusSquare)
                        {
                            const int tx = cx + dx;
                            const int ty = cy + dy;
                            if (tx >= 0 && tx < GRID_SIZE && ty >= 0 && ty < GRID_SIZE)
                            {
                                const int index = ty * GRID_SIZE + tx;
                                appState.visible[index] = true;
                                appState.explored[index] = true;
                            }
                        }
                    }
                }
            }
        }

        // Town Center visibility
        for (const TownCenter& tc : appState.townCenters)
        {
            // Calculate pseudo center of 4x4
            const int cx = tc.tile.x + 2; 
            const int cy = tc.tile.y + 2;
            const int radiusSquare = 8 * 8;
            for (int dy = -8; dy <= 8; dy++)
            {
                for (int dx = -8; dx <= 8; dx++)
                {
                    if (dx * dx + dy * dy <= radiusSquare)
                    {
                        const int tx = cx + dx;
                        const int ty = cy + dy;
                        if (tx >= 0 && tx < GRID_SIZE && ty >= 0 && ty < GRID_SIZE)
                        {
                            const int index = ty * GRID_SIZE + tx;
                            appState.visible[index] = true;
                            appState.explored[index] = true;
                        }
                    }
                }
            }
        }
        
        for (int i = 0; i < GRID_SIZE * GRID_SIZE; i++)
        {
            if (appState.visible[i])
            {
                appState.tileVisibilities[i] = 1.0f;
                appState.minimapPixels[i] = 0xFF00AA00; // Bright green (AABBGGRR)
            }
            else if (appState.explored[i])
            {
                appState.tileVisibilities[i] = 0.5f;
                appState.minimapPixels[i] = 0xFF003300; // Dark green (AABBGGRR)
            }
            else
            {
                appState.tileVisibilities[i] = 0.0f;
                appState.minimapPixels[i] = 0xFF000000; // Opaque black
            }
        }
        
        // Draw trees onto minimap
        for (const PineTree& tree : appState.pineTrees)
        {
            const int index = tree.tile.y * GRID_SIZE + tree.tile.x;
            if (appState.explored[index])
            {
                appState.minimapPixels[index] = 0xFF004400; // Even darker green for trees
            }
        }

        // Draw town centers onto minimap
        for (const TownCenter& tc : appState.townCenters)
        {
            for (int dy = 0; dy < 4; ++dy)
            {
                for (int dx = 0; dx < 4; ++dx)
                {
                    if (tc.tile.y + dy < GRID_SIZE && tc.tile.x + dx < GRID_SIZE)
                    {
                        const int index = (tc.tile.y + dy) * GRID_SIZE + (tc.tile.x + dx);
                        if (appState.explored[index])
                        {
                            appState.minimapPixels[index] = 0xFFFF0000; // Blue (AABBGGRR)
                        }
                    }
                }
            }
        }

        // Draw villagers onto minimap
        for (const Villager& v : appState.villagers)
        {
            if (v.isGarrisoned) continue;
            const std::optional<glm::ivec2> vTile = world_to_tile(v.position);
            if (vTile.has_value())
            {
                const int index = vTile->y * GRID_SIZE + vTile->x;
                appState.minimapPixels[index] = 0xFFFFFF00; // Cyan
            }
        }
        
        glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.visibilityVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, appState.tileVisibilities.size() * sizeof(float), appState.tileVisibilities.data());

        glBindTexture(GL_TEXTURE_2D, appState.minimapTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GRID_SIZE, GRID_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, appState.minimapPixels.data());


}

void RenderScene(EngineState& engine, AppState& appState)
{
        const int selectionSegments = 32;
        const glm::vec2 spriteScale = glm::vec2(72.0f, 72.0f);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const float halfW = (static_cast<float>(SCR_WIDTH) / 2.0f) / zoom;
        const float halfH = (static_cast<float>(SCR_HEIGHT) / 2.0f) / zoom;
        const glm::mat4 projection = glm::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
        const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-cameraX, -cameraY, 0.0f));
        const glm::mat4 screenProjection = glm::ortho(0.0f, static_cast<float>(SCR_WIDTH), static_cast<float>(SCR_HEIGHT), 0.0f, -1.0f, 1.0f);
        const glm::mat4 identityView = glm::mat4(1.0f);

        glUseProgram(engine.gpu.tileShaderProgram);
        glUniformMatrix4fv(engine.gpu.tileProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(engine.gpu.tileViewLoc, 1, GL_FALSE, glm::value_ptr(view));

        glBindVertexArray(engine.gpu.tileVAO);
        glUniform4f(engine.gpu.tileColorLoc, 0.2f, 0.6f, 0.2f, 1.0f);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GRID_SIZE * GRID_SIZE);

        if (!engine.blockedTileTranslations.empty())
        {
            glBindVertexArray(engine.gpu.blockedTileVAO);
            glUniform4f(engine.gpu.tileColorLoc, 0.09f, 0.18f, 0.09f, 1.0f);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(engine.blockedTileTranslations.size()));
        }

        glBindVertexArray(engine.gpu.outlineVAO);
        glUniform4f(engine.gpu.tileColorLoc, 0.1f, 0.4f, 0.1f, 1.0f);
        glDrawArraysInstanced(GL_LINE_LOOP, 0, 4, GRID_SIZE * GRID_SIZE);

        // ---------------------------------------------------------------------
        // Render Sprites (Depth Sorted)
        // isometric depth sorting: higher world Y is drawn first (further away).
        // ---------------------------------------------------------------------
        struct RenderPayload {
            int type; // 0=PineTree, 1=TownCenter, 2=Villager, 3=House
            size_t index;
            float sortY;
        };
        std::vector<RenderPayload> renderQueue;
        
        if (engine.pineTreeFrame.has_value())
        {
            for (size_t i = 0; i < appState.pineTrees.size(); i++)
            {
                const PineTree& pt = appState.pineTrees[i];
                if (appState.tileVisibilities[pt.tile.y * GRID_SIZE + pt.tile.x] > 0.0f)
                {
                    // Sort anchored at the bottom of the tree
                    renderQueue.push_back({0, i, pt.position.y + PINE_RENDER_OFFSET.y});
                }
            }
        }
        
        if (engine.townCenterFrame.has_value())
        {
            for (size_t i = 0; i < appState.townCenters.size(); i++)
            {
                const TownCenter& tc = appState.townCenters[i];
                const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
                if (tcIndex >= 0 && tcIndex < GRID_SIZE * GRID_SIZE && appState.tileVisibilities[tcIndex] > 0.0f)
                {
                    renderQueue.push_back({1, i, tc.position.y + TOWN_CENTER_RENDER_OFFSET.y});
                }
            }
        }

        // Houses use houseIcon for rendering (same as UI icon)
        for (size_t i = 0; i < appState.houses.size(); i++)
        {
            const House& house = appState.houses[i];
            const int houseIndex = house.tile.y * GRID_SIZE + house.tile.x;
            if (houseIndex >= 0 && houseIndex < GRID_SIZE * GRID_SIZE && appState.tileVisibilities[houseIndex] > 0.0f)
            {
                renderQueue.push_back({3, i, house.position.y + HOUSE_RENDER_OFFSET.y});
            }
        }
        
        for (size_t i = 0; i < appState.villagers.size(); i++)
        {
            if (!appState.villagers[i].isGarrisoned)
            {
                renderQueue.push_back({2, i, appState.villagers[i].position.y});
            }
        }
        
        std::sort(renderQueue.begin(), renderQueue.end(), [](const RenderPayload& a, const RenderPayload& b) {
            return a.sortY > b.sortY;
        });

        // --- Phase 1: Draw Sprites ---
        glUseProgram(engine.gpu.spriteShaderProgram);
        glUniformMatrix4fv(engine.gpu.spriteProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(engine.gpu.spriteViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glBindVertexArray(engine.gpu.spriteVAO);
        
        for (const RenderPayload& payload : renderQueue)
        {
            if (payload.type == 0) // Pine Tree
            {
                const PineTree& pt = appState.pineTrees[payload.index];
                const float treeVis = appState.tileVisibilities[pt.tile.y * GRID_SIZE + pt.tile.x];
                glUniform2f(engine.gpu.spriteSizeLoc, appState.pineTreeSpriteSize.x, appState.pineTreeSpriteSize.y);
                glUniform1f(engine.gpu.spriteVisLoc, treeVis);
                
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, engine.pineTreeFrame->texture);
                glUniform2f(engine.gpu.spritePosLoc, pt.position.x + PINE_RENDER_OFFSET.x, pt.position.y + PINE_RENDER_OFFSET.y);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            else if (payload.type == 1) // Town Center
            {
                const TownCenter& tc = appState.townCenters[payload.index];
                const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
                glUniform2f(engine.gpu.spriteSizeLoc, appState.townCenterSpriteSize.x, appState.townCenterSpriteSize.y);
                glUniform1f(engine.gpu.spriteVisLoc, appState.tileVisibilities[tcIndex]);
                
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, engine.townCenterFrame->texture);
                glUniform2f(engine.gpu.spritePosLoc, tc.position.x + TOWN_CENTER_RENDER_OFFSET.x, tc.position.y + TOWN_CENTER_RENDER_OFFSET.y);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            else if (payload.type == 2) // Villager
            {
                const Villager& v = appState.villagers[payload.index];
                const AnimationSet& activeAnimation = v.isBuilding ? engine.builderAnimation : (v.moving ? engine.walkAnimation : engine.idleAnimation);
                if (!activeAnimation.frames.empty())
                {
                    int frameIndex = 0;
                    if (v.isBuilding)
                    {
                        // Builder animation uses direction-based frame selection
                        const int builderFrameCount = static_cast<int>(engine.builderAnimation.frames.size());
                        if (builderFrameCount == 0)
                        {
                            frameIndex = 0;
                        }
                        else
                        {
                            int dirGroup = walk_direction_group_from_direction(v.facingDirection);
                            frameIndex = dirGroup * WALK_FRAMES_PER_DIRECTION + (v.builderFrameIndex % WALK_FRAMES_PER_DIRECTION);
                            if (frameIndex >= builderFrameCount) frameIndex = frameIndex % builderFrameCount;
                        }
                    }
                    else if (v.moving)
                    {
                        frameIndex = walk_animation_index(v.facingDirection, v.walkFrameIndex, static_cast<int>(engine.walkAnimation.frames.size()));
                    }
                    else
                    {
                        frameIndex = facing_index_from_direction(v.facingDirection, static_cast<int>(engine.idleAnimation.frames.size()));
                    }
                    const TextureFrame& activeFrame = activeAnimation.frames[static_cast<size_t>(frameIndex)];

                    glUniform2f(engine.gpu.spriteSizeLoc, spriteScale.x, spriteScale.y);
                    glUniform1f(engine.gpu.spriteVisLoc, 1.0f); // Always lit

                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, activeFrame.texture);
                    // Standard offset used by Villagers (they pivot around TILE_HALF_HEIGHT)
                    glUniform2f(engine.gpu.spritePosLoc, v.position.x, v.position.y - TILE_HALF_HEIGHT);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                }
            }
            else if (payload.type == 3) // House
            {
                const House& house = appState.houses[payload.index];
                const int houseIndex = house.tile.y * GRID_SIZE + house.tile.x;
                glUniform2f(engine.gpu.spriteSizeLoc, appState.houseSpriteSize.x, appState.houseSpriteSize.y);
                glUniform1f(engine.gpu.spriteVisLoc, appState.tileVisibilities[houseIndex]);

                GLuint houseTexture;
                if (house.isUnderConstruction)
                {
                    if (house.buildProgress < 0.33f)
                        houseTexture = engine.houseStage0.texture;
                    else if (house.buildProgress < 0.66f)
                        houseTexture = engine.houseStage1.texture;
                    else if (house.buildProgress < 1.0f)
                        houseTexture = engine.houseStage2.texture;
                    else
                        houseTexture = engine.houseStage3.texture;
                }
                else
                {
                    houseTexture = engine.houseStage3.texture;
                }

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, houseTexture);
                glUniform2f(engine.gpu.spritePosLoc, house.position.x + HOUSE_RENDER_OFFSET.x, house.position.y + HOUSE_RENDER_OFFSET.y);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }
        
        // --- Phase 2: Draw Overlays (Selections, Debug Bounds, Waypoints) ---
        glUseProgram(engine.gpu.overlayShaderProgram);
        glUniformMatrix4fv(engine.gpu.overlayProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(engine.gpu.overlayViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        
        for (const PineTree& pt : appState.pineTrees)
        {
            if (pt.selected)
            {
                const glm::vec2 pineTreeOrigin = pt.position + PINE_RENDER_OFFSET;
                glUniform2f(engine.gpu.overlayOffsetLoc, pineTreeOrigin.x, pineTreeOrigin.y);
                glUniform4f(engine.gpu.overlayColorLoc, 1.0f, 0.1f, 0.1f, 1.0f);
                glBindVertexArray(engine.gpu.pineBoundsVAO);
                glDrawArrays(GL_LINE_LOOP, 0, 4);

                glUniform2f(engine.gpu.overlayOffsetLoc, pt.position.x + PINE_RENDER_OFFSET.x, pt.position.y - 4.0f);
                glUniform4f(engine.gpu.overlayColorLoc, 0.95f, 0.18f, 0.18f, 1.0f);
                glBindVertexArray(engine.gpu.selectionVAO);
                glDrawArrays(GL_LINE_LOOP, 0, selectionSegments);
            }
        }
        
        for (const TownCenter& tc : appState.townCenters)
        {
            const int tcIndex = (tc.tile.y + 2) * GRID_SIZE + (tc.tile.x + 2);
            if (tcIndex >= 0 && tcIndex < GRID_SIZE * GRID_SIZE && appState.tileVisibilities[tcIndex] > 0.0f) 
            {
                glm::vec2 renderPos = tc.position + TOWN_CENTER_RENDER_OFFSET;
                glUniform2f(engine.gpu.overlayOffsetLoc, renderPos.x, renderPos.y);
                glUniform4f(engine.gpu.overlayColorLoc, 1.0f, 0.0f, 0.0f, 1.0f);
                const float hw = appState.townCenterSpriteSize.x * 0.5f;
                const float h = appState.townCenterSpriteSize.y;
                const float baseH = hw;
                const float borderPolygon[] = {
                    0.0f, 0.0f,
                    hw, baseH * 0.5f,
                    hw, h - baseH * 0.5f,
                    0.0f, h,
                    -hw, h - baseH * 0.5f,
                    -hw, baseH * 0.5f
                };
                glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.rectVBO);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(borderPolygon), borderPolygon);
                glBindVertexArray(engine.gpu.rectVAO);
                glDrawArrays(GL_LINE_LOOP, 0, 6);
                
                if (tc.selected)
                {
                    // Placeholder for actual town center selection circle
                }

                if (tc.selected && tc.hasGatherPoint)
                {
                    glUniform2f(engine.gpu.overlayOffsetLoc, tc.gatherPoint.x, tc.gatherPoint.y);
                    glUniform4f(engine.gpu.overlayColorLoc, 1.0f, 0.0f, 0.0f, 1.0f); // Red
                    glBindVertexArray(engine.gpu.tileVAO);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                }
            }
        }

        for (const House& house : appState.houses)
        {
            const int houseIndex = house.tile.y * GRID_SIZE + house.tile.x;
            if (houseIndex >= 0 && houseIndex < GRID_SIZE * GRID_SIZE && appState.tileVisibilities[houseIndex] > 0.0f)
            {
                if (house.selected)
                {
                    // Calculate center of 2x2 tile footprint with padding
                    glm::vec2 centerPos = tile_to_world(house.tile + glm::ivec2(1, 1));

                    // Draw 2x2 isometric selection with padding (white color)
                    const float padding = 10.0f;
                    const float hw = (TILE_HALF_WIDTH * 2.0f) + padding;
                    const float hh = (TILE_HALF_HEIGHT * 2.0f) + padding;
                    const float borderPolygon[] = {
                        0.0f,    hh,
                        hw,     0.0f,
                        0.0f,    -hh,
                        -hw,    0.0f
                    };
                    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.rectVBO);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(borderPolygon), borderPolygon);
                    glUniform2f(engine.gpu.overlayOffsetLoc, centerPos.x, centerPos.y);
                    glUniform4f(engine.gpu.overlayColorLoc, 1.0f, 1.0f, 1.0f, 1.0f);
                    glBindVertexArray(engine.gpu.rectVAO);
                    glDrawArrays(GL_LINE_LOOP, 0, 4);
                }
            }
        }

        // Render pending build tile highlight (2x2 green/red filled)
        if (appState.pendingBuildTile.x >= 0 && appState.pendingBuildTile.y >= 0)
        {
            // The pendingBuildTile is the cursor's tile snapped to grid
            // For a 2x2 footprint, the highlight should be centered on the cursor tile
            glm::ivec2 tile = appState.pendingBuildTile;
            glm::vec2 centerWorld = tile_to_world(tile) + HIGHLIGHT_OFFSET;

            // 2x2 isometric tile diamond - vertices relative to center
            const float hw2 = TILE_HALF_WIDTH * 2.0f;
            const float hh2 = TILE_HALF_HEIGHT * 2.0f;
            const float highlightPoly[] = {
                0.0f,    hh2,      // Top
                hw2,     0.0f,     // Right
                0.0f,    -hh2,     // Bottom
                -hw2,    0.0f      // Left
            };

            glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.rectVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(highlightPoly), highlightPoly);
            glUniform2f(engine.gpu.overlayOffsetLoc, centerWorld.x, centerWorld.y);
            if (appState.canBuildAtPendingTile)
                glUniform4f(engine.gpu.overlayColorLoc, 0.0f, 1.0f, 0.0f, 0.4f); // Green
            else
                glUniform4f(engine.gpu.overlayColorLoc, 1.0f, 0.0f, 0.0f, 0.4f); // Red
            glBindVertexArray(engine.gpu.rectVAO);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }

        for (const Villager& v : appState.villagers)
        {
            if (v.selected)
            {
                glUniform2f(engine.gpu.overlayOffsetLoc, v.position.x, v.position.y - 22.0f);
                glUniform4f(engine.gpu.overlayColorLoc, 0.1f, 1.0f, 0.1f, 1.0f);
                glBindVertexArray(engine.gpu.selectionVAO);
                glDrawArrays(GL_LINE_LOOP, 0, selectionSegments);
            }

            // Render build progress bar for building villagers
            if (v.isBuilding && v.buildingTargetIndex >= 0 && v.buildingTargetIndex < static_cast<int>(appState.houses.size()))
            {
                const House& house = appState.houses[v.buildingTargetIndex];
                if (house.isUnderConstruction)
                {
                    // Progress bar background
                    const float barWidth = 30.0f;
                    const float barHeight = 4.0f;
                    const float bgPoly[] = {
                        -barWidth, 0.0f,
                         barWidth, 0.0f,
                         barWidth, barHeight,
                        -barWidth, barHeight
                    };
                    glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.rectVBO);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(bgPoly), bgPoly);
                    glUniform2f(engine.gpu.overlayOffsetLoc, v.position.x, v.position.y - 40.0f);
                    glUniform4f(engine.gpu.overlayColorLoc, 0.3f, 0.3f, 0.3f, 0.8f);
                    glBindVertexArray(engine.gpu.rectVAO);
                    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

                    // Progress bar fill
                    float progressWidth = barWidth * 2.0f * house.buildProgress;
                    if (progressWidth > 0.0f)
                    {
                        const float fillPoly[] = {
                            -barWidth, 0.0f,
                            -barWidth + progressWidth, 0.0f,
                            -barWidth + progressWidth, barHeight,
                            -barWidth, barHeight
                        };
                        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(fillPoly), fillPoly);
                        glUniform2f(engine.gpu.overlayOffsetLoc, v.position.x, v.position.y - 40.0f);
                        glUniform4f(engine.gpu.overlayColorLoc, 0.0f, 0.8f, 0.0f, 1.0f);
                        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
                    }
                }
            }

            if (v.selected && v.moving)
            {
                std::vector<glm::vec2> allWPs;
                allWPs.reserve(1 + v.waypointQueue.size());
                allWPs.push_back(v.targetPosition);
                for (const glm::vec2& wp : v.waypointQueue)
                {
                    allWPs.push_back(wp);
                }

                static unsigned int wpVAO = 0;
                static unsigned int wpVBO = 0;
                if (wpVAO == 0)
                {
                    const float wpHW = 8.0f;
                    const float wpHH = 4.0f;
                    const float diamond[] = {
                         0.0f,  wpHH,
                         wpHW,  0.0f,
                         0.0f, -wpHH,
                        -wpHW,  0.0f
                    };
                    glGenVertexArrays(1, &wpVAO);
                    glGenBuffers(1, &wpVBO);
                    glBindVertexArray(wpVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, wpVBO);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(diamond), diamond, GL_STATIC_DRAW);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
                }

                {
                    std::vector<float> lineVerts;
                    lineVerts.reserve((1 + allWPs.size()) * 2);
                    lineVerts.push_back(v.position.x);
                    lineVerts.push_back(v.position.y);
                    for (const glm::vec2& wp : allWPs)
                    {
                        lineVerts.push_back(wp.x);
                        lineVerts.push_back(wp.y);
                    }
                    static unsigned int lineVAO = 0;
                    static unsigned int lineVBO = 0;
                    if (lineVAO == 0)
                    {
                        glGenVertexArrays(1, &lineVAO);
                        glGenBuffers(1, &lineVBO);
                        glBindVertexArray(lineVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
                        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
                        glEnableVertexAttribArray(0);
                        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
                    }
                    glBindVertexArray(lineVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
                    glBufferData(GL_ARRAY_BUFFER,
                        static_cast<GLsizeiptr>(lineVerts.size() * sizeof(float)),
                        lineVerts.data(), GL_DYNAMIC_DRAW);
                    glUniform2f(engine.gpu.overlayOffsetLoc, 0.0f, 0.0f);
                    glUniform4f(engine.gpu.overlayColorLoc, 1.0f, 1.0f, 0.3f, 0.55f);
                    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(lineVerts.size() / 2));
                }

                glBindVertexArray(wpVAO);
                for (size_t i = 0; i < allWPs.size(); ++i)
                {
                    const glm::vec2& wp = allWPs[i];
                    glUniform2f(engine.gpu.overlayOffsetLoc, wp.x, wp.y);
                    if (i == 0)
                        glUniform4f(engine.gpu.overlayColorLoc, 1.0f, 1.0f, 0.0f, 1.0f);
                    else
                        glUniform4f(engine.gpu.overlayColorLoc, 1.0f, 0.85f, 0.1f, 0.75f);
                    glDrawArrays(GL_LINE_LOOP, 0, 4);
                }
            }
        }

        if (appState.selection.dragging && appState.selection.moved)
        {
            const float rectVertices[] = {
                static_cast<float>(appState.selection.startScreen.x), static_cast<float>(appState.selection.startScreen.y),
                static_cast<float>(appState.selection.currentScreen.x), static_cast<float>(appState.selection.startScreen.y),
                static_cast<float>(appState.selection.currentScreen.x), static_cast<float>(appState.selection.currentScreen.y),
                static_cast<float>(appState.selection.startScreen.x), static_cast<float>(appState.selection.currentScreen.y)
            };

            glBindBuffer(GL_ARRAY_BUFFER, engine.gpu.rectVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(rectVertices), rectVertices);

            glUseProgram(engine.gpu.overlayShaderProgram);
            glUniformMatrix4fv(engine.gpu.overlayProjLoc, 1, GL_FALSE, glm::value_ptr(screenProjection));
            glUniformMatrix4fv(engine.gpu.overlayViewLoc, 1, GL_FALSE, glm::value_ptr(identityView));
            glUniform2f(engine.gpu.overlayOffsetLoc, 0.0f, 0.0f);
            glBindVertexArray(engine.gpu.rectVAO);
            glUniform4f(engine.gpu.overlayColorLoc, 0.2f, 0.8f, 0.3f, 0.18f);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glUniform4f(engine.gpu.overlayColorLoc, 0.4f, 1.0f, 0.5f, 1.0f);
            glDrawArrays(GL_LINE_LOOP, 0, 4);
        }

}

static void UpdateFPS(FPSState& fps, float deltaTime)
{
    fps.updateTimer += deltaTime;
    fps.accumulatedTime -= fps.frameTimes[fps.sampleIndex];
    fps.frameTimes[fps.sampleIndex] = deltaTime;
    fps.accumulatedTime += deltaTime;
    fps.sampleIndex = (fps.sampleIndex + 1) % FPSState::SAMPLE_COUNT;

    if (fps.updateTimer >= FPSState::UPDATE_INTERVAL)
    {
        fps.updateTimer = 0.0f;
        const float avgDelta = fps.accumulatedTime / FPSState::SAMPLE_COUNT;
        fps.currentFPS = static_cast<int>(0.5f + (1.0f / avgDelta));
    }
}

void RenderUI(EngineState& engine, AppState& appState)
{
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 34.0f));
        ImGui::SetNextWindowBgAlpha(0.84f);
        ImGuiWindowFlags resourceFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("Resources", nullptr, resourceFlags);
        int popCount = appState.villagers.size();
        int idleCount = 0;
        for (const auto& v : appState.villagers) {
            // For now, any moving or non-moving villager is idle as long as they are not garrisoned.
            if (!v.isGarrisoned) {
                idleCount++;
            }
        }

        int totalSeconds = static_cast<int>(appState.inGameTime);
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;

        ImGui::SetCursorPos(ImVec2(16.0f, 9.0f));
        ImGui::Text("Wood: %d", appState.wood);
        ImGui::SameLine(120.0f);
        ImGui::Text("Food: %d", appState.food);
        ImGui::SameLine(220.0f);
        ImGui::Text("Stone: %d", appState.stone);
        ImGui::SameLine(330.0f);
        ImGui::Text("Gold: %d", appState.gold);

        ImGui::SameLine(440.0f);
        bool isPopFull = popCount >= appState.maxPopulation;
        if (isPopFull)
        {
            // Blink red background when population is full
            float blink = static_cast<float>(sin(ImGui::GetTime() * 6.0f) > 0.0);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, blink, blink, 1.0f));
        }
        ImGui::Text("Pop: %d/%d", popCount, appState.maxPopulation);
        if (isPopFull)
        {
            ImGui::PopStyleColor();
        }
        ImGui::SameLine(540.0f);
        ImGui::Text("Idle: %d", idleCount);
        ImGui::SameLine(640.0f);
        
        const char* ageNames[] = { "Unknown", "Age I", "Age II", "Age III", "Age IV" };
        const char* currentAgeStr = (appState.currentAge >= 1 && appState.currentAge <= 4) ? ageNames[appState.currentAge] : ageNames[0];
        ImGui::Text("%s", currentAgeStr);
        
        char timeStr[64];
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d (Normal - 1.7)", hours, minutes, seconds);

        char fpsStr[32];
        snprintf(fpsStr, sizeof(fpsStr), "FPS: %d", appState.fps.currentFPS);

        UpdateFPS(appState.fps, deltaTime);

        float timeWidth = ImGui::CalcTextSize(timeStr).x;
        float fpsWidth = ImGui::CalcTextSize(fpsStr).x;
        float gap = 24.0f;
        float totalWidth = timeWidth + gap + fpsWidth;

        ImGui::SameLine((viewport->Size.x - totalWidth) * 0.5f);
        ImGui::Text("%s", timeStr);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + gap);
        ImGui::Text("%s", fpsStr);

        ImGui::End();

        // ---------------------------------------------------------------------
        // Bottom UI Panel
        // ---------------------------------------------------------------------
        const float bottomPanelHeight = 180.0f;
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - bottomPanelHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, bottomPanelHeight));
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGuiWindowFlags bottomFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        
        // Remove default window padding so the table touches the absolute top and bottom edges
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("BottomPanel", nullptr, bottomFlags);
        ImGui::PopStyleVar();
        
        // Force the table to take up the full panel height so separators draw to the bottom
        if (ImGui::BeginTable("bottom_table", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit, ImVec2(0.0f, bottomPanelHeight)))
        {
            ImGui::TableSetupColumn("Commands", ImGuiTableColumnFlags_WidthFixed, 320.0f);
            ImGui::TableSetupColumn("Selection Info", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Minimap", ImGuiTableColumnFlags_WidthFixed, 320.0f);
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();

            int selectedVillagerCount = 0;
            Villager* firstSelectedVillager = nullptr;
            for (Villager& v : appState.villagers) 
            {
                if (v.selected) 
                {
                    selectedVillagerCount++;
                    if (!firstSelectedVillager) firstSelectedVillager = &v;
                }
            }

            TownCenter* firstSelectedTC = nullptr;
            for (TownCenter& tc : appState.townCenters)
            {
                if (tc.selected)
                {
                    firstSelectedTC = &tc;
                    break;
                }
            }

            // --- Column 0: Commands ---
            ImGui::TableSetColumnIndex(0);
            if (firstSelectedVillager)
            {
                if (appState.cursorMode == CursorMode::BuildEco)
                {
                    // Show economic buildings
                    if (ImGui::ImageButton("house", (ImTextureID)(intptr_t)engine.houseIcon.texture, ImVec2(40, 40)))
                    {
                        appState.selectedBuilding = BuildableBuilding::House;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("House (Q)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("mill", (ImTextureID)(intptr_t)engine.millIcon.texture, ImVec2(40, 40)))
                    {
                        appState.selectedBuilding = BuildableBuilding::Mill;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mill (W)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("mining_camp", (ImTextureID)(intptr_t)engine.miningCampIcon.texture, ImVec2(40, 40)))
                    {
                        appState.selectedBuilding = BuildableBuilding::MiningCamp;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mining Camp (E)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("lumber_camp", (ImTextureID)(intptr_t)engine.lumberCampIcon.texture, ImVec2(40, 40)))
                    {
                        appState.selectedBuilding = BuildableBuilding::LumberCamp;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lumber Camp (R)");
                }
                else if (appState.cursorMode == CursorMode::BuildMil)
                {
                    // Show military buildings
                    if (ImGui::ImageButton("barracks", (ImTextureID)(intptr_t)engine.barracksIcon.texture, ImVec2(40, 40))) { }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Barracks (Q)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("archery_range", (ImTextureID)(intptr_t)engine.archeryRangeIcon.texture, ImVec2(40, 40))) { }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Archery Range (W)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("stable", (ImTextureID)(intptr_t)engine.stableIcon.texture, ImVec2(40, 40))) { }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stable (E)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("siege_workshop", (ImTextureID)(intptr_t)engine.siegeWorkshopIcon.texture, ImVec2(40, 40))) { }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Siege Workshop (R)");
                }
                else
                {
                    // Show normal command buttons
                    if (ImGui::ImageButton("build_eco", (ImTextureID)(intptr_t)engine.buildEconomicIcon.texture, ImVec2(40, 40))) { }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Build Economic Building (Q)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("build_mil", (ImTextureID)(intptr_t)engine.buildMilitaryIcon.texture, ImVec2(40, 40))) { }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Build Military Building (W)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("repair", (ImTextureID)(intptr_t)engine.repairIcon.texture, ImVec2(40, 40))) { }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Repair (R)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("garrison", (ImTextureID)(intptr_t)engine.garrisonIcon.texture, ImVec2(40, 40)))
                    {
                        appState.cursorMode = CursorMode::Garrison;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Garrison (T)");
                    ImGui::SameLine();

                    if (ImGui::ImageButton("stop", (ImTextureID)(intptr_t)engine.stopIcon.texture, ImVec2(40, 40)))
                    {
                        for (Villager& v : appState.villagers)
                        {
                            if (v.selected)
                            {
                                v.waypointQueue.clear();
                                v.moving = false;
                                v.targetPosition = v.position;
                            }
                        }
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (G)");
                }
            }
            else if (firstSelectedTC)
            {
                bool canCreateVillager = firstSelectedTC->villagerQueueCount < 15 && static_cast<int>(appState.villagers.size()) < appState.maxPopulation;
                if (canCreateVillager)
                {
                    if (ImGui::Button("Create Villager (Q)", ImVec2(160, 40)) || ImGui::IsKeyPressed(ImGuiKey_Q, false))
                    {
                        firstSelectedTC->villagerQueueCount++;
                    }
                }
                else
                {
                    ImGui::BeginDisabled();
                    ImGui::Button("Create Villager (Q) (Max)", ImVec2(160, 40));
                    ImGui::EndDisabled();
                }

                if (firstSelectedTC->garrisonCount > 0)
                {
                    if (ImGui::Button("Ungarrison All", ImVec2(160, 40)))
                    {
                        int tcIdx = -1;
                        for (size_t t = 0; t < appState.townCenters.size(); ++t) {
                            if (&appState.townCenters[t] == firstSelectedTC) tcIdx = static_cast<int>(t);
                        }
                        
                        for (Villager& v : appState.villagers)
                        {
                            if (v.isGarrisoned && v.garrisonTcIndex == tcIdx)
                            {
                                v.isGarrisoned = false;
                                v.selected = true;
                                firstSelectedTC->garrisonCount--;
                                
                                glm::ivec2 vTile = glm::ivec2(firstSelectedTC->tile.x - 1, firstSelectedTC->tile.y + 5);
                                float bestDist = 1e9f;
                                glm::vec2 targetWorld = firstSelectedTC->hasGatherPoint ? firstSelectedTC->gatherPoint : firstSelectedTC->position;

                                for (int x = -1; x <= 4; ++x) {
                                    for (int y = -1; y <= 4; ++y) {
                                        if (x >= 0 && x <= 3 && y >= 0 && y <= 3) continue;
                                        glm::ivec2 p(firstSelectedTC->tile.x + x, firstSelectedTC->tile.y + y);
                                        if (p.x >= 0 && p.x < GRID_SIZE && p.y >= 0 && p.y < GRID_SIZE && !is_tile_blocked(appState, p)) {
                                            float dist = glm::length(tile_to_world(p) - targetWorld);
                                            if (dist < bestDist) {
                                                bestDist = dist;
                                                vTile = p;
                                            }
                                        }
                                    }
                                }
                                v.position = tile_to_world(vTile);
                                v.targetPosition = v.position;

                                if (firstSelectedTC->hasGatherPoint)
                                {
                                    std::vector<glm::vec2> path = find_path(appState, v.position, firstSelectedTC->gatherPoint);
                                    if (path.size() > 1) {
                                        v.targetPosition = path[1];
                                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                                        v.moving = true;
                                        for (size_t i = 2; i < path.size(); ++i) {
                                            v.waypointQueue.push_back(path[i]);
                                        }
                                    } else if (path.size() == 1) {
                                        v.targetPosition = path[0];
                                        v.facingDirection = glm::normalize(v.targetPosition - v.position);
                                        v.moving = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // --- Column 1: Details ---
            ImGui::TableSetColumnIndex(1);
            if (firstSelectedVillager)
            {
                if (selectedVillagerCount > 1)
                {
                    ImGui::Text("Villagers (%d)", selectedVillagerCount);
                }
                else
                {
                    ImGui::Text("Villager");
                }
                char hpText[32];
                snprintf(hpText, sizeof(hpText), "%d / %d", firstSelectedVillager->hp, firstSelectedVillager->maxHp);
                ImGui::ProgressBar(static_cast<float>(firstSelectedVillager->hp) / static_cast<float>(firstSelectedVillager->maxHp), ImVec2(200.0f, 20.0f), hpText);
                ImGui::Text("Attack: 3");
            }
            else if (firstSelectedTC)
            {
                ImGui::BeginGroup();
                ImGui::Text("Town Center");
                char hpText[32];
                snprintf(hpText, sizeof(hpText), "Health: %d / %d", firstSelectedTC->hp, firstSelectedTC->maxHp);
                ImGui::ProgressBar(static_cast<float>(firstSelectedTC->hp) / static_cast<float>(firstSelectedTC->maxHp), ImVec2(200.0f, 20.0f), hpText);
                ImGui::Text("Capacity: %d/%d", firstSelectedTC->garrisonCount, firstSelectedTC->maxGarrison);
                ImGui::Text("Attack: %d", firstSelectedTC->attack);
                ImGui::Text("Range: %d", firstSelectedTC->range);
                ImGui::EndGroup();

                if (firstSelectedTC->villagerQueueCount > 0)
                {
                    ImGui::SameLine(0.0f, 20.0f);
                    ImGui::BeginGroup();

                    bool isPopFull = static_cast<int>(appState.villagers.size()) >= appState.maxPopulation;
                    if (isPopFull)
                    {
                        float blink = static_cast<float>(sin(ImGui::GetTime() * 6.0f) > 0.0);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, blink, blink, 1.0f));
                        ImGui::Text("Training Villager (%d/15) - PAUSED", firstSelectedTC->villagerQueueCount);
                        ImGui::PopStyleColor();
                    }
                    else
                    {
                        ImGui::Text("Training Villager (%d/15)", firstSelectedTC->villagerQueueCount);
                    }

                    const float progress = firstSelectedTC->villagerTrainingTimer / 14.7f;
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.1fs", 14.7f - firstSelectedTC->villagerTrainingTimer);
                    ImGui::ProgressBar(progress, ImVec2(160.0f, 20.0f), buf);
                    ImGui::EndGroup();
                }
            }
            else if (appState.selectedTreeIndex >= 0)
            {
                ImGui::Text("Pine Tree");
                const PineTree& tree = appState.pineTrees[static_cast<size_t>(appState.selectedTreeIndex)];
                char hpText[32];
                snprintf(hpText, sizeof(hpText), "Wood: %d", tree.hp);
                ImGui::ProgressBar(static_cast<float>(tree.hp) / static_cast<float>(tree.maxHp), ImVec2(200.0f, 20.0f), hpText);
            }

            // --- Column 2: Minimap ---
            ImGui::TableSetColumnIndex(2);
            ImVec2 curPos = ImGui::GetCursorScreenPos();
            float mmW = 260.0f;
            float mmH = 130.0f;
            
            // Center within the 320.0f column width constraint
            curPos.x += (320.0f - mmW) / 2.0f;
            curPos.y += 10.0f;
            
            ImGui::InvisibleButton("minimap_area", ImVec2(mmW, mmH));
            
            ImVec2 p1(curPos.x + mmW / 2.0f, curPos.y);                  // Top
            ImVec2 p2(curPos.x + mmW, curPos.y + mmH / 2.0f);            // Right
            ImVec2 p3(curPos.x + mmW / 2.0f, curPos.y + mmH);            // Bottom
            ImVec2 p4(curPos.x, curPos.y + mmH / 2.0f);                  // Left

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            // Solid black background diamond underneath so transparent pixels are consistently black
            drawList->AddQuadFilled(p1, p2, p3, p4, IM_COL32(0, 0, 0, 255));
            
            // Render 200x200 OpenGL texture as an isometric diamond mapped quad
            drawList->AddImageQuad((ImTextureID)(intptr_t)appState.minimapTexture, 
                                   p1, p2, p3, p4,
                                   ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f), 
                                   ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f), IM_COL32_WHITE);
            
            // Render simple border over the diamond bounds
            drawList->AddQuad(p1, p2, p3, p4, IM_COL32(150, 150, 150, 255), 2.0f);
            
            // Handle Map Clicking
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                ImVec2 mousePos = ImGui::GetMousePos();
                float dx = mousePos.x - (curPos.x + mmW / 2.0f);
                float dy = mousePos.y - (curPos.y + mmH / 2.0f);
                
                float u = dx / mmW + dy / mmH;
                float v = dy / mmH - dx / mmW;
                float true_u = u + 0.5f;
                float true_v = v + 0.5f;

                if (true_u >= 0.0f && true_u <= 1.0f && true_v >= 0.0f && true_v <= 1.0f)
                {
                    int tileX = static_cast<int>(true_u * GRID_SIZE);
                    int tileY = static_cast<int>(true_v * GRID_SIZE);
                    glm::ivec2 targetTile(tileX, tileY);
                    glm::vec2 worldPos = tile_to_world(targetTile);
                    cameraX = worldPos.x;
                    cameraY = worldPos.y;
                }
            }
            
            // Draw Camera Viewport indicator on Minimap
            const glm::vec2 camWorld(cameraX, cameraY);
            const std::optional<glm::ivec2> camTile = world_to_tile(camWorld);
            if (camTile.has_value())
            {
                float true_u = static_cast<float>(camTile->x) / static_cast<float>(GRID_SIZE);
                float true_v = static_cast<float>(camTile->y) / static_cast<float>(GRID_SIZE);
                float u = true_u - 0.5f;
                float v = true_v - 0.5f;
                
                float cx = (u - v) * (mmW / 2.0f) + curPos.x + mmW / 2.0f;
                float cy = (u + v) * (mmH / 2.0f) + curPos.y + mmH / 2.0f;
                
                drawList->AddQuad(
                    ImVec2(cx, cy - 8), ImVec2(cx + 16, cy),
                    ImVec2(cx, cy + 8), ImVec2(cx - 16, cy),
                    IM_COL32(255, 255, 255, 255), 1.5f);
            }

            ImGui::EndTable();
        }
        ImGui::End();

}
