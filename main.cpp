#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "glm/ext/matrix_float4x4.hpp"

#include <learnopengl/shader_m.h>
#include <learnopengl/camera.h>
#include <learnopengl/filesystem.h>
#include <learnopengl/mesh.h>
#include <learnopengl/shader.h>
#include <learnopengl/animdata.h>
#include <learnopengl/assimp_glm_helpers.h>

#include <assimp/anim.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ==============================================
// Global Config
// ==============================================
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;
const unsigned int GENERATE_FISH = 1;
const float CATCH_RADIUS = 1.2f;
const string MODEL_SHARK_PATH = FileSystem::getPath("src/game_3d/Hungry_Fish_3D/great_white_shark.glb");
const string MODEL_FISH_PATH = FileSystem::getPath("src/game_3d/Hungry_Fish_3D/low_poly_fish.glb");

struct Fish {
    glm::vec3 position;
    glm::vec3 spawnCenter;
    glm::vec3 target;
    float speed;
};

// ==============================================
// Utility Functions
// ==============================================
unsigned int loadTexture(const char *path);
unsigned int loadCubemap(vector<std::string> faces);

// ==============================================
// Model
// ==============================================
class Model {
public:
    std::vector<Mesh> meshes;

    bool init(const std::string& path) {
        loadModel(path);
        return true;
    };

