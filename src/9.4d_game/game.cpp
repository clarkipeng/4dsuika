#include "game.h"
#include "threadpool.hpp"
#include "physics_solver.hpp"
// #include "resource_manager.h"
#include "ballrenderer.hpp"
#include "stb_image.h"

#include "learnopengl/shader.h"
#include "learnopengl/filesystem.h"

#include "hemisphere_boundary.hpp"
#include <random>

//debug
// #include <glm/gtx/string_cast.hpp>

// #include "learnopengl/camera.h"

// Game-related State data
BallRenderer    *b_rend;
PhysicSolver *physics_solver;
tp::ThreadPool *thread_pool;
Shader *background_shader;

Shader *ballshader;

const float light_radius = 20.0f;
const float angle_in_sky = glm::radians(30.0f);
const float max_height = light_radius*sin(angle_in_sky); // height of the sun at its peak (noon)
const glm::vec3 light_color(300.0f);


HemisphereBoundary boundary(
    glm::vec4(0.0f), // Center of the hemisphere
    6);
// GameObject        *Player;
// BallObject        *Ball;
// ParticleGenerator *Particles;
// PostProcessor     *Effects;
// ISoundEngine      *SoundEngine = createIrrKlangDevice();
// TextRenderer      *Text;

float ShakeTime = 0.0f;
float                   angle=0;
float                   angle_speed=10;
float                   dim4=0;
bool place_object = false;

float getRandomFloat(float min, float max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}

unsigned int loadCubemap(std::vector<std::string> faces)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++)
    {
        unsigned char *data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data)
        {
            GLenum format;
            if (nrChannels == 1)
                format = GL_RED;
            else if (nrChannels == 3)
                format = GL_RGB;
            else if (nrChannels == 4)
                format = GL_RGBA;

            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else
        {
            std::cout << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
            stbi_image_free(data);
        }
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

// utility function for loading a 2D texture from file
// ---------------------------------------------------
unsigned int loadTexture(char const * path)
{
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data)
    {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Texture failed to load at path: " << path << std::endl;
        stbi_image_free(data);
    }

    return textureID;
}

unsigned int cubeVAO = 0;
unsigned int cubeVBO = 0;
void renderCube()
{
    // initialize (if necessary)
    if (cubeVAO == 0)
    {
        float vertices[] = {
            // back face
            -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
             1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
             1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f, // bottom-right         
             1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
            -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
            -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f, // top-left
            // front face
            -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
             1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f, // bottom-right
             1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
             1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
            -1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f, // top-left
            -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
            // left face
            -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
            -1.0f,  1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-left
            -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
            -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
            -1.0f, -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-right
            -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
            // right face
             1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
             1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
             1.0f,  1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-right         
             1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
             1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
             1.0f, -1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-left     
            // bottom face
            -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
             1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 1.0f, // top-left
             1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
             1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
            -1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 0.0f, // bottom-right
            -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
            // top face
            -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
             1.0f,  1.0f , 1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
             1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 1.0f, // top-right     
             1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
            -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
            -1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 0.0f  // bottom-left        
        };
        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        // fill buffer
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        // link vertex attributes
        glBindVertexArray(cubeVAO);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    // render Cube
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}
struct ViewState {
    float w=0.0f;
    const float w_min=-5.0f, w_max=5.0f;

    glm::vec3 sphereCenter = glm::vec3(0.0f, 0.0f, 0.0f);
    float sphereRadius = 5.0f;

    glm::vec3 orbitTarget = sphereCenter; //+ glm::vec3(0, sphereRadius, 0);
    float radius = 8.0f;

    float yaw = glm::radians(90.0f);    // looking along +X
    float pitch = glm::radians(20.0f);  // slight downward

    const float pitchMin = glm::radians(-89.0f);
    const float pitchMax = glm::radians(89.0f);

    bool rotating = false;
    double lastX = 0.0, lastY = 0.0;

    // Update camera position based on current yaw, pitch, radius
    glm::vec3 getCameraPosition() const {
        float x = radius * cos(pitch) * cos(yaw);
        float y = radius * sin(pitch);
        float z = radius * cos(pitch) * sin(yaw);

        return orbitTarget + glm::vec3(x, y, z);
    }
    glm::mat4 getViewMatrix() const {
        glm::vec3 dir = {
            cosf(pitch) * cosf(yaw),
            sinf(pitch),
            cosf(pitch) * sinf(yaw)
        };
        glm::vec3 eye = orbitTarget - dir * radius;
        return glm::lookAt(eye, orbitTarget, glm::vec3(0, 1, 0));
    }

    // glm::mat4 getViewMatrix() const {
    //     glm::vec3 camPos = getCameraPosition();
    //     return glm::lookAt(camPos, orbitTarget, glm::vec3(0,1,0));
    // }

    // Call on mouse button press/release
    void onMouseButton(int button, int action, double xpos, double ypos) {
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                rotating = true;
                lastX = xpos;
                lastY = ypos;
            } else if (action == GLFW_RELEASE) {
                rotating = false;
            }
        }
    }

    // Call on mouse move
    void onCursorPos(double xpos, double ypos) {
        if (!rotating) return;

        float sensitivity = 0.005f;
        float dx = float(xpos - lastX);
        float dy = float(ypos - lastY);

        yaw   += dx * sensitivity;
        pitch -= dy * sensitivity;

        if (pitch > pitchMax) pitch = pitchMax;
        if (pitch < pitchMin) pitch = pitchMin;

        lastX = xpos;
        lastY = ypos;
    }

    // Call on scroll
    void onScroll(double yoffset) {
        float zoomSpeed = 1.0f;
        radius -= float(yoffset) * zoomSpeed;
        if (radius < sphereRadius + 1.0f) radius = sphereRadius + 1.0f;
        if (radius > sphereRadius + 50.0f) radius = sphereRadius + 50.0f;
    }
    
} state;


