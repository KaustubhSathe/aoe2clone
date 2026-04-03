#include <gtest/gtest.h>
#include "src/Game/Pathfinding.h"
#include "src/Math/CoordinateSystem.h"
#include "src/Core/Globals.h"
#include "src/Core/Constants.h"

#include <glm/glm.hpp>

namespace {

struct PathfindingTest : ::testing::Test {
    void SetUp() override {
        SCR_WIDTH = 1280;
        SCR_HEIGHT = 720;
        cameraX = 0.0f;
        cameraY = 0.0f;
        zoom = 1.0f;
        gAppState = &appState;
    }

    void TearDown() override {
        gAppState = nullptr;
    }

    AppState appState;
};

TEST_F(PathfindingTest, FindPathSameTileReturnsSingleWaypoint)
{
    glm::vec2 world = tile_to_world(glm::ivec2(10, 10));
    auto path = find_path(appState, world, world);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.size(), 1u);
}

TEST_F(PathfindingTest, FindPathEmptyStateReturnsPath)
{
    glm::vec2 start = tile_to_world(glm::ivec2(0, 0));
    glm::vec2 target = tile_to_world(glm::ivec2(1, 0));
    auto path = find_path(appState, start, target);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.back(), target);
}

TEST_F(PathfindingTest, FindPathLongerRoute)
{
    glm::vec2 start = tile_to_world(glm::ivec2(0, 0));
    glm::vec2 target = tile_to_world(glm::ivec2(10, 0));
    auto path = find_path(appState, start, target);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.back(), target);
    EXPECT_GE(path.size(), 2u);
}

TEST_F(PathfindingTest, FindPathInvalidStartReturnsEmpty)
{
    auto path = find_path(appState, glm::vec2(1e9f, 1e9f), glm::vec2(0.0f, 0.0f));
    EXPECT_TRUE(path.empty());
}

TEST_F(PathfindingTest, FindPathInvalidTargetReturnsEmpty)
{
    auto path = find_path(appState, glm::vec2(0.0f, 0.0f), glm::vec2(1e9f, 1e9f));
    EXPECT_TRUE(path.empty());
}

TEST_F(PathfindingTest, FindPathBlockedByTree)
{
    PineTree tree;
    tree.tile = glm::ivec2(5, 0);
    appState.pineTrees.push_back(tree);

    glm::vec2 start = tile_to_world(glm::ivec2(0, 0));
    glm::vec2 target = tile_to_world(glm::ivec2(10, 0));

    // Path should still exist but go around the tree
    auto path = find_path(appState, start, target);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.back(), target);
}

TEST_F(PathfindingTest, FindPathBlockedByTownCenter)
{
    TownCenter tc;
    tc.tile = glm::ivec2(5, 0);
    appState.townCenters.push_back(tc);

    glm::vec2 start = tile_to_world(glm::ivec2(0, 0));
    glm::vec2 target = tile_to_world(glm::ivec2(10, 0));

    auto path = find_path(appState, start, target);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.back(), target);
}

TEST_F(PathfindingTest, FindGroupDestinationsSingleUnit)
{
    glm::ivec2 center(10, 10);
    auto destinations = find_group_destinations(appState, center, 1);
    ASSERT_EQ(destinations.size(), 1u);
    EXPECT_EQ(destinations[0], center);
}

TEST_F(PathfindingTest, FindGroupDestinationsSpreadOut)
{
    glm::ivec2 center(10, 10);
    auto destinations = find_group_destinations(appState, center, 5);
    EXPECT_EQ(destinations.size(), 5u);

    // All destinations should be unique
    std::set<glm::ivec2, bool(*)(const glm::ivec2&, const glm::ivec2&)> unique(
        [](const glm::ivec2& a, const glm::ivec2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
    for (const auto& d : destinations) {
        unique.insert(d);
    }
    EXPECT_EQ(unique.size(), destinations.size());
}

TEST_F(PathfindingTest, FindGroupDestinationsZeroReturnsEmpty)
{
    auto destinations = find_group_destinations(appState, glm::ivec2(10, 10), 0);
    EXPECT_TRUE(destinations.empty());
}

TEST_F(PathfindingTest, FindGroupDestinationsExcludesBlockedTiles)
{
    PineTree tree;
    tree.tile = glm::ivec2(11, 10);
    appState.pineTrees.push_back(tree);

    glm::ivec2 center(10, 10);
    auto destinations = find_group_destinations(appState, center, 10);

    // The tree tile should not be in destinations
    for (const auto& d : destinations) {
        EXPECT_NE(d, glm::ivec2(11, 10));
    }
}

} // namespace
