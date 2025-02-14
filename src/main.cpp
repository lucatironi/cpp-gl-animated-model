// File: main.cpp
#include "animated_model.hpp"
#include "cube_model.hpp"
#include "fps_camera.hpp"
#include "frustum_box.hpp"
#include "gltf_loader.hpp"
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
void Render(const Shader& shader, float currentTime);

void RenderQuad();
std::vector<glm::vec3> GetFrustumCornersWorldSpace(const glm::mat4& viewProjMatrix);
glm::mat4 CalcLightSpaceMatrix(const glm::vec3& worldMin, const glm::vec3& worldMax);

FPSCamera Camera;
std::shared_ptr<CubeModel> Cube;
std::shared_ptr<PlaneModel> Floor;
std::unique_ptr<AnimatedModel> AnimModel;

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
    float WorldSize = 10.0f;
    glm::vec3 LightDir = glm::normalize(glm::vec3(0.5f, 1.0f, 1.0f));
    glm::vec3 LightColor = glm::vec3(1.0f, 1.0f, 0.8f);
    glm::vec3 AmbientColor = glm::vec3(1.0f, 1.0f, 1.0f);
    float AmbientIntensity = 0.5f;
    float SpecularShininess = 32.0f;
    float SpecularIntensity = 0.5;
    bool DebugShadow = false;
    bool DebugFrustum = false;
    bool Animate = true;
    unsigned int CurrentAnimation = 1;
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

    AnimModel = std::make_unique<AnimatedModel>();
    GLTFLoader::LoadFromGLTF("assets/vanguard.glb", *AnimModel);
    AnimModel->SetCurrentAnimation(Settings.CurrentAnimation);

    Camera.Position = glm::vec3(0.0f, 2.0f, 2.0f);
    Camera.FOV = Settings.FOV;
    Camera.AspectRatio = static_cast<GLfloat>(Settings.WindowWidth) / static_cast<GLfloat>(Settings.WindowHeight);

    glm::vec3 worldMin(-Settings.WorldSize / 2.0f, 0.0f, -Settings.WorldSize / 2.0f);
    glm::vec3 worldMax(Settings.WorldSize / 2.0f, Settings.WorldSize / 2.0f, Settings.WorldSize / 2.0f);
    glm::mat4 lightViewSpaceMatrix = CalcLightSpaceMatrix(worldMin, worldMax);
    std::vector<glm::vec3> worldFrustumCorners = {
        { worldMin.x, worldMin.y, worldMin.z }, { worldMax.x, worldMin.y, worldMin.z },
        { worldMin.x, worldMax.y, worldMin.z }, { worldMax.x, worldMax.y, worldMin.z },
        { worldMin.x, worldMin.y, worldMax.z }, { worldMax.x, worldMin.y, worldMax.z },
        { worldMin.x, worldMax.y, worldMax.z }, { worldMax.x, worldMax.y, worldMax.z }
    };
    FrustumBox lightSpaceFrustum(GetFrustumCornersWorldSpace(lightViewSpaceMatrix), glm::vec3(1.0f, 1.0f, 0.0f));
    FrustumBox worldFrustum(worldFrustumCorners, glm::vec3(1.0f, 0.0f, 0.0f));

    Shader defaultShader("shaders/default.vs", "shaders/default.fs");
    defaultShader.Use();
    defaultShader.SetMat4("projection", Camera.GetProjectionMatrix());
    defaultShader.SetMat4("lightSpaceMatrix", lightViewSpaceMatrix);
    defaultShader.SetVec3("lightDir", Settings.LightDir);
    defaultShader.SetVec3("lightColor", Settings.LightColor);
    defaultShader.SetVec3("ambientColor", Settings.AmbientColor);
    defaultShader.SetFloat("ambientIntensity", Settings.AmbientIntensity);
    defaultShader.SetFloat("specularShininess", Settings.SpecularShininess);
    defaultShader.SetFloat("specularIntensity", Settings.SpecularIntensity);
    defaultShader.SetInt("depthMap", 3);

    Shader shadowShader("shaders/default.vs", "shaders/shadow.fs");
    shadowShader.Use();
    shadowShader.SetMat4("lightSpaceMatrix", lightViewSpaceMatrix);

    Shader debugShader("shaders/render_to_quad.vs", "shaders/debug_shadows.fs");
    debugShader.Use();
    debugShader.SetInt("depthMap", 0);

    Shader lineShader("shaders/line.vs", "shaders/line.fs");
    lineShader.Use();
    lineShader.SetMat4("projection", Camera.GetProjectionMatrix());

    // configure depth map FBO
    // -----------------------
    const unsigned int SHADOW_WIDTH = 2048, SHADOW_HEIGHT = 2048;
    unsigned int depthMapFBO;
    glGenFramebuffers(1, &depthMapFBO);
    // create depth texture
    unsigned int depthMap;
    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    // attach depth texture as FBO's depth buffer
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

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
        if (Settings.Animate)
            AnimModel->UpdateAnimation(deltaTime);

        // render
        // ------
        glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 1. render depth of scene to texture (from light's perspective)
        // --------------------------------------------------------------
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
            glClear(GL_DEPTH_BUFFER_BIT);
            glCullFace(GL_FRONT);
            shadowShader.Use();
            shadowShader.SetBool("shadowPass", true);
            Render(shadowShader, currentTime);
            glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // reset viewport
        glViewport(0, 0, Settings.WindowWidth, Settings.WindowHeight);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 2. render scene as normal using the generated depth/shadow map
        // --------------------------------------------------------------
        if (Settings.DebugShadow)
        {
            debugShader.Use();
            debugShader.SetFloat("nearPlane", Camera.NearPlane);
            debugShader.SetFloat("farPlane", Camera.FarPlane);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, depthMap);
            RenderQuad();
        }
        else
        {
            defaultShader.Use();
            defaultShader.SetMat4("view", Camera.GetViewMatrix());
            defaultShader.SetVec3("cameraPos", Camera.Position);
            defaultShader.SetBool("shadowPass", false);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, depthMap);
            Render(defaultShader, currentTime);
        }

        if (Settings.DebugFrustum)
        {
            lineShader.Use();
            lineShader.SetMat4("view", Camera.GetViewMatrix());
            lightSpaceFrustum.Draw(lineShader);
            worldFrustum.Draw(lineShader);
        }

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
    else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
    {
        Settings.CurrentAnimation = (Settings.CurrentAnimation + 1) % AnimModel->GetNumAnimations();
        AnimModel->SetCurrentAnimation(Settings.CurrentAnimation);
    }
    else if (key == GLFW_KEY_P && action == GLFW_PRESS)
        Settings.Animate = !Settings.Animate;
    else if (key == GLFW_KEY_O && action == GLFW_PRESS)
        Settings.DebugShadow = !Settings.DebugShadow;
    else if (key == GLFW_KEY_F && action == GLFW_PRESS)
        Settings.DebugFrustum = !Settings.DebugFrustum;
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