// Result structure for ray intersection
struct RayIntersectionResult {
    bool hit = false;
    float distance = FLT_MAX;
    glm::vec3 point = glm::vec3(0.0f);
    PhysicsObject* gameObject = nullptr;
    
    RayIntersectionResult() = default;
    
    RayIntersectionResult(bool h, float d, glm::vec3 p, PhysicsObject* obj)
        : hit(h), distance(d), point(p), gameObject(obj) {}
};

RayIntersectionResult testRaySphereIntersection(const glm::vec3& rayOrigin, 
                                                const glm::vec3& rayDirection, 
                                                PhysicsObject* gameObject) {
    RayIntersectionResult result;
    
    glm::vec3 oc = rayOrigin - glm::vec3(gameObject->position);
    float a = glm::dot(rayDirection, rayDirection);
    float b = 2.0f * glm::dot(oc, rayDirection);
    float c = glm::dot(oc, oc) - (gameObject->radius * gameObject->radius - (gameObject->position.w - state.w)*(gameObject->position.w - state.w));
    
    float discriminant = b * b - 4 * a * c;

    
    if (discriminant < 0) {
        return result; // No intersection, hit remains false
    }
    
    // Calculate the two intersection points
    float sqrt_discriminant = sqrt(discriminant);
    float t1 = (-b - sqrt_discriminant) / (2.0f * a);
    float t2 = (-b + sqrt_discriminant) / (2.0f * a);
    
    // We want the closest positive intersection
    float t = -1.0f;
    if (t1 > 0 && t2 > 0) {
        t = std::min(t1, t2); // Both positive, take closest
    } else if (t1 > 0) {
        t = t1; // Only t1 is positive
    } else if (t2 > 0) {
        t = t2; // Only t2 is positive
    }
    
    if (t > 0) {
        result.hit = true;
        result.distance = t;
        result.point = rayOrigin + t * rayDirection;
        result.gameObject = gameObject;
    }
    
    return result;
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    state.onMouseButton(button, action, xpos, ypos);

    glm::mat4 view_matrix = state.getViewMatrix();

    // Get window dimensions
    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);

    glm::mat4 projection_matrix = glm::perspective(
        glm::radians(45.0f),
        (float)windowWidth / (float)windowHeight,  // Fixed: use actual window dimensions
        0.1f,
        100.0f
    );

    // Ray casting calculation (only do this when we actually need it)
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        // Convert mouse coordinates to normalized device coordinates
        float x = (2.0f * (float)xpos) / (float)windowWidth - 1.0f;
        float y = 1.0f - (2.0f * (float)ypos) / (float)windowHeight;
        
        glm::vec3 ray_nds = glm::vec3(x, y, 1.0f);
        glm::vec4 ray_clip = glm::vec4(ray_nds.x, ray_nds.y, -1.0f, 1.0f);
        
        // Transform to eye space
        glm::vec4 ray_eye = glm::inverse(projection_matrix) * ray_clip;
        ray_eye = glm::vec4(ray_eye.x, ray_eye.y, 1.0f, 0.0f);
        
        // Transform to world space
        glm::vec3 ray_world = glm::vec3(glm::inverse(view_matrix) * ray_eye);
        ray_world = glm::normalize(ray_world);  // Fixed: use glm::normalize
        
        // Ray origin is camera position
        glm::vec3 ray_origin = state.getCameraPosition();
        
        // Now you can do intersection tests
        // Example: Test against sphere
        RayIntersectionResult closestResult;
        for (int i=0;i<MAX_OBJECTS;i++) {
            if (!physics_solver->has_obj[i]) continue;
            RayIntersectionResult result = testRaySphereIntersection(ray_origin, ray_world, &physics_solver->objects[i]);
            
            if (result.hit && result.distance < closestResult.distance) {
                closestResult = result;
            }
        }
        std::cout << "Ray origin: " << ray_origin.x << ", " << ray_origin.y << ", " << ray_origin.z << std::endl;
        std::cout << "Ray direction: " << ray_world.x << ", " << ray_world.y << ", " << ray_world.z << std::endl;

        
        if (closestResult.hit) {
            std::cout<<"HIT"<<std::endl;
            physics_solver->addObject(
                PhysicsObject(glm::vec4(closestResult.point, 0.0f), 1, true, false
                )
            );
        } else {
            // if (objects.)

            physics_solver->addObject(
                PhysicsObject(glm::vec4(0.0f), 1, true, false
                )
            );
        }
    }
    
    // if (button == GLFW_MOUSE_BUTTON_LEFT) {
    //     if (action == GLFW_PRESS) {
    //         // place_object=true;
    //     } else if (action == GLFW_RELEASE) {
    //         // Handle release
    //     }
    // }
}


