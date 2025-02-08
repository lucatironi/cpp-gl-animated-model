// File: main.cpp
#include "assimp/DefaultLogger.hpp"
#include "cube_model.hpp"
#include "fps_camera.hpp"
#include "mesh.hpp"
#include "model.hpp"
#include "plane_model.hpp"
#include "shader.hpp"
#include "texture_2D.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#include <memory>

void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void CursorPosCallback(GLFWwindow* window, double xposIn, double yposIn);

void ProcessInput(GLFWwindow* window, float deltaTime);

FPSCamera Camera;
std::shared_ptr<CubeModel> Cube;
std::shared_ptr<PlaneModel> Floor;
std::unique_ptr<SkinnedModel> AnimatedModel;
unsigned int CurrentAnimation = 0;

bool Animate = false;

bool FirstMouse = true;
float LastX, LastY;

struct Settings
{
    std::string WindowTitle = "OpenGL Animated Model";
    int WindowWidth = 800;
    int WindowHeight = 600;
    int WindowPositionX = 0;
    int WindowPositionY = 0;
    bool FullScreen = false;
    float FOV = 75.0f;
    float WorldSize = 100.0f;
    glm::vec3 LightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 1.0f));
    glm::vec3 LightColor = glm::vec3(1.0f, 1.0f, 0.8f);
    glm::vec3 AmbientColor = glm::vec3(1.0f, 1.0f, 1.0f);
    float AmbientIntensity = 0.5f;
    float SpecularShininess = 32.0f;
    float SpecularIntensity = 0.5;
} Settings;

int main()
{
    // glfw: initialize and configure
    // ------------------------------
    glfwInit();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GL_FALSE);
#endif

    // glfw window creation
    // --------------------
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    GLFWwindow* window = nullptr;
    if (Settings.FullScreen) {
        const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        window = glfwCreateWindow(mode->width, mode->height, Settings.WindowTitle.c_str(), monitor, nullptr);
        Settings.WindowWidth = mode->width;
        Settings.WindowHeight = mode->height;
    }
    else
    {
        window = glfwCreateWindow(Settings.WindowWidth, Settings.WindowHeight, Settings.WindowTitle.c_str(), nullptr, nullptr);
        glfwGetWindowSize(window, &Settings.WindowWidth, &Settings.WindowHeight);
        glfwGetWindowPos(window, &Settings.WindowPositionX, &Settings.WindowPositionY);
    }

    if (window == nullptr)
    {
        std::cerr << "ERROR::GLFW: Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    // disable vsync
    glfwSwapInterval(0);

    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);

    // tell GLFW to capture our mouse
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cerr << "ERROR::GLAD: Failed to initialize GLAD" << std::endl;
        return -1;
    }

    Floor = std::make_shared<PlaneModel>("assets/texture_05.png", Settings.WorldSize);
    Cube = std::make_shared<CubeModel>("assets/texture_05.png");
    AnimatedModel = std::make_unique<SkinnedModel>("assets/Mike.gltf");
    glm::mat4 translationMatrix, rotationMatrix, scaleMatrix, modelMatrix;

    Camera.Position = glm::vec3(0.0f, 2.0f, 2.0f);
    Camera.FOV = Settings.FOV;
    Camera.AspectRatio = static_cast<GLfloat>(Settings.WindowWidth) / static_cast<GLfloat>(Settings.WindowHeight);

    Shader defaultShader("shaders/default.vs", "shaders/default.fs");
    defaultShader.Use();
    defaultShader.SetMat4("projection", Camera.GetProjectionMatrix());
    defaultShader.SetVec3("lightDir", Settings.LightDir);
    defaultShader.SetVec3("lightColor", Settings.LightColor);
    defaultShader.SetVec3("ambientColor", Settings.AmbientColor);
    defaultShader.SetFloat("ambientIntensity", Settings.AmbientIntensity);
    defaultShader.SetFloat("specularShininess", Settings.SpecularShininess);
    defaultShader.SetFloat("specularIntensity", Settings.SpecularIntensity);

    // setup OpenGL
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    // game loop
    // -----------
    float currentTime = 0.0f;
    float lastTime    = 0.0f;
    float lastFPSTime = 0.0f;
    float deltaTime   = 0.0f;
    int frames = 0;
    int fps    = 0;

    while (!glfwWindowShouldClose(window))
    {
        // calculate deltaTime and FPS
        // ---------------------------
        currentTime = glfwGetTime();
        deltaTime = currentTime - lastTime;
        lastTime = currentTime;
        // fps counter
        frames++;
        if ((currentTime - lastFPSTime) >= 1.0f)
        {
            fps = frames;
            frames = 0;
            lastFPSTime = currentTime;
        }

        // input
        // -----
        ProcessInput(window, deltaTime);

        // update
        // ------

        // render
        // ------
        glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        defaultShader.Use();
        defaultShader.SetMat4("view", Camera.GetViewMatrix());
        defaultShader.SetVec3("cameraPos", Camera.Position);

        defaultShader.SetMat4("model", glm::mat4(1.0f));
        Floor->Draw(defaultShader);

        translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.5f, 1.0f));
        rotationMatrix = glm::mat4(1.0f);
        scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
        modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;
        defaultShader.SetMat4("model", modelMatrix);
        Cube->Draw(defaultShader);

        translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
        rotationMatrix = glm::rotate(glm::mat4(1.0f), glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
        modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;
        defaultShader.SetMat4("model", modelMatrix);
        AnimatedModel->SetBoneTransformations(defaultShader, Animate ? currentTime : 0.0f);
        AnimatedModel->Draw(defaultShader);

        // display FPS in window title
        glfwSetWindowTitle(window, (Settings.WindowTitle + " - FPS: " + std::to_string(fps)).c_str());

        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // optional: de-allocate all resources once they've outlived their purpose:
    // ------------------------------------------------------------------------

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
    glfwTerminate();
    return 0;
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void FramebufferSizeCallback(GLFWwindow* /* window */, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
}

// glfw: whenever a mouse button is clicked, this callback is called
// -----------------------------------------------------------------
void MouseButtonCallback(GLFWwindow* window, int button, int action, int /* mods */)
{}

// glfw: whenever a keyboard key is pressed, this callback is called
// -----------------------------------------------------------------
void KeyCallback(GLFWwindow* window, int key, int /* scancode */, int action, int /* mods */)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    if (key == GLFW_KEY_P && action == GLFW_PRESS)
        Animate = !Animate;
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    {
        CurrentAnimation = (CurrentAnimation + 1) % AnimatedModel->GetNumAnimations();
        AnimatedModel->SetAnimation(CurrentAnimation);
        std::cout << "Current Animation: " << CurrentAnimation << std::endl;
    }
}

// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void CursorPosCallback(GLFWwindow* /* window */, double xposIn, double yposIn)
{
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (FirstMouse)
    {
        LastX = xpos;
        LastY = ypos;
        FirstMouse = false;
    }

    float xoffset = xpos - LastX;
    float yoffset = LastY - ypos; // reversed since y-coordinates go from bottom to top

    LastX = xpos;
    LastY = ypos;

    Camera.ProcessMouseMovement(xoffset, yoffset);
}

void ProcessInput(GLFWwindow* window, float deltaTime)
{
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        Camera.Move(MOVE_FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        Camera.Move(MOVE_BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        Camera.Move(MOVE_LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        Camera.Move(MOVE_RIGHT, deltaTime);
}