    void draw(Shader& shader){
        for (unsigned int i = 0; i < meshes.size(); i++)
        {
            shader.use();
            meshes[i].Draw(shader);
        }
    }


private:
    void loadModel(const std::string& path) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(
            path, aiProcess_Triangulate | aiProcess_FlipUVs |
            aiProcess_GenNormals | aiProcess_CalcTangentSpace);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
            return;
        }

        std::string directory = path.substr(0, path.find_last_of('/'));
        processNode(scene->mRootNode, scene, directory, path);
    }

    void processNode(aiNode* node, const aiScene* scene, const std::string& directory, const std::string& path) {
        // process meshes
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene, directory, path));
        }

        // then process children
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene, directory, path);
        }
    }

    std::vector<Texture> loadMaterialTextures( const aiScene *scene, aiMaterial *mat, const std::string &directory,  std::vector<Texture> &local_textures_loaded, const std::string &modelPath)
    {
        std::vector<Texture> textures;

        //std::cout << "Total embedded textures in scene: " << scene->mNumTextures << std::endl;

        for (int typeInt = aiTextureType_NONE; typeInt <= aiTextureType_UNKNOWN; ++typeInt)
        {
            aiTextureType type = static_cast<aiTextureType>(typeInt);
            unsigned int count = mat->GetTextureCount(type);
            if (count == 0)
                continue;

            //std::cout << "Found " << count << " textures for type: " << typeInt << std::endl;

            for (unsigned int i = 0; i < count; i++)
            {
                aiString str;
                mat->GetTexture(type, i, &str);
                std::string texPath = str.C_Str();

                // Make embedded texture keys unique per model file
                if (!texPath.empty() && texPath[0] == '*')
                    texPath = modelPath + texPath;

                std::string typeName = aiTextureTypeToString(type);
                unsigned int texID = 0;

                // --- DE-DUPLICATION (per model only) ---
                bool alreadyLoaded = false;
                for (auto &tex : local_textures_loaded)
                {
                    if (tex.path == texPath)
                    {
                        texID = tex.id;
                        alreadyLoaded = true;
                        textures.push_back({texID, typeName, texPath});
                        //std::cout << "Reusing existing texture within model: " << texPath << std::endl;
                        break;
                    }
                }
                if (alreadyLoaded)
                    continue;

                // --- Embedded texture ---
                if (!texPath.empty() && texPath.find(modelPath) != std::string::npos && texPath.find('*') != std::string::npos)
                {
                    int texIndex = std::stoi(texPath.substr(texPath.find('*') + 1));
                    if (texIndex >= 0 && texIndex < (int)scene->mNumTextures)
                    {
                        const aiTexture *aTex = scene->mTextures[texIndex];
                        if (aTex)
                        {
                            unsigned char *data = nullptr;
                            int width = 0, height = 0, nrComponents = 0;
                            bool embedded_compressed = (aTex->mHeight == 0);

                            if (embedded_compressed)
                            {
                                stbi_set_flip_vertically_on_load(true);
                                data = stbi_load_from_memory(
                                    reinterpret_cast<unsigned char *>(aTex->pcData),
                                    aTex->mWidth, &width, &height, &nrComponents, 0);
                            }
                            else
                            {
                                width = aTex->mWidth;
                                height = aTex->mHeight;
                                nrComponents = 4;
                                data = (unsigned char *)aTex->pcData;
                            }

                            if (data)
                            {   
                                glGenTextures(1, &texID);
                                GLenum format = (nrComponents == 3 ? GL_RGB : GL_RGBA);
                                glBindTexture(GL_TEXTURE_2D, texID);
                                glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
                                glGenerateMipmap(GL_TEXTURE_2D);

                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                                if (embedded_compressed)
                                    stbi_image_free(data);

                                Texture texture = {texID, typeName, texPath};
                                textures.push_back(texture);
                                local_textures_loaded.push_back(texture);
                                //std::cout << "Uploaded embedded texture: " << texPath << std::endl;
                            }
                        }
                    }
                }
                // --- External texture ---
                else if (!texPath.empty())
                {
                    std::string fullpath = (texPath.find(":") == std::string::npos && !directory.empty())
                                            ? directory + "/" + texPath
                                            : texPath;
                    texID = loadTexture(fullpath.c_str());
                    if (texID)
                    {
                        Texture texture = {texID, typeName, texPath};
                        textures.push_back(texture);
                        local_textures_loaded.push_back(texture);
                    }
                }
            }
        }

        return textures;
    }

    Mesh processMesh(aiMesh* mesh, const aiScene* scene, const std::string& directory, const std::string& path) {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        std::vector<Texture> textures;

        // --- vertices ---
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;

            // Position
            vertex.Position = {
                mesh->mVertices[i].x,
                mesh->mVertices[i].y,
                mesh->mVertices[i].z
            };

            // Normal
            if (mesh->HasNormals()) {
                vertex.Normal = {
                    mesh->mNormals[i].x,
                    mesh->mNormals[i].y,
                    mesh->mNormals[i].z
                };
            } else {
                vertex.Normal = {0.0f, 0.0f, 0.0f};
            }

            // Texture coords
            if (mesh->mTextureCoords[0]) {
                vertex.TexCoords = {
                    mesh->mTextureCoords[0][i].x,
                    mesh->mTextureCoords[0][i].y
                };
            } else {
                vertex.TexCoords = {0.0f, 0.0f};
            }

            vertices.push_back(vertex);
        }

        // --- indices ---
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
                indices.push_back(face.mIndices[j]);
        }

        // --- textures ---
        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            std::vector<Texture> loadedTextures =
                loadMaterialTextures(scene, material, directory, textures, path);
            textures.insert(textures.end(), loadedTextures.begin(), loadedTextures.end());
        }
        return Mesh(vertices, indices, textures);
    }

};

// ==============================================
// Skybox (Cubemap)
// ==============================================
class Skybox {
public:
    unsigned int VAO, VBO, textureID;

    bool init(const std::vector<std::string>& faces) {
        float skyboxVertices[] = {
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

            -1.0f,  1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f
        };

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        textureID = loadCubemap(faces);
        return textureID != 0;
    }

    void draw(const Shader& shader, Camera& camera, const glm::mat4& projection) const {
        glDepthFunc(GL_LEQUAL);
        shader.use();
        glm::mat4 view = glm::mat4(glm::mat3(camera.GetViewMatrix()));
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);

        glBindVertexArray(VAO);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthFunc(GL_LESS);
    }
};

// ==============================================
// Application
// ==============================================
class Application {
public:
    GLFWwindow* window = nullptr;
    Camera camera{ glm::vec3(0.0f, 0.0f, 0.0f) };
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    bool firstMouse = true;
    float lastX = SCR_WIDTH / 2.0f;
    float lastY = SCR_HEIGHT / 2.0f;
    bool gameOver = false;

    std::vector<Fish> fishes;
    int fishCount = 0;

    Skybox skybox;
    Model sharkModel;
    Model fishModel;
    Shader* skyboxShader = nullptr;
    Shader* sharkShader = nullptr;
    Shader* fishShader = nullptr;