void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    state.onCursorPos(xpos, ypos);
}

void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    state.onScroll(yoffset);
}

Game::Game(unsigned int width, unsigned int height) {
    this->State = GAME_ACTIVE;
    this->Width = width;
    this->Height = height;

    // Initialize keys to false
    for (int i = 0; i < 1024; i++) {
        this->Keys[i] = false;
        this->KeysProcessed[i] = false;
    }

    // Initialize the physics solver and thread pool
    thread_pool = new tp::ThreadPool(4); // Adjust the number of threads as needed

    // glm::vec3 center, float radius, float angleDegrees = 90.0f, float margin = 0.01f
    // physics_solver = new PhysicSolver(*thread_pool, &boundary);
    physics_solver = new PhysicSolver(*thread_pool);
}
Game::~Game(){
    delete b_rend;
    delete physics_solver;
    delete thread_pool;
}
void Game::Init(GLFWwindow* window){
    ballshader = new Shader("2.2.2.pbr.vs", "2.2.2.pbr.fs");

    ballshader->use();
    ballshader->setInt("texture_diffuse", 0);    // or whatever your sampler is named
    ballshader->setInt("environmentMap", 0);

    GLint success;
    char infoLog[512];
    glGetShaderiv(ballshader->ID, GL_LINK_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(ballshader->ID, 512, NULL, infoLog);
        std::cerr << "[ballshader] Link error:\n" << infoLog << std::endl;
    }
    b_rend = new BallRenderer(ballshader, 5, SPHERE);

    background_shader = new Shader("2.2.2.background.vs", "2.2.2.background.fs");

    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);

    std::vector<std::string> faces
    {
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // right
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // left
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // top
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // bottom
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // front
        FileSystem::getPath("resources/textures/pbr/wall/albedo.png"),  // back
    };
    simpleCubemap = loadCubemap(faces);
    balltexture = loadTexture(FileSystem::getPath("resources/textures/pbr/gold/metallic.png").c_str());

}
void Game::ProcessInput(float dt){
    // // Handle input for camera movement
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_W) == GLFW_PRESS) {
        state.w += 1.0f * dt; // Move up
    }
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_S) == GLFW_PRESS) {
        state.w -= 1.0f * dt; // Move down
    }
    // if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_A) == GLFW_PRESS) {
    //     state.orbitTarget.x -= 1.0f * dt; // Move left
    // }
    // if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_D) == GLFW_PRESS) {
    //     state.orbitTarget.x += 1.0f * dt; // Move right
    // }
    // if (place_object){
    //     std::cout<<"OBJECT PLACED"<<std::endl;

    //     // Somewhere in your code where you're adding the physics object:
    //     glm::vec3 randomOffset3D(
    //         getRandomFloat(-0.5f, 0.5f),
    //         getRandomFloat(-0.5f, 0.5f),
    //         getRandomFloat(-0.5f, 0.5f)
    //     );

    //     physics_solver->addObject(
    //         PhysicsObject(glm::vec4(state.sphereCenter+randomOffset3D, 0.0f), 1, true, false
    //         )
    //     );
    //     place_object = false;
    // }
}
void Game::Update(float dt){
    physics_solver->update(dt);
}


