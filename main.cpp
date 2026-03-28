#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow* window);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

// settings
unsigned int SCR_WIDTH = 1280;
unsigned int SCR_HEIGHT = 720;

// Camera
float cameraX = 0.0f;
float cameraY = -4800.0f; // Start roughly in the middle of the 200x200 grid
float cameraSpeed = 1000.0f; // Pixels per second
float zoom = 1.0f;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

const char* vertexShaderSource = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    layout (location = 1) in vec2 aOffset;

    uniform mat4 uProjection;
    uniform mat4 uView;

    void main()
    {
        // Position the instance in the world
        vec2 worldPos = aPos + aOffset;
        
        // Apply view and projection matrices
        gl_Position = uProjection * uView * vec4(worldPos, 0.0, 1.0);
    }
)";

const char* fragmentShaderSource = R"(
    #version 330 core
    out vec4 FragColor;
    uniform vec4 uColor;

    void main()
    {
        FragColor = uColor;
    }
)";

int main()
{
    // glfw: initialize and configure
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // glfw window creation
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "AoE2 Clone - Isometric Platform", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // glad: load all OpenGL function pointers
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // build and compile our shader program
    // vertex shader
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    
    // fragment shader
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);
    
    // link shaders
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Tile diamond geometry (96x48)
    // Used for the solid fill (two triangles)
    float tileVertices[] = {
        0.0f,  24.0f, // Top
       -48.0f,  0.0f, // Left
        48.0f,  0.0f, // Right
        
       -48.0f,  0.0f, // Left
        0.0f, -24.0f, // Bottom
        48.0f,  0.0f  // Right
    };

    // Diamond perimeter for pure diamond grid outline!
    float tileOutline[] = {
        0.0f,  24.0f, // Top
        48.0f,  0.0f, // Right
        0.0f, -24.0f, // Bottom
       -48.0f,  0.0f  // Left
    };

    // Isometric grid generation (200x200 tiles)
    const int GRID_SIZE = 200;
    std::vector<glm::vec2> translations;
    translations.reserve(GRID_SIZE * GRID_SIZE);

    float halfWidth = 96.0f / 2.0f;
    float halfHeight = 48.0f / 2.0f;

    for (int y = 0; y < GRID_SIZE; y++)
    {
        for (int x = 0; x < GRID_SIZE; x++)
        {
            float screenX = (x - y) * halfWidth;
            float screenY = -(x + y) * halfHeight; // Negative so it builds downwards
            translations.push_back(glm::vec2(screenX, screenY));
        }
    }

    // VAO/VBO setup
    unsigned int VAO, VBO, outlineVAO, outlineVBO, instanceVBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenVertexArrays(1, &outlineVAO);
    glGenBuffers(1, &outlineVBO);
    glGenBuffers(1, &instanceVBO);

    // --- Fill Geometry ---
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tileVertices), tileVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // Attach instance attributes to Fill VAO
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, translations.size() * sizeof(glm::vec2), &translations[0], GL_STATIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    glVertexAttribDivisor(1, 1);

    // --- Outline Geometry ---
    glBindVertexArray(outlineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, outlineVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tileOutline), tileOutline, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    // Attach same instance buffer to Outline VAO
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
    glVertexAttribDivisor(1, 1);

    // Get Uniforms
    int uColorLoc = glGetUniformLocation(shaderProgram, "uColor");
    int uProjLoc = glGetUniformLocation(shaderProgram, "uProjection");
    int uViewLoc = glGetUniformLocation(shaderProgram, "uView");

    glUseProgram(shaderProgram);
    
    // Enable blending for smooth lines
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // render loop
    while (!glfwWindowShouldClose(window))
    {
        // per-frame time logic
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // input
        processInput(window);

        // render
        // Use a perfectly black background for out-of-bounds area like AoE2
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        
        // Configure projection matrix taking zoom into account
        float halfW = (static_cast<float>(SCR_WIDTH) / 2.0f) / zoom;
        float halfH = (static_cast<float>(SCR_HEIGHT) / 2.0f) / zoom;
        
        glm::mat4 projection = glm::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-cameraX, -cameraY, 0.0f));

        glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        
        // 1. Draw solid green tiles
        glBindVertexArray(VAO);
        glUniform4f(uColorLoc, 0.2f, 0.6f, 0.2f, 1.0f); // Base platform green
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GRID_SIZE * GRID_SIZE);

        // 2. Draw border lines strictly using the diamond outline (no internal triangle diagonals)
        glBindVertexArray(outlineVAO);
        glUniform4f(uColorLoc, 0.1f, 0.4f, 0.1f, 1.0f); // Dark green outline
        // We use GL_LINE_LOOP to draw the perfect outline around the 4 points
        glDrawArraysInstanced(GL_LINE_LOOP, 0, 4, GRID_SIZE * GRID_SIZE);


        // glfw: swap buffers and poll IO events
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // de-allocate all resources
    glDeleteVertexArrays(1, &VAO);
    glDeleteVertexArrays(1, &outlineVAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &outlineVBO);
    glDeleteBuffers(1, &instanceVBO);
    glDeleteProgram(shaderProgram);

    // glfw: terminate
    glfwTerminate();
    return 0;
}

void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    // Apply zoom influence to camera panning speed so fast zoom doesn't break motion control
    float velocity = cameraSpeed * deltaTime * (1.0f / zoom);
    
    // Panning directions
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
        cameraY += velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
        cameraY -= velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
        cameraX -= velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
        cameraX += velocity;
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    zoom += static_cast<float>(yoffset) * 0.1f * zoom;
    if (zoom < 0.1f) zoom = 0.1f;
    if (zoom > 10.0f) zoom = 10.0f;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions
    SCR_WIDTH = width;
    SCR_HEIGHT = height;
    glViewport(0, 0, width, height);
}
