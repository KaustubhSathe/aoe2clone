#pragma once
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <vector>
#include <iostream>
#include <optional>
#include "Types.h"

// =============================================================================
// Declared here so main() can register them as GLFW callbacks before they are
// defined at the bottom of the file.
// =============================================================================
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow* window);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

struct GPUState {
    GLuint tileVAO = 0;
    GLuint tileVBO = 0;
    GLuint outlineVAO = 0;
    GLuint outlineVBO = 0;
    GLuint instanceVBO = 0;
    GLuint visibilityVBO = 0;
    GLuint visibleTileInstanceVBO = 0;  // Dynamic VBO for frustum culled tile translations
    GLuint visibleTileVisibilityVBO = 0;  // Dynamic VBO for frustum culled tile visibility
    GLuint blockedTileVAO = 0;
    GLuint blockedTileVBO = 0;
    GLuint blockedInstanceVBO = 0;
    
    GLuint spriteVAO = 0;
    GLuint spriteVBO = 0;
    
    GLuint selectionVAO = 0;
    GLuint selectionVBO = 0;
    GLuint rectVAO = 0;
    GLuint rectVBO = 0;
    GLuint hitboxVAO = 0;
    GLuint hitboxVBO = 0;
    
    GLuint pineBoundsVAO = 0;
    GLuint pineBoundsVBO = 0;

    GLuint tileShaderProgram = 0;
    GLuint spriteShaderProgram = 0;
    GLuint overlayShaderProgram = 0;
    
    GLint tileColorLoc = -1;
    GLint tileProjLoc = -1;
    GLint tileViewLoc = -1;
    GLint spriteProjLoc = -1;
    GLint spriteViewLoc = -1;
    GLint spritePosLoc = -1;
    GLint spriteSizeLoc = -1;
    GLint spriteVisLoc = -1;

    GLint overlayProjLoc = -1;
    GLint overlayViewLoc = -1;
    GLint overlayOffsetLoc = -1;
    GLint overlayColorLoc = -1;
};

struct EngineState {
    GPUState gpu;
    AnimationSet walkAnimation;
    AnimationSet idleAnimation;
    AnimationSet builderAnimation;
    std::optional<TextureFrame> pineTreeFrame;
    std::optional<TextureFrame> townCenterFrame;
    std::optional<TextureFrame> houseFrame;
    
    std::vector<glm::vec2> translations;
    std::vector<glm::vec2> blockedTileTranslations;
    
    TextureFrame buildEconomicIcon;
    TextureFrame buildMilitaryIcon;
    TextureFrame repairIcon;
    TextureFrame garrisonIcon;
    TextureFrame stopIcon;
    TextureFrame houseIcon;
    TextureFrame millIcon;
    TextureFrame miningCampIcon;
    TextureFrame lumberCampIcon;
    TextureFrame barracksIcon;
    TextureFrame archeryRangeIcon;
    TextureFrame stableIcon;
    TextureFrame siegeWorkshopIcon;
    TextureFrame houseStage0;
    TextureFrame houseStage1;
    TextureFrame houseStage2;
    TextureFrame houseStage3;
};

void rebuild_blocked_tiles(EngineState& engine, AppState& appState);


