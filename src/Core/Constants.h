#pragma once

#include <glm/glm.hpp>

constexpr int GRID_SIZE = 200;
constexpr float TILE_HALF_WIDTH = 48.0f;
constexpr float TILE_HALF_HEIGHT = 24.0f;
constexpr float TILE_WORLD_STEP = 53.66563f;
constexpr int WALK_DIRECTION_COUNT = 16;
constexpr int WALK_FRAMES_PER_DIRECTION = 30;
constexpr float WALK_ANIMATION_FPS = 15.0f;
constexpr float CLICK_SELECT_RADIUS = 26.0f;
constexpr float DRAG_THRESHOLD = 4.0f;
constexpr float MINIMAP_SIZE = 160.0f;
constexpr glm::vec2 PINE_RENDER_OFFSET = glm::vec2(-20.0f, -10.0f);
constexpr glm::vec2 TOWN_CENTER_RENDER_OFFSET = glm::vec2(0.0f, -65.0f);
