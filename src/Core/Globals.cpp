#include "Globals.h"
#include <GLFW/glfw3.h>

unsigned int SCR_WIDTH = 1280;
unsigned int SCR_HEIGHT = 720;

float cameraX = 0.0f;
float cameraY = -4800.0f;
float cameraSpeed = 1000.0f;
float zoom = 1.0f;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

AppState* gAppState = nullptr;
GLFWcursor* gGarrisonCursor = nullptr;
EngineState* gEngine = nullptr;
