#pragma once

#include <glm/glm.hpp>
#include <optional>

// Tile grid conversion
glm::vec2 tile_to_world(const glm::ivec2& tile);
std::optional<glm::ivec2> world_to_tile(const glm::vec2& worldPosition);

// Screen coordinate conversion
glm::vec2 screen_to_world(const glm::dvec2& screenPosition);
glm::vec2 world_to_screen(const glm::vec2& worldPosition);