    bool init() {
        // Init GLFW
        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
        // Window
        window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "OOP OpenGL", nullptr, nullptr);
        if (!window) {
            std::cout << "Failed to create GLFW window\n";
            glfwTerminate();
            return false;
        }
        glfwMakeContextCurrent(window);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, framebufferCallback);
        glfwSetCursorPosCallback(window, mouseCallback);
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            std::cout << "Failed to initialize GLAD\n";
            return false;
        }

        glEnable(GL_DEPTH_TEST);

        skyboxShader = new Shader("skybox.vs", "skybox.fs");
        sharkShader = new Shader("model.vs", "model.fs");
        fishShader = new Shader("model.vs", "model.fs");

        std::vector<std::string> faces = {
            FileSystem::getPath("resources/textures/skybox/right.jpg"),
            FileSystem::getPath("resources/textures/skybox/left.jpg"),
            FileSystem::getPath("resources/textures/skybox/top.jpg"),
            FileSystem::getPath("resources/textures/skybox/bottom.jpg"),
            FileSystem::getPath("resources/textures/skybox/front.jpg"),
            FileSystem::getPath("resources/textures/skybox/back.jpg")
        };
        skybox.init(faces);
        sharkModel.init(MODEL_SHARK_PATH);
        fishModel.init(MODEL_FISH_PATH);

        initFishes();

        camera.MovementSpeed = 5.0f;

        return true;
    }

    void initFishes() {
        srand(static_cast<unsigned int>(time(0)));
        for (int i = 0; i < GENERATE_FISH; ++i) {
            glm::vec3 spawn(
                ((rand() % 10000) / 10000.0f - 0.5f) * 15.0f,
                ((rand() % 10000) / 10000.0f - 0.5f) * 12.0f,
                ((rand() % 10000) / 10000.0f - 0.5f) * 15.0f
            );
            Fish f;
            f.spawnCenter = spawn;
            f.position = spawn;
            f.target = spawn + glm::vec3(
                ((rand() % 10000) / 10000.0f - 0.5f) * 4.0f,
                ((rand() % 10000) / 10000.0f - 0.5f) * 4.0f,
                ((rand() % 10000) / 10000.0f - 0.5f) * 4.0f
            );
            f.speed = 0.5f + ((rand() % 10000) / 10000.0f) * 1.5f;
            fishes.push_back(f);
        }
        fishCount = GENERATE_FISH;
        renderHUD();
    }

    void updateFishes(float deltaTime) {
        for (size_t i = 0; i < fishes.size();) {
            Fish& f = fishes[i];

            glm::vec3 direction = glm::normalize(f.target - f.position);
            f.position += direction * f.speed * deltaTime;

            if (glm::length(f.target - f.position) < 0.1f) {
                f.target = f.spawnCenter + glm::vec3(
                    ((rand() % 10000) / 10000.0f - 0.5f) * 4.0f,
                    ((rand() % 10000) / 10000.0f - 0.5f) * 4.0f,
                    ((rand() % 10000) / 10000.0f - 0.5f) * 4.0f
                );
            }

            float distance = glm::length(camera.Position - f.position);
            if (distance < CATCH_RADIUS) {
                fishes.erase(fishes.begin() + i);
                fishCount--;

                renderHUD();
                continue;
            }
            ++i;
        }
    }

    void renderHUD() {
        std::cout << "\rFish left: " << fishCount << " " << std::flush;
        std::stringstream ss;
        ss << "Hungry_Fish_3D - Fish left: " << fishCount;
        glfwSetWindowTitle(window, ss.str().c_str());

        if (fishCount == 0) {
            glfwSetWindowTitle(window, "Hungry_Fish_3D - You're full. Press Esc to exit.");
            std::cout << "\nYou're full. Press Esc to exit." << std::endl;
            gameOver = true;
        }
    }

    void renderFishes(const glm::mat4& view, const glm::mat4& projection) {
        fishShader->use();

        for (const auto& f : fishes) {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, f.position);

            // Compute direction and yaw (rotation around Y-axis)
            glm::vec3 dir = glm::normalize(f.target - f.position);
            float yaw = atan2(dir.x, dir.z);

            // Compute pitch (tilt up/down based on direction.y)
            float pitch = asin(glm::clamp(dir.y, -1.0f, 1.0f));

            // Apply rotations: yaw (Y-axis) then pitch (X-axis)
            model = glm::rotate(model, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, -pitch, glm::vec3(0.5f, 0.0f, 0.0f));

            // Add a body sway (left-right oscillation)
            float sway = sin(glfwGetTime() * 6.0f + f.position.x * 0.5f) * glm::radians(10.0f);
            model = glm::rotate(model, sway, glm::vec3(0.0f, 1.0f, 0.0f));

            // Slight roll for more natural swimming (Z-axis wobble)
            float roll = sin(glfwGetTime() * 3.0f + f.position.z) * glm::radians(3.0f);
            model = glm::rotate(model, roll, glm::vec3(0.0f, 0.0f, 1.0f));

            // Scale the fish model
            model = glm::scale(model, glm::vec3(0.7f));

            // Set shader uniforms and draw
            fishShader->use();
            fishShader->setMat4("model", model);
            fishShader->setMat4("view", view);
            fishShader->setMat4("projection", projection);

            fishModel.draw(*fishShader);
        }
    }

    void run() {
        while (!glfwWindowShouldClose(window)) {
            float currentFrame = static_cast<float>(glfwGetTime());
            deltaTime = currentFrame - lastFrame;
            lastFrame = currentFrame;

            processInput();
            updateFishes(deltaTime);
            render();
            glfwSwapBuffers(window);
            glfwPollEvents();
        }
        glfwTerminate();
    }

