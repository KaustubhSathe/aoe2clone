#pragma once

#include "../Core/Types.h"
#include <vector>
#include <glm/glm.hpp>

bool point_in_drag_rect(const glm::vec2& point, const SelectionState& selection);
bool villager_hit_test_screen(const glm::vec2& villagerScreenPosition, const glm::dvec2& cursorScreen);
bool tree_hit_test_screen(const PineTree& tree, const glm::dvec2& cursorScreen, const glm::vec2& spriteSize);
void clear_selection(AppState& appState);
bool town_center_hit_test_screen(const TownCenter& tc, const glm::dvec2& cursorScreen, const glm::vec2& spriteSize);
bool is_tile_blocked(const AppState& appState, const glm::ivec2& tile);
std::vector<glm::vec2> blocked_tile_translations(const AppState& appState);
