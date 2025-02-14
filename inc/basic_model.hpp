#pragma once

#include "mesh.hpp"
#include "shader.hpp"

#include <vector>

class BasicModel
{
public:
    virtual void Draw(const Shader& shader) const = 0;

    void AddMesh(const Mesh& mesh)
    {
        meshes.emplace_back(mesh);
    }

    void Debug() const
    {
        std::cout << "Meshes:" << std::endl;
        for (const auto& mesh : meshes)
            mesh.Debug();
    }

protected:
    std::vector<Mesh> meshes;
};
