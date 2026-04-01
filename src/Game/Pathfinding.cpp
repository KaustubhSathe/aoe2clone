#include "Pathfinding.h"
#include "GameLogicHelpers.h"
#include "../Math/CoordinateSystem.h"
#include "../Core/Constants.h"
#include <queue>
#include <algorithm>

std::vector<glm::vec2> find_path(const AppState& appState, const glm::vec2& startWorld, const glm::vec2& targetWorld)
{
    const std::optional<glm::ivec2> startOpt = world_to_tile(startWorld);
    const std::optional<glm::ivec2> targetOpt = world_to_tile(targetWorld);

    if (!startOpt.has_value() || !targetOpt.has_value())
    {
        return {};
    }

    const glm::ivec2 start = *startOpt;
    const glm::ivec2 target = *targetOpt;

    if (start == target)
    {
        return { targetWorld };
    }

    auto heuristic = [](const glm::ivec2& a, const glm::ivec2& b) -> float {
        int dx = std::abs(a.x - b.x);
        int dy = std::abs(a.y - b.y);
        return static_cast<float>(std::max(dx, dy)) + (1.414f - 1.0f) * std::min(dx, dy);
    };

    std::vector<float> gScore(GRID_SIZE * GRID_SIZE, 1e9f);
    std::vector<int> cameFrom(GRID_SIZE * GRID_SIZE, -1);
    std::vector<bool> closedSet(GRID_SIZE * GRID_SIZE, false);

    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;

    const int startIndex = start.y * GRID_SIZE + start.x;
    gScore[startIndex] = 0.0f;
    openSet.push({ start, heuristic(start, target) });

    const glm::ivec2 neighbors[8] = {
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}
    };
    const float moveCosts[8] = {
        1.0f, 1.414f, 1.0f, 1.414f,
        1.0f, 1.414f, 1.0f, 1.414f
    };

    bool found = false;

    while (!openSet.empty())
    {
        const glm::ivec2 current = openSet.top().tile;
        openSet.pop();

        if (current == target)
        {
            found = true;
            break;
        }

        const int currentIndex = current.y * GRID_SIZE + current.x;
        if (closedSet[currentIndex])
        {
            continue;
        }
        closedSet[currentIndex] = true;

        for (int i = 0; i < 8; ++i)
        {
            const glm::ivec2 neighbor = current + neighbors[i];
            if (neighbor.x < 0 || neighbor.x >= GRID_SIZE || neighbor.y < 0 || neighbor.y >= GRID_SIZE)
            {
                continue;
            }

            if (i % 2 != 0) 
            {
                glm::ivec2 adj1 = current + glm::ivec2(neighbors[i].x, 0);
                glm::ivec2 adj2 = current + glm::ivec2(0, neighbors[i].y);
                if ((adj1.x >= 0 && adj1.x < GRID_SIZE && adj1.y >= 0 && adj1.y < GRID_SIZE && is_tile_blocked(appState, adj1)) ||
                    (adj2.x >= 0 && adj2.x < GRID_SIZE && adj2.y >= 0 && adj2.y < GRID_SIZE && is_tile_blocked(appState, adj2)))
                {
                    continue; 
                }
            }

            if (is_tile_blocked(appState, neighbor) && neighbor != target)
            {
                continue;
            }

            const int neighborIndex = neighbor.y * GRID_SIZE + neighbor.x;
            if (closedSet[neighborIndex])
            {
                continue;
            }

            const float tentativeGScore = gScore[currentIndex] + moveCosts[i];
            if (tentativeGScore < gScore[neighborIndex])
            {
                cameFrom[neighborIndex] = currentIndex;
                gScore[neighborIndex] = tentativeGScore;
                openSet.push({ neighbor, tentativeGScore + heuristic(neighbor, target) });
            }
        }
    }

    if (!found)
    {
        return {};
    }

    std::vector<glm::vec2> path;
    int currPathIndex = target.y * GRID_SIZE + target.x;
    while (currPathIndex != startIndex)
    {
        const glm::ivec2 tile(currPathIndex % GRID_SIZE, currPathIndex / GRID_SIZE);
        path.push_back(tile_to_world(tile));
        currPathIndex = cameFrom[currPathIndex];
    }
    
    if (!path.empty())
    {
        path.front() = targetWorld; 
    }

    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<glm::ivec2> find_group_destinations(const AppState& appState, const glm::ivec2& centerTile, int numUnits)
{
    std::vector<glm::ivec2> destinations;
    if (numUnits <= 0) return destinations;
    
    std::vector<bool> visited(GRID_SIZE * GRID_SIZE, false);
    std::queue<glm::ivec2> q;
    
    q.push(centerTile);
    visited[centerTile.y * GRID_SIZE + centerTile.x] = true;
    
    const glm::ivec2 neighbors[8] = {
        {0, -1}, {1, 0}, {0, 1}, {-1, 0},
        {1, -1}, {1, 1}, {-1, 1}, {-1, -1}
    };
    
    while (!q.empty() && destinations.size() < static_cast<size_t>(numUnits))
    {
        glm::ivec2 curr = q.front();
        q.pop();
        
        if (!is_tile_blocked(appState, curr))
        {
            destinations.push_back(curr);
        }
        
        for (int i = 0; i < 8; ++i)
        {
            glm::ivec2 next = curr + neighbors[i];
            if (next.x >= 0 && next.x < GRID_SIZE && next.y >= 0 && next.y < GRID_SIZE)
            {
                int index = next.y * GRID_SIZE + next.x;
                if (!visited[index])
                {
                    visited[index] = true;
                    q.push(next);
                }
            }
        }
    }
    
    return destinations;
}
