#pragma once

#include "mesh.hpp"
#include "shader.hpp"

#include <string>
#include <vector>

class PlaneModel : public Model
{
public:
    PlaneModel(const std::string& texturePath, float size = 1.0f)
        : size(size)
    {
       createMesh(texturePath);
    }

    void Draw(const Shader& shader) const override
    {
        shader.Use();
        shader.SetBool("animated", false);
        mesh->Draw(shader);
    }

private:
    float size;
    std::unique_ptr<Mesh> mesh;

    void createMesh(const std::string& texturePath)
    {
        float halfSize = size / 2.0f;
        std::vector<Vertex> vertices = {
            // positions                      normals               texCoords
            { { -halfSize, 0.0f,  halfSize }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
            { {  halfSize, 0.0f,  halfSize }, { 0.0f, 1.0f, 0.0f }, { size, 0.0f } },
            { {  halfSize, 0.0f, -halfSize }, { 0.0f, 1.0f, 0.0f }, { size, size } },
            { { -halfSize, 0.0f, -halfSize }, { 0.0f, 1.0f, 0.0f }, { 0.0f, size } }
        };
        std::vector<GLuint> indices = { 0, 1, 2, 2, 3, 0 };

        // Texture setup
        std::vector<Texture> textures = {
            { Texture2D(texturePath, { .wrapS = GL_REPEAT, .wrapT = GL_REPEAT }), "texture_diffuse", texturePath }
        };

        // Create the mesh
        mesh = std::make_unique<Mesh>(vertices, indices, textures);
    }
};