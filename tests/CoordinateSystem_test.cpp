#include <gtest/gtest.h>
#include "src/Math/CoordinateSystem.h"
#include "src/Core/Constants.h"

#include <glm/glm.hpp>

namespace {

TEST(CoordinateSystemTest, TileToWorldProducesCorrectPosition)
{
    glm::ivec2 tile(0, 0);
    glm::vec2 world = tile_to_world(tile);
    EXPECT_EQ(world.x, 0.0f);
    EXPECT_EQ(world.y, 0.0f);
}

TEST(CoordinateSystemTest, TileToWorldOriginIsZero)
{
    glm::vec2 world = tile_to_world(glm::ivec2(0, 0));
    EXPECT_FLOAT_EQ(world.x, 0.0f);
    EXPECT_FLOAT_EQ(world.y, 0.0f);
}

TEST(CoordinateSystemTest, TileToWorldPositiveX)
{
    glm::vec2 world = tile_to_world(glm::ivec2(1, 0));
    EXPECT_FLOAT_EQ(world.x, TILE_HALF_WIDTH);
    EXPECT_FLOAT_EQ(world.y, -TILE_HALF_HEIGHT);
}

TEST(CoordinateSystemTest, TileToWorldPositiveY)
{
    glm::vec2 world = tile_to_world(glm::ivec2(0, 1));
    EXPECT_FLOAT_EQ(world.x, -TILE_HALF_WIDTH);
    EXPECT_FLOAT_EQ(world.y, -TILE_HALF_HEIGHT);
}

TEST(CoordinateSystemTest, WorldToTileRoundtrip)
{
    glm::ivec2 originalTile(5, 3);
    glm::vec2 world = tile_to_world(originalTile);
    auto result = world_to_tile(world);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, originalTile);
}

TEST(CoordinateSystemTest, WorldToTileRoundtripVariousTiles)
{
    std::vector<glm::ivec2> tiles = {
        {0, 0}, {1, 0}, {0, 1}, {10, 5}, {100, 99}, {5, 50}
    };
    for (const glm::ivec2& tile : tiles) {
        glm::vec2 world = tile_to_world(tile);
        auto result = world_to_tile(world);
        ASSERT_TRUE(result.has_value()) << "Failed for tile (" << tile.x << ", " << tile.y << ")";
        EXPECT_EQ(*result, tile) << "Failed for tile (" << tile.x << ", " << tile.y << ")";
    }
}

TEST(CoordinateSystemTest, WorldToTileOutOfBoundsReturnsNullopt)
{
    auto result1 = world_to_tile(glm::vec2(1e9f, 1e9f));
    EXPECT_FALSE(result1.has_value());

    auto result2 = world_to_tile(glm::vec2(-1e9f, -1e9f));
    EXPECT_FALSE(result2.has_value());
}

TEST(CoordinateSystemTest, WorldToTileEdgeOfGrid)
{
    auto result = world_to_tile(tile_to_world(glm::ivec2(GRID_SIZE - 1, GRID_SIZE - 1)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, glm::ivec2(GRID_SIZE - 1, GRID_SIZE - 1));
}

} // namespace
