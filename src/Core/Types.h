#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>
#include <unordered_map>
#include <cstdint>

using EntityId = uint64_t;

struct TextureFrame
{
    GLuint texture = 0;
    int frameIndex = 0;
    int width = 0;
    int height = 0;
};

struct AnimationSet
{
    std::vector<TextureFrame> frames;
};

enum class OperationType
{
    WALK,
    BUILD
};

enum class BuildableBuilding
{
    None,
    House,
    Mill,
    MiningCamp,
    LumberCamp
};

struct QueuedOperation
{
    OperationType type;
    glm::vec2 targetPosition; // For WALK
    EntityId buildingId = 0; // For BUILD - EntityId of target house (0 if not yet created)
    glm::ivec2 targetTile; // For BUILD - tile position of building
    BuildableBuilding buildingType = BuildableBuilding::None; // For BUILD - type to create if buildingId == 0
};

struct Villager
{
    EntityId uuid = 0;
    glm::vec2 position = glm::vec2(0.0f);
    glm::vec2 targetPosition = glm::vec2(0.0f);
    glm::vec2 facingDirection = glm::vec2(1.0f, 0.0f);
    bool selected = false;
    bool moving = false;
    float moveSpeed = 53.66563f * 0.8f * 1.7f * 1.0f;
    float walkAnimTimer = 0.0f;
    int walkFrameIndex = 0;
    std::vector<QueuedOperation> operationQueue; // Unified queue for walk/build operations
    int hp = 25;
    int maxHp = 25;
    bool isGarrisoned = false;
    bool isMovingToGarrison = false;
    EntityId targetTcId = 0;
    EntityId garrisonTcId = 0;
    bool isBuilding = false;
    EntityId buildingTargetId = 0;
    int builderFrameIndex = 0;
    float builderAnimTimer = 0.0f;
};

struct PineTree
{
    EntityId uuid = 0;
    glm::ivec2 tile = glm::ivec2(0);
    glm::vec2 position = glm::vec2(0.0f);
    bool selected = false;
    int hp = 100;
    int maxHp = 100;
};

struct House
{
    EntityId uuid = 0;
    glm::ivec2 tile = glm::ivec2(0);
    glm::vec2 position = glm::vec2(0.0f);
    bool selected = false;
    int hp = 500;
    int maxHp = 500;
    bool isUnderConstruction = true;
    bool isGhostFoundation = true; // Ghost doesn't block tiles until villager arrives
    float buildProgress = 0.0f;
    EntityId assignedVillagerId = 0;
};

struct TownCenter
{
    EntityId uuid = 0;
    glm::ivec2 tile = glm::ivec2(0);
    glm::vec2 position = glm::vec2(0.0f);
    bool selected = false;
    int hp = 2400;
    int maxHp = 2400;
    int villagerQueueCount = 0;
    float villagerTrainingTimer = 0.0f;
    int garrisonCount = 0;
    int maxGarrison = 15;
    glm::vec2 gatherPoint = glm::vec2(0.0f);
    bool hasGatherPoint = false;
    int attack = 5;
    int range = 6;
    bool gatherPointIsSelf = false;
};

struct SelectionState
{
    bool dragging = false;
    bool moved = false;
    glm::dvec2 startScreen = glm::dvec2(0.0);
    glm::dvec2 currentScreen = glm::dvec2(0.0);
};

struct FPSState
{
    static constexpr int SAMPLE_COUNT = 60;
    float frameTimes[SAMPLE_COUNT] = {};
    int sampleIndex = 0;
    float accumulatedTime = 0.0f;
    int currentFPS = 0;
    float updateTimer = 0.0f;
    static constexpr float UPDATE_INTERVAL = 0.25f;
};

enum class CursorMode
{
    Normal,
    Garrison,
    BuildEco,
    BuildMil
};

struct BuildTask
{
    EntityId villagerId = 0;
    EntityId buildingId = 0;
    glm::ivec2 targetTile;
};

struct AppState
{
    std::unordered_map<EntityId, Villager> villagers;
    std::unordered_map<EntityId, PineTree> pineTrees;
    std::unordered_map<EntityId, TownCenter> townCenters;
    std::unordered_map<EntityId, House> houses;

    glm::vec2 pineTreeSpriteSize = glm::vec2(108.0f, 162.0f);
    glm::vec2 townCenterSpriteSize = glm::vec2(256.0f, 256.0f);
    glm::vec2 houseSpriteSize = glm::vec2(128.0f, 128.0f);
    SelectionState selection;
    glm::dvec2 cursorScreen = glm::dvec2(0.0);
    EntityId selectedTreeId = 0;
    
    std::vector<bool> explored;
    std::vector<bool> visible;
    std::vector<float> tileVisibilities;
    std::vector<uint32_t> minimapPixels;
    GLuint minimapTexture = 0;

    int food = 200;
    int wood = 200;
    int stone = 150;
    int gold = 100;

    int maxPopulation = 5;
    int housePopulationBonus = 0;

    float inGameTime = 0.0f;
    int currentAge = 1; // 1 = Dark Age
    FPSState fps;
    CursorMode cursorMode = CursorMode::Normal;
    BuildableBuilding selectedBuilding = BuildableBuilding::None;

    std::vector<BuildTask> buildTasks;
    glm::ivec2 pendingBuildTile = glm::ivec2(-1, -1);
    bool canBuildAtPendingTile = false;
};

