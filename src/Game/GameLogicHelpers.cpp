#include "GameLogicHelpers.h"
#include "../Core/Constants.h"
#include "../Math/CoordinateSystem.h"
#include <algorithm>

bool point_in_drag_rect(const glm::vec2& point, const SelectionState& selection)
{
    const double minX = std::min(selection.startScreen.x, selection.currentScreen.x);
    const double maxX = std::max(selection.startScreen.x, selection.currentScreen.x);
    const double minY = std::min(selection.startScreen.y, selection.currentScreen.y);
    const double maxY = std::max(selection.startScreen.y, selection.currentScreen.y);
    return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY;
}

// Villager hitbox ellipse test in world space.
// Ellipse center: (villagerWorldPos.x, villagerWorldPos.y - TILE_HALF_HEIGHT)
// Ellipse radii: rx=22, ry=36 (matching the visual hitbox)
bool villager_hit_test_screen(const glm::vec2& villagerWorldPos, const glm::dvec2& cursorScreen)
{
    constexpr float HITBOX_RX = 22.0f;
    constexpr float HITBOX_RY = 36.0f;
    const glm::vec2 cursorWorld = screen_to_world(cursorScreen);
    const glm::vec2 ellipseCenter = villagerWorldPos + glm::vec2(5.0f, 6.0f);
    const glm::vec2 delta = cursorWorld - ellipseCenter;
    return (delta.x * delta.x) / (HITBOX_RX * HITBOX_RX) + (delta.y * delta.y) / (HITBOX_RY * HITBOX_RY) <= 1.0f;
}

bool tree_hit_test_screen(const PineTree& tree, const glm::dvec2& cursorScreen, const glm::vec2& spriteSize)
{
    const glm::vec2 spriteOrigin = tree.position + PINE_RENDER_OFFSET;
    const glm::vec2 screenBottomLeft = world_to_screen(spriteOrigin + glm::vec2(-0.5f * spriteSize.x, 0.0f));
    const glm::vec2 screenTopRight = world_to_screen(spriteOrigin + glm::vec2(0.5f * spriteSize.x, spriteSize.y));
    const float minX = std::min(screenBottomLeft.x, screenTopRight.x);
    const float maxX = std::max(screenBottomLeft.x, screenTopRight.x);
    const float minY = std::min(screenBottomLeft.y, screenTopRight.y);
    const float maxY = std::max(screenBottomLeft.y, screenTopRight.y);
    return static_cast<float>(cursorScreen.x) >= minX && static_cast<float>(cursorScreen.x) <= maxX &&
        static_cast<float>(cursorScreen.y) >= minY && static_cast<float>(cursorScreen.y) <= maxY;
}

void clear_selection(AppState& appState)
{
    appState.selectedTreeId = 0;
    for (auto& [uuid, v] : appState.villagers)
    {
        v.selected = false;
    }
    for (auto& [uuid, tree] : appState.pineTrees)
    {
        tree.selected = false;
    }
    for (auto& [uuid, tc] : appState.townCenters)
    {
        tc.selected = false;
    }
    for (auto& [uuid, house] : appState.houses)
    {
        house.selected = false;
    }
    for (auto& [uuid, mill] : appState.mills)
    {
        mill.selected = false;
    }
    for (auto& [uuid, miningCamp] : appState.miningCamps)
    {
        miningCamp.selected = false;
    }
    for (auto& [uuid, lumberCamp] : appState.lumberCamps)
    {
        lumberCamp.selected = false;
    }
    appState.selectedBuilding = BuildableBuilding::None;
    appState.cursorMode = CursorMode::Normal;
}

bool point_in_polygon(const glm::vec2& p, const std::vector<glm::vec2>& polygon)
{
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        if (((polygon[i].y > p.y) != (polygon[j].y > p.y)) &&
            (p.x < (polygon[j].x - polygon[i].x) * (p.y - polygon[i].y) / (polygon[j].y - polygon[i].y) + polygon[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

std::vector<glm::vec2> get_town_center_polygon(const TownCenter& tc, const glm::vec2& spriteSize)
{
    const glm::vec2 renderPos = tc.position + TOWN_CENTER_RENDER_OFFSET;
    const float hw = spriteSize.x * 0.5f;
    const float h = spriteSize.y;
    const float baseH = hw; // typical ratio for AoE2 2:1 projection
    
    return {
        renderPos + glm::vec2(0.0f, 0.0f),            // Bottom tip
        renderPos + glm::vec2(hw, baseH * 0.5f),      // Right tip
        renderPos + glm::vec2(hw, h - baseH * 0.5f),  // Top right
        renderPos + glm::vec2(0.0f, h),               // Top tip
        renderPos + glm::vec2(-hw, h - baseH * 0.5f), // Top left
        renderPos + glm::vec2(-hw, baseH * 0.5f)      // Left tip
    };
}

bool town_center_hit_test_screen(const TownCenter& tc, const glm::dvec2& cursorScreen, const glm::vec2& spriteSize)
{
    std::vector<glm::vec2> worldPoly = get_town_center_polygon(tc, spriteSize);
    std::vector<glm::vec2> screenPoly;
    screenPoly.reserve(worldPoly.size());
    for (const auto& wp : worldPoly) {
        screenPoly.push_back(world_to_screen(wp));
    }
    return point_in_polygon(glm::vec2(static_cast<float>(cursorScreen.x), static_cast<float>(cursorScreen.y)), screenPoly);
}

bool house_hit_test_screen(const House& house, const glm::dvec2& cursorScreen, const glm::vec2& spriteSize)
{
    const glm::vec2 houseOrigin = house.position + HOUSE_RENDER_OFFSET;
    const glm::vec2 screenBottomLeft = world_to_screen(houseOrigin + glm::vec2(-0.5f * spriteSize.x, 0.0f));
    const glm::vec2 screenTopRight = world_to_screen(houseOrigin + glm::vec2(0.5f * spriteSize.x, spriteSize.y));
    const float minX = std::min(screenBottomLeft.x, screenTopRight.x);
    const float maxX = std::max(screenBottomLeft.x, screenTopRight.x);
    const float minY = std::min(screenBottomLeft.y, screenTopRight.y);
    const float maxY = std::max(screenBottomLeft.y, screenTopRight.y);
    return static_cast<float>(cursorScreen.x) >= minX && static_cast<float>(cursorScreen.x) <= maxX &&
        static_cast<float>(cursorScreen.y) >= minY && static_cast<float>(cursorScreen.y) <= maxY;
}

bool mill_hit_test_screen(const Mill& mill, const glm::dvec2& cursorScreen, const glm::vec2& spriteSize)
{
    const glm::vec2 millOrigin = mill.position + MILL_RENDER_OFFSET;
    const glm::vec2 screenBottomLeft = world_to_screen(millOrigin + glm::vec2(-0.5f * spriteSize.x, 0.0f));
    const glm::vec2 screenTopRight = world_to_screen(millOrigin + glm::vec2(0.5f * spriteSize.x, spriteSize.y));
    const float minX = std::min(screenBottomLeft.x, screenTopRight.x);
    const float maxX = std::max(screenBottomLeft.x, screenTopRight.x);
    const float minY = std::min(screenBottomLeft.y, screenTopRight.y);
    const float maxY = std::max(screenBottomLeft.y, screenTopRight.y);
    return static_cast<float>(cursorScreen.x) >= minX && static_cast<float>(cursorScreen.x) <= maxX &&
        static_cast<float>(cursorScreen.y) >= minY && static_cast<float>(cursorScreen.y) <= maxY;
}

bool mining_camp_hit_test_screen(const MiningCamp& miningCamp, const glm::dvec2& cursorScreen, const glm::vec2& spriteSize)
{
    const glm::vec2 campOrigin = miningCamp.position + MINING_CAMP_RENDER_OFFSET;
    const glm::vec2 screenBottomLeft = world_to_screen(campOrigin + glm::vec2(-0.5f * spriteSize.x, 0.0f));
    const glm::vec2 screenTopRight = world_to_screen(campOrigin + glm::vec2(0.5f * spriteSize.x, spriteSize.y));
    const float minX = std::min(screenBottomLeft.x, screenTopRight.x);
    const float maxX = std::max(screenBottomLeft.x, screenTopRight.x);
    const float minY = std::min(screenBottomLeft.y, screenTopRight.y);
    const float maxY = std::max(screenBottomLeft.y, screenTopRight.y);
    return static_cast<float>(cursorScreen.x) >= minX && static_cast<float>(cursorScreen.x) <= maxX &&
        static_cast<float>(cursorScreen.y) >= minY && static_cast<float>(cursorScreen.y) <= maxY;
}

bool lumber_camp_hit_test_screen(const LumberCamp& lumberCamp, const glm::dvec2& cursorScreen, const glm::vec2& spriteSize)
{
    const glm::vec2 campOrigin = lumberCamp.position + LUMBER_CAMP_RENDER_OFFSET;
    const glm::vec2 screenBottomLeft = world_to_screen(campOrigin + glm::vec2(-0.5f * spriteSize.x, 0.0f));
    const glm::vec2 screenTopRight = world_to_screen(campOrigin + glm::vec2(0.5f * spriteSize.x, spriteSize.y));
    const float minX = std::min(screenBottomLeft.x, screenTopRight.x);
    const float maxX = std::max(screenBottomLeft.x, screenTopRight.x);
    const float minY = std::min(screenBottomLeft.y, screenTopRight.y);
    const float maxY = std::max(screenBottomLeft.y, screenTopRight.y);
    return static_cast<float>(cursorScreen.x) >= minX && static_cast<float>(cursorScreen.x) <= maxX &&
        static_cast<float>(cursorScreen.y) >= minY && static_cast<float>(cursorScreen.y) <= maxY;
}

bool is_tile_blocked(const AppState& appState, const glm::ivec2& tile)
{
    for (const auto& [uuid, tree] : appState.pineTrees)
    {
        if (tree.tile == tile) return true;
    }
    for (const auto& [uuid, tc] : appState.townCenters)
    {
        if (tile.x >= tc.tile.x && tile.x < tc.tile.x + 4 &&
            tile.y >= tc.tile.y && tile.y < tc.tile.y + 4)
        {
            return true;
        }
    }
    for (const auto& [uuid, house] : appState.houses)
    {
        if (house.isGhostFoundation) continue; // Ghost foundations don't block
        if (tile.x >= house.tile.x && tile.x < house.tile.x + 2 &&
            tile.y >= house.tile.y && tile.y < house.tile.y + 2)
        {
            return true;
        }
    }
    for (const auto& [uuid, mill] : appState.mills)
    {
        if (mill.isGhostFoundation) continue;
        if (tile.x >= mill.tile.x && tile.x < mill.tile.x + 2 &&
            tile.y >= mill.tile.y && tile.y < mill.tile.y + 2)
        {
            return true;
        }
    }
    for (const auto& [uuid, miningCamp] : appState.miningCamps)
    {
        if (miningCamp.isGhostFoundation) continue;
        if (tile.x >= miningCamp.tile.x && tile.x < miningCamp.tile.x + 2 &&
            tile.y >= miningCamp.tile.y && tile.y < miningCamp.tile.y + 2)
        {
            return true;
        }
    }
    for (const auto& [uuid, lumberCamp] : appState.lumberCamps)
    {
        if (lumberCamp.isGhostFoundation) continue;
        if (tile.x >= lumberCamp.tile.x && tile.x < lumberCamp.tile.x + 2 &&
            tile.y >= lumberCamp.tile.y && tile.y < lumberCamp.tile.y + 2)
        {
            return true;
        }
    }
    return false;
}

std::vector<glm::vec2> blocked_tile_translations(const AppState& appState)
{
    std::vector<glm::vec2> blockedTiles;
    for (const auto& [uuid, tree] : appState.pineTrees)
    {
        blockedTiles.push_back(tile_to_world(tree.tile));
    }
    for (const auto& [uuid, tc] : appState.townCenters)
    {
        for (int dx = 0; dx < 4; dx++)
        {
            for (int dy = 0; dy < 4; dy++)
            {
                blockedTiles.push_back(tile_to_world(tc.tile + glm::ivec2(dx, dy)));
            }
        }
    }
    for (const auto& [uuid, house] : appState.houses)
    {
        if (house.isGhostFoundation) continue; // Ghost foundations don't block tiles
        for (int dx = 0; dx < 2; dx++)
        {
            for (int dy = 0; dy < 2; dy++)
            {
                blockedTiles.push_back(tile_to_world(house.tile + glm::ivec2(dx, dy)));
            }
        }
    }
    for (const auto& [uuid, mill] : appState.mills)
    {
        if (mill.isGhostFoundation) continue;
        for (int dx = 0; dx < 2; dx++)
        {
            for (int dy = 0; dy < 2; dy++)
            {
                blockedTiles.push_back(tile_to_world(mill.tile + glm::ivec2(dx, dy)));
            }
        }
    }
    for (const auto& [uuid, miningCamp] : appState.miningCamps)
    {
        if (miningCamp.isGhostFoundation) continue;
        for (int dx = 0; dx < 2; dx++)
        {
            for (int dy = 0; dy < 2; dy++)
            {
                blockedTiles.push_back(tile_to_world(miningCamp.tile + glm::ivec2(dx, dy)));
            }
        }
    }
    for (const auto& [uuid, lumberCamp] : appState.lumberCamps)
    {
        if (lumberCamp.isGhostFoundation) continue;
        for (int dx = 0; dx < 2; dx++)
        {
            for (int dy = 0; dy < 2; dy++)
            {
                blockedTiles.push_back(tile_to_world(lumberCamp.tile + glm::ivec2(dx, dy)));
            }
        }
    }
    return blockedTiles;
}

bool can_place_house(const AppState& appState, const glm::ivec2& tile)
{
    if (tile.x < 0 || tile.x >= GRID_SIZE - 1 || tile.y < 0 || tile.y >= GRID_SIZE - 1)
    {
        return false;
    }
    return !is_tile_blocked(appState, tile) &&
           !is_tile_blocked(appState, tile + glm::ivec2(1, 0)) &&
           !is_tile_blocked(appState, tile + glm::ivec2(0, 1)) &&
           !is_tile_blocked(appState, tile + glm::ivec2(1, 1));
}

bool can_place_mill(const AppState& appState, const glm::ivec2& tile)
{
    if (tile.x < 0 || tile.x >= GRID_SIZE - 1 || tile.y < 0 || tile.y >= GRID_SIZE - 1)
    {
        return false;
    }
    return !is_tile_blocked(appState, tile) &&
           !is_tile_blocked(appState, tile + glm::ivec2(1, 0)) &&
           !is_tile_blocked(appState, tile + glm::ivec2(0, 1)) &&
           !is_tile_blocked(appState, tile + glm::ivec2(1, 1));
}

bool can_place_mining_camp(const AppState& appState, const glm::ivec2& tile)
{
    if (tile.x < 0 || tile.x >= GRID_SIZE - 1 || tile.y < 0 || tile.y >= GRID_SIZE - 1)
    {
        return false;
    }
    return !is_tile_blocked(appState, tile) &&
           !is_tile_blocked(appState, tile + glm::ivec2(1, 0)) &&
           !is_tile_blocked(appState, tile + glm::ivec2(0, 1)) &&
           !is_tile_blocked(appState, tile + glm::ivec2(1, 1));
}

bool can_place_lumber_camp(const AppState& appState, const glm::ivec2& tile)
{
    if (tile.x < 0 || tile.x >= GRID_SIZE - 1 || tile.y < 0 || tile.y >= GRID_SIZE - 1)
    {
        return false;
    }
    return !is_tile_blocked(appState, tile) &&
           !is_tile_blocked(appState, tile + glm::ivec2(1, 0)) &&
           !is_tile_blocked(appState, tile + glm::ivec2(0, 1)) &&
           !is_tile_blocked(appState, tile + glm::ivec2(1, 1));
}
