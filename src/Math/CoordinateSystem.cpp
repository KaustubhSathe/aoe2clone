#include "CoordinateSystem.h"
#include "../Core/Globals.h"
#include "../Core/Constants.h"
#include <cmath>

glm::vec2 tile_to_world(const glm::ivec2& tile)
{
    return glm::vec2(
        static_cast<float>(tile.x - tile.y) * TILE_HALF_WIDTH,
        -static_cast<float>(tile.x + tile.y) * TILE_HALF_HEIGHT);
}

std::optional<glm::ivec2> world_to_tile(const glm::vec2& worldPosition)
{
    const float a = worldPosition.x / TILE_HALF_WIDTH;
    const float b = -worldPosition.y / TILE_HALF_HEIGHT;
    const int tileX = static_cast<int>(std::round((a + b) * 0.5f));
    const int tileY = static_cast<int>(std::round((b - a) * 0.5f));
    if (tileX < 0 || tileX >= GRID_SIZE || tileY < 0 || tileY >= GRID_SIZE)
    {
        return std::nullopt;
    }

    return glm::ivec2(tileX, tileY);
}

glm::vec2 screen_to_world(const glm::dvec2& screenPosition)
{
    const float worldX = cameraX + (static_cast<float>(screenPosition.x) - static_cast<float>(SCR_WIDTH) * 0.5f) / zoom;
    const float worldY = cameraY + (static_cast<float>(SCR_HEIGHT) * 0.5f - static_cast<float>(screenPosition.y)) / zoom;
    return glm::vec2(worldX, worldY);
}

glm::vec2 world_to_screen(const glm::vec2& worldPosition)
{
    const float screenX = (worldPosition.x - cameraX) * zoom + static_cast<float>(SCR_WIDTH) * 0.5f;
    const float screenY = static_cast<float>(SCR_HEIGHT) * 0.5f - (worldPosition.y - cameraY) * zoom;
    return glm::vec2(screenX, screenY);
}
