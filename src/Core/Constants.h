#pragma once

#include <glm/glm.hpp>

constexpr int GRID_SIZE = 200;
constexpr float TILE_HALF_WIDTH = 48.0f;
constexpr float TILE_HALF_HEIGHT = 24.0f;
constexpr float TILE_WORLD_STEP = 53.66563f;
constexpr int WALK_DIRECTION_COUNT = 16;
constexpr int WALK_FRAMES_PER_DIRECTION = 30;
constexpr float WALK_ANIMATION_FPS = 30.0f;
constexpr float BUILD_ANIMATION_FPS = 30.0f;
constexpr float CLICK_SELECT_RADIUS = 26.0f;
constexpr float DRAG_THRESHOLD = 4.0f;
constexpr float MINIMAP_SIZE = 160.0f;
constexpr glm::vec2 PINE_RENDER_OFFSET = glm::vec2(-20.0f, -10.0f);
constexpr glm::vec2 TOWN_CENTER_RENDER_OFFSET = glm::vec2(0.0f, -120.0f);
constexpr glm::vec2 HOUSE_RENDER_OFFSET = glm::vec2(0.0f, -60.0f);
constexpr glm::vec2 MILL_RENDER_OFFSET = glm::vec2(0.0f, -60.0f);
constexpr glm::vec2 MINING_CAMP_RENDER_OFFSET = glm::vec2(0.0f, -60.0f);
constexpr glm::vec2 LUMBER_CAMP_RENDER_OFFSET = glm::vec2(0.0f, -60.0f);

constexpr int HOUSE_COST_WOOD = 25;
constexpr float HOUSE_BUILD_TIME = 25.0f;

constexpr int MILL_COST_WOOD = 100;
constexpr float MILL_BUILD_TIME = 35.0f;

constexpr int MINING_CAMP_COST_WOOD = 100;
constexpr float MINING_CAMP_BUILD_TIME = 35.0f;

constexpr int LUMBER_CAMP_COST_WOOD = 100;
constexpr float LUMBER_CAMP_BUILD_TIME = 35.0f;

// Tweakable offset for building placement highlight (adjust if highlight doesn't align with cursor)
constexpr glm::vec2 HIGHLIGHT_OFFSET = glm::vec2(0.0f, -20.0f);

// Line of Sight radii for fog of war (in tiles)
constexpr float VILLAGER_LOS_RADIUS = 4.0f;
constexpr float TOWN_CENTER_LOS_RADIUS = 8.0f;
