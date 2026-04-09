#pragma once

#include "../Core/Types.h"
#include <vector>
#include <glm/glm.hpp>

struct AStarNode
{
    glm::ivec2 tile;
    float fScore;

    bool operator>(const AStarNode& other) const
    {
        return fScore > other.fScore;
    }
};

std::vector<glm::vec2> find_path(AppState& appState, const glm::vec2& startWorld, const glm::vec2& targetWorld);
std::vector<glm::ivec2> find_group_destinations(const AppState& appState, const glm::ivec2& centerTile, int numUnits);
void invalidate_pathfinding_cache(AppState& appState);