private:
    void render() {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);

        glm::mat4 view = camera.GetViewMatrix();

        skyboxShader->use();
        skybox.draw(*skyboxShader, camera, projection);

        sharkShader->use();
        glm::mat4 modelShark = glm::mat4(1.0f);
        glm::vec3 offset = glm::vec3(0.0f, -0.35f, 0.0f);
        modelShark = glm::translate(modelShark, camera.Position + offset);
        modelShark = glm::rotate(modelShark, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        modelShark = glm::rotate(modelShark, glm::radians(camera.Yaw), glm::vec3(0.0f, -1.0f, 0.0f));
        modelShark = glm::rotate(modelShark, glm::radians(camera.Pitch * 0.7f), glm::vec3(-1.0f, 0.0f, 0.0f));

        float sharkSwimAngle = sin(glfwGetTime() * 2.0f) * glm::radians(6.0f); 
        modelShark = glm::rotate(modelShark, sharkSwimAngle, glm::vec3(0.0f, 1.0f, 0.0f));

        modelShark = glm::scale(modelShark, glm::vec3(0.3f));

        sharkShader->setMat4("model", modelShark);
        sharkShader->setMat4("view", view);
        sharkShader->setMat4("projection", projection);
        sharkModel.draw(*sharkShader);

        renderFishes(view,projection);
    }

    void processInput() {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        if (gameOver) return;

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.ProcessKeyboard(RIGHT, deltaTime);
    }

    // Static Callbacks to redirect to instance methods
    static void framebufferCallback(GLFWwindow* w, int width, int height) {
        glViewport(0, 0, width, height);
    }

    static void mouseCallback(GLFWwindow* w, double xpos, double ypos) {
        auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
        if (!app) return;

        float x = static_cast<float>(xpos);
        float y = static_cast<float>(ypos);

        if (app->firstMouse) {
            app->lastX = x;
            app->lastY = y;
            app->firstMouse = false;
        }

        float xoffset = x - app->lastX;
        float yoffset = app->lastY - y;
        app->lastX = x;
        app->lastY = y;

        app->camera.ProcessMouseMovement(xoffset, yoffset);
    }

    static void scrollCallback(GLFWwindow* w, double xoffset, double yoffset) {
        auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
        if (app)
            app->camera.ProcessMouseScroll(static_cast<float>(yoffset));
    }
};

// ==============================================
// Main Entry
// ==============================================
int main() {
    Application app;
    if (!app.init()) return -1;
    app.run();
    return 0;
}


// ==============================================
// utility function for loading a 2D texture from file
// ==============================================

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

unsigned int loadCubemap(vector<std::string> faces)
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
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
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
