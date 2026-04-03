#pragma once

struct AppState;
struct GLFWwindow;
struct GLFWcursor;

extern unsigned int SCR_WIDTH;
extern unsigned int SCR_HEIGHT;

extern float cameraX;
extern float cameraY;
extern float cameraSpeed;
extern float zoom;

extern float deltaTime;
extern float lastFrame;

extern AppState* gAppState;
extern GLFWcursor* gGarrisonCursor;
struct EngineState;
extern EngineState* gEngine;
