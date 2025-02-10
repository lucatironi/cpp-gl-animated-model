#pragma once

#include "mesh.hpp"
#include "shader.hpp"

#include <vector>

class BasicModel
{
public:
    virtual void Draw(const Shader& shader) const = 0;

protected:
    std::vector<Mesh> meshes;
};