void Game::Render(){
    // Clear buffers
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Setup shader and matrices
    glm::mat4 view = state.getViewMatrix();
    glm::mat4 projection = glm::perspective(
        glm::radians(45.0f),
        (float)this->Width / (float)this->Height,
        0.1f,
        100.0f
    );
    glm::vec3 cameraPos = state.getCameraPosition();
    
    b_rend->shader->use();
    b_rend->shader->setMat4("view", view);
    b_rend->shader->setMat4("projection", projection);
    b_rend->shader->setVec3("camPos", cameraPos);
    


    b_rend->shader->setFloat("metallic", 0.0f);
    b_rend->shader->setFloat("roughness", 0.5f);
    b_rend->shader->setFloat("ao", 1.0f);
    b_rend->shader->setVec3("albedo", glm::vec3(1.0f, 0.5f, 0.5f)); // Set albedo color

    float theta = 3.1415*(state.w-state.w_min) / (state.w_max-state.w_min); // or use glfwGetTime(), animating from 0 to 2π
    float x = light_radius * cos(theta);
    float y = max_height * sin(theta);
    b_rend->shader->setVec3("lightPositions[" + std::to_string(0) + "]", glm::vec3(x,y,light_radius * cos(angle_in_sky)));
    b_rend->shader->setVec3("lightColors[" + std::to_string(0) + "]", light_color);

    // Debug output (only occasionally to avoid spam)
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 60 == 0) { // Print every 60 frames (once per second at 60fps)
        std::cout << "=== FRAME " << frameCount/60 << " ===" << std::endl;
        std::cout << "Camera pos: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")" << std::endl;
        std::cout << "W coordinate: " << state.w << std::endl;
        std::cout << "Objects to render: " << physics_solver->objects.size() << std::endl;
    }

    // Render all physics objects
    int renderedCount = 0;
    // for (auto &obj : physics_solver->objects) {
    for (int i=0;i<MAX_OBJECTS;i++) {
        if (!physics_solver->has_obj[i])continue;
        PhysicsObject &obj = physics_solver->objects[i];
        if (obj.hidden) continue;
        
        // Debug 4D projection calculation
        float w_diff = state.w - obj.position.w;
        float projected_scale = sqrt(obj.radius*obj.radius - w_diff*w_diff);
        
        if (frameCount % 60 == 0) {
            std::cout << "Object " << renderedCount << ": pos(" 
                      << obj.position.x << ", " << obj.position.y << ", " 
                      << obj.position.z << ", " << obj.position.w << ")" << std::endl;
            std::cout << "  w_diff=" << w_diff << ", projected_scale=" << projected_scale << std::endl;
        }
        
        // Skip objects that would be behind the 4D viewing plane or have invalid scale
        // if (abs(w_diff) < 0.001f) {
        //     if (frameCount % 60 == 0) {
        //         std::cout << "  SKIPPED: Object too close to 4D viewing plane" << std::endl;
        //     }
        //     continue;
        // }
        
        // if (abs(projected_scale) > 100.0f) {
        //     if (frameCount % 60 == 0) {
        //         std::cout << "  SKIPPED: Projected scale too large (" << projected_scale << ")" << std::endl;
        //     }
        //     continue;
        // }
        
        // Render the object
        b_rend->Draw4d(state.w, balltexture, obj.position, obj.radius, 
                       glm::vec3(0.0f), glm::vec3(1.0f, 0.5f, 0.5f));
        // GLenum err;
        // while ((err = glGetError()) != GL_NO_ERROR) {
        //     std::cerr << "[Draw4d] GL ERROR: " << err << std::endl;
        // }
        renderedCount++;
    }
    
    if (frameCount % 60 == 0) {
        std::cout << "Actually rendered: " << renderedCount << " objects" << std::endl;
    }

    // Render skybox LAST (after all objects)
    glDepthFunc(GL_LEQUAL); // Change depth function so skybox renders behind everything
    
    background_shader->use();
    
    // Remove translation from view matrix for skybox
    glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
    background_shader->setMat4("view", skyboxView);
    background_shader->setMat4("projection", projection);
    background_shader->setInt("environmentMap", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, simpleCubemap);
    renderCube();
    
    // Restore default depth function
    glDepthFunc(GL_LESS);
}
// void Game::ResetLevel(){

// }
// void Game::ResetPlayer(){

// }