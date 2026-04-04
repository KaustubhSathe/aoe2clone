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

bool villager_hit_test_screen(const glm::vec2& villagerScreenPosition, const glm::dvec2& cursorScreen)
{
    const glm::vec2 delta = glm::vec2(static_cast<float>(cursorScreen.x), static_cast<float>(cursorScreen.y)) - villagerScreenPosition;
    return glm::length(delta) <= CLICK_SELECT_RADIUS;
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
    appState.selectedTreeIndex = -1;
    for (Villager& v : appState.villagers)
    {
        v.selected = false;
    }
    for (PineTree& tree : appState.pineTrees)
    {
        tree.selected = false;
    }
    for (TownCenter& tc : appState.townCenters)
    {
        tc.selected = false;
    }
    for (House& house : appState.houses)
    {
        house.selected = false;
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

bool is_tile_blocked(const AppState& appState, const glm::ivec2& tile)
{
    for (const PineTree& tree : appState.pineTrees)
    {
        if (tree.tile == tile) return true;
    }
    for (const TownCenter& tc : appState.townCenters)
    {
        if (tile.x >= tc.tile.x && tile.x < tc.tile.x + 4 &&
            tile.y >= tc.tile.y && tile.y < tc.tile.y + 4)
        {
            return true;
        }
    }
    for (const House& house : appState.houses)
    {
        if (house.isGhostFoundation) continue; // Ghost foundations don't block
        if (tile.x >= house.tile.x && tile.x < house.tile.x + 2 &&
            tile.y >= house.tile.y && tile.y < house.tile.y + 2)
        {
            return true;
        }
    }
    return false;
}

std::vector<glm::vec2> blocked_tile_translations(const AppState& appState)
{
    std::vector<glm::vec2> blockedTiles;
    for (const PineTree& tree : appState.pineTrees)
    {
        blockedTiles.push_back(tile_to_world(tree.tile));
    }
    for (const TownCenter& tc : appState.townCenters)
    {
        for (int dx = 0; dx < 4; dx++)
        {
            for (int dy = 0; dy < 4; dy++)
            {
                blockedTiles.push_back(tile_to_world(tc.tile + glm::ivec2(dx, dy)));
            }
        }
    }
    for (const House& house : appState.houses)
    {
        for (int dx = 0; dx < 2; dx++)
        {
            for (int dy = 0; dy < 2; dy++)
            {
                blockedTiles.push_back(tile_to_world(house.tile + glm::ivec2(dx, dy)));
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