void Render(const Shader& shader, float currentTime)
{
    glm::mat4 translationMatrix, rotationMatrix, scaleMatrix, modelMatrix;

    shader.Use();
    shader.SetMat4("view", Camera.GetViewMatrix());
    shader.SetVec3("cameraPos", Camera.Position);

    shader.SetMat4("model", glm::mat4(1.0f));
    Floor->Draw(shader);

    translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.5f, 1.0f));
    rotationMatrix = glm::mat4(1.0f);
    scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f));
    modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;
    shader.SetMat4("model", modelMatrix);
    Cube->Draw(shader);

    translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, -3.0f));
    rotationMatrix = glm::mat4(1.0f);
    scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;
    shader.SetMat4("model", modelMatrix);
    Cube->Draw(shader);

    translationMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    rotationMatrix = glm::mat4(1.0f);
    scaleMatrix = glm::mat4(1.0f);
    modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;
    shader.SetMat4("model", modelMatrix);
    AnimModel->SetBoneTransformations(shader);
    AnimModel->Draw(shader);
}

unsigned int quadVAO = 0;
unsigned int quadVBO;
void RenderQuad()
{
    if (quadVAO == 0)
    {
        float quadVertices[] = {
            // positions        texture Coords
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        // setup plane VAO
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

std::vector<glm::vec3> GetFrustumCornersWorldSpace(const glm::mat4& viewProjMatrix)
{
    const glm::mat4 inv = glm::inverse(viewProjMatrix);
    // Define the 8 corners in NDC space
    std::vector<glm::vec4> ndcCorners = {
        {-1.0f, -1.0f, -1.0f, 1.0f}, // Near Bottom Left
        { 1.0f, -1.0f, -1.0f, 1.0f}, // Near Bottom Right
        {-1.0f,  1.0f, -1.0f, 1.0f}, // Near Top Left
        { 1.0f,  1.0f, -1.0f, 1.0f}, // Near Top Right
        {-1.0f, -1.0f,  1.0f, 1.0f}, // Far Bottom Left
        { 1.0f, -1.0f,  1.0f, 1.0f}, // Far Bottom Right
        {-1.0f,  1.0f,  1.0f, 1.0f}, // Far Top Left
        { 1.0f,  1.0f,  1.0f, 1.0f}  // Far Top Right
    };

    // Transform corners to world space
    std::vector<glm::vec3> worldCorners;
    for (const auto& ndc : ndcCorners)
    {
        glm::vec4 corner = inv * ndc;
        worldCorners.push_back(glm::vec3(corner / corner.w)); // Perform perspective divide
    }

    return worldCorners;
}

glm::mat4 CalcLightSpaceMatrix(const glm::vec3& worldMin, const glm::vec3& worldMax)
{
    glm::mat4 lightView = glm::lookAt(Settings.LightDir, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    std::vector<glm::vec4> worldCorners = {
        { worldMin.x, worldMin.y, worldMax.z, 1.0f }, { worldMax.x, worldMin.y, worldMax.z, 1.0f },
        { worldMax.x, worldMin.y, worldMin.z, 1.0f }, { worldMin.x, worldMin.y, worldMin.z, 1.0f },
        { worldMin.x, worldMax.y, worldMax.z, 1.0f }, { worldMax.x, worldMax.y, worldMax.z, 1.0f },
        { worldMax.x, worldMax.y, worldMin.z, 1.0f }, { worldMin.x, worldMax.y, worldMin.z, 1.0f }};

    std::vector<glm::vec4> frustumCornersLightSpace;
    for (const auto& corner : worldCorners)
        frustumCornersLightSpace.push_back(lightView * corner);

    glm::vec3 min = glm::vec3(FLT_MAX);
    glm::vec3 max = glm::vec3(-FLT_MAX);
    for (const auto& corner : frustumCornersLightSpace)
    {
        min = glm::min(min, glm::vec3(corner));
        max = glm::max(max, glm::vec3(corner));
    }
    float zMarginFactor = 0.3f; // Expand by 30% of the range
    float range = max.z - min.z;
    min.z -= zMarginFactor * range;
    max.z += zMarginFactor * range;

    glm::mat4 lightProjection = glm::ortho(min.x, max.x, min.y, max.y, min.z, max.z);
    glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    return lightSpaceMatrix;
}