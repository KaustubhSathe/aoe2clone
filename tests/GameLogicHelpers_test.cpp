#include <gtest/gtest.h>
#include "src/Game/GameLogicHelpers.h"
#include "src/Math/CoordinateSystem.h"
#include "src/Core/Types.h"
#include "src/Core/Constants.h"

#include <glm/glm.hpp>

namespace {

TEST(GameLogicHelpersTest, PointInPolygonSquare)
{
    std::vector<glm::vec2> square = {
        {0.0f, 0.0f},
        {10.0f, 0.0f},
        {10.0f, 10.0f},
        {0.0f, 10.0f}
    };

    EXPECT_TRUE(point_in_polygon({5.0f, 5.0f}, square));
    EXPECT_TRUE(point_in_polygon({0.5f, 0.5f}, square));
    EXPECT_FALSE(point_in_polygon({15.0f, 5.0f}, square));
    EXPECT_FALSE(point_in_polygon({-1.0f, 5.0f}, square));
}

TEST(GameLogicHelpersTest, PointInPolygonTriangle)
{
    std::vector<glm::vec2> triangle = {
        {0.0f, 0.0f},
        {10.0f, 0.0f},
        {5.0f, 10.0f}
    };

    EXPECT_TRUE(point_in_polygon({5.0f, 1.0f}, triangle));
    EXPECT_FALSE(point_in_polygon({5.0f, 11.0f}, triangle));
    EXPECT_FALSE(point_in_polygon({0.0f, 5.0f}, triangle));
}

TEST(GameLogicHelpersTest, PointInPolygonEmpty)
{
    std::vector<glm::vec2> empty;
    EXPECT_FALSE(point_in_polygon({5.0f, 5.0f}, empty));
}

TEST(GameLogicHelpersTest, VillagerHitTestScreenInsideRadius)
{
    glm::vec2 villagerScreen(100.0f, 100.0f);
    glm::dvec2 cursor(100.0f, 100.0f);
    EXPECT_TRUE(villager_hit_test_screen(villagerScreen, cursor));
}

TEST(GameLogicHelpersTest, VillagerHitTestScreenOutsideRadius)
{
    glm::vec2 villagerScreen(100.0f, 100.0f);
    glm::dvec2 cursor(200.0f, 100.0f);
    EXPECT_FALSE(villager_hit_test_screen(villagerScreen, cursor));
}

TEST(GameLogicHelpersTest, VillagerHitTestScreenOnBoundary)
{
    glm::vec2 villagerScreen(0.0f, 0.0f);
    glm::dvec2 cursor(CLICK_SELECT_RADIUS, 0.0f);
    EXPECT_TRUE(villager_hit_test_screen(villagerScreen, cursor));
}

TEST(GameLogicHelpersTest, GetTownCenterPolygonReturnsSixVertices)
{
    TownCenter tc;
    tc.tile = glm::ivec2(0, 0);
    tc.position = tile_to_world(tc.tile);

    std::vector<glm::vec2> polygon = get_town_center_polygon(tc, glm::vec2(256.0f, 256.0f));
    ASSERT_EQ(polygon.size(), 6u);
}

TEST(GameLogicHelpersTest, GetTownCenterPolygonVerticesAreContiguous)
{
    TownCenter tc;
    tc.tile = glm::ivec2(0, 0);
    tc.position = tile_to_world(tc.tile);

    std::vector<glm::vec2> polygon = get_town_center_polygon(tc, glm::vec2(256.0f, 256.0f));

    // All vertices should share the same base position offset
    // The polygon bottom tip should be at render offset
    glm::vec2 expectedBottom = tc.position + TOWN_CENTER_RENDER_OFFSET;
    EXPECT_EQ(polygon[0], expectedBottom);
}

TEST(GameLogicHelpersTest, IsTileBlockedByPineTree)
{
    AppState appState;
    PineTree tree;
    tree.uuid = 1;
    tree.tile = glm::ivec2(5, 5);
    appState.pineTrees[tree.uuid] = tree;

    EXPECT_TRUE(is_tile_blocked(appState, glm::ivec2(5, 5)));
    EXPECT_FALSE(is_tile_blocked(appState, glm::ivec2(5, 6)));
    EXPECT_FALSE(is_tile_blocked(appState, glm::ivec2(4, 5)));
}

TEST(GameLogicHelpersTest, IsTileBlockedByTownCenter)
{
    AppState appState;
    TownCenter tc;
    tc.uuid = 1;
    tc.tile = glm::ivec2(10, 10);
    appState.townCenters[tc.uuid] = tc;

    // TC occupies 4x4 tiles
    EXPECT_TRUE(is_tile_blocked(appState, glm::ivec2(10, 10)));
    EXPECT_TRUE(is_tile_blocked(appState, glm::ivec2(13, 10)));
    EXPECT_TRUE(is_tile_blocked(appState, glm::ivec2(10, 13)));
    EXPECT_TRUE(is_tile_blocked(appState, glm::ivec2(13, 13)));
    EXPECT_FALSE(is_tile_blocked(appState, glm::ivec2(14, 10)));
    EXPECT_FALSE(is_tile_blocked(appState, glm::ivec2(10, 9)));
}

TEST(GameLogicHelpersTest, IsTileBlockedEmptyAppState)
{
    AppState appState;
    EXPECT_FALSE(is_tile_blocked(appState, glm::ivec2(0, 0)));
    EXPECT_FALSE(is_tile_blocked(appState, glm::ivec2(100, 100)));
}

TEST(GameLogicHelpersTest, BlockedTileTranslationsPineTree)
{
    AppState appState;
    PineTree tree;
    tree.uuid = 1;
    tree.tile = glm::ivec2(3, 7);
    appState.pineTrees[tree.uuid] = tree;

    auto blocked = blocked_tile_translations(appState);
    ASSERT_EQ(blocked.size(), 1u);
    EXPECT_EQ(blocked[0], tile_to_world(glm::ivec2(3, 7)));
}

TEST(GameLogicHelpersTest, BlockedTileTranslationsTownCenter)
{
    AppState appState;
    TownCenter tc;
    tc.uuid = 1;
    tc.tile = glm::ivec2(5, 5);
    appState.townCenters[tc.uuid] = tc;

    auto blocked = blocked_tile_translations(appState);
    ASSERT_EQ(blocked.size(), 16u); // 4x4 grid

    // Check a few expected tiles
    bool found00 = false, found33 = false;
    for (const glm::vec2& v : blocked) {
        if (v == tile_to_world(glm::ivec2(5, 5))) found00 = true;
        if (v == tile_to_world(glm::ivec2(8, 8))) found33 = true;
    }
    EXPECT_TRUE(found00);
    EXPECT_TRUE(found33);
}

TEST(GameLogicHelpersTest, BlockedTileTranslationsMultipleObjects)
{
    AppState appState;
    PineTree tree;
    tree.uuid = 1;
    tree.tile = glm::ivec2(1, 1);
    appState.pineTrees[tree.uuid] = tree;

    TownCenter tc;
    tc.uuid = 2;
    tc.tile = glm::ivec2(10, 10);
    appState.townCenters[tc.uuid] = tc;

    auto blocked = blocked_tile_translations(appState);
    EXPECT_EQ(blocked.size(), 17u); // 1 tree + 16 TC tiles
}

TEST(GameLogicHelpersTest, PointInDragRectInside)
{
    SelectionState sel;
    sel.dragging = true;
    sel.startScreen = glm::dvec2(0.0, 0.0);
    sel.currentScreen = glm::dvec2(100.0, 100.0);

    EXPECT_TRUE(point_in_drag_rect({50.0f, 50.0f}, sel));
}

TEST(GameLogicHelpersTest, PointInDragRectOutside)
{
    SelectionState sel;
    sel.dragging = true;
    sel.startScreen = glm::dvec2(0.0, 0.0);
    sel.currentScreen = glm::dvec2(100.0, 100.0);

    EXPECT_FALSE(point_in_drag_rect({200.0f, 200.0f}, sel));
}

TEST(GameLogicHelpersTest, PointInDragRectReversedCoords)
{
    SelectionState sel;
    sel.dragging = true;
    sel.startScreen = glm::dvec2(100.0, 100.0);
    sel.currentScreen = glm::dvec2(0.0, 0.0);

    EXPECT_TRUE(point_in_drag_rect({50.0f, 50.0f}, sel));
}

} // namespace
