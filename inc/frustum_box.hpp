#pragma once

#include "shader.hpp"

#include <vector>

class FrustumBox
{
public:
    FrustumBox(const std::vector<glm::vec3>& corners, const glm::vec3& color)
        : corners(corners), color(color)
    {
       loadMesh();
    }

    void Draw(const Shader& shader) const
    {
        shader.Use();
        shader.SetVec3("color", color);
        glBindVertexArray(VAO);
        glDrawElements(GL_LINES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

private:
    GLuint VAO, VBO, EBO = 0;
    std::vector<GLuint> indices;
    std::vector<glm::vec3> corners;
    glm::vec3 color;

    void loadMesh()
    {
        /*-----------------/
              6------7
             /|     /|
            2------3 |
            | 4----|-5
            |/     |/
            0------1
        /-----------------*/

        indices = {
            0, 1, 1, 3, 3, 2, 2, 0, // Near plane CCW
            4, 5, 5, 7, 7, 6, 6, 4, // Far plane CCW
            0, 4, 1, 5, 2, 6, 3, 7  // Connect near and far planes
        };

        // Create VAO/VBO
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, corners.size() * sizeof(glm::vec3), corners.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
        glEnableVertexAttribArray(0);
    }
};