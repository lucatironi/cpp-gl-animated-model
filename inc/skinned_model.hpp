#pragma once

#include "basic_model.hpp"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static inline glm::mat4 GetGLMMat4(const aiMatrix4x4& from)
{
    glm::mat4 to;
    // the a,b,c,d in assimp is the row; the 1,2,3,4 is the column
    to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
    to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
    to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
    to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
    return to;
}

static inline glm::vec3 GetGLMVec3(const aiVector3D& vec) { return glm::vec3(vec.x, vec.y, vec.z); }
static inline glm::quat GetGLMQuat(const aiQuaternion& qat) { return glm::quat(qat.w, qat.x, qat.y, qat.z); }

class SkinnedModel : public BasicModel
{
public:
    SkinnedModel(const std::string& path)
        : animDuration(0.0f), currentAnimation(0), bonesCount(0)
    {
        loadModel(path);
    }

    ~SkinnedModel()
    {
        scene = nullptr;
    }

    void Draw(const Shader& shader) const override
    {
        for (const auto& mesh : meshes)
            mesh.Draw(shader);
    }

    void TextureOverride(const std::string& texturePath)
    {
        for (auto& mesh : meshes)
            mesh.AddTexture({ Texture2D(texturePath), "texture_diffuse", texturePath });
    }

    void SetCurrentAnimation(unsigned int animation)
    {
        if (hasAnimations && animation >= 0 && animation < numAnimations)
        {
            currentAnimation = animation;
            setAnimParams();
        }
    }

    void SetBoneTransformations(Shader shader, float currentTime)
    {
        if (hasAnimations)
        {
            std::vector<glm::mat4> transforms;
            boneTransform(currentTime, transforms);
            shader.Use();
            shader.SetBool("animated", hasAnimations);
            shader.SetMat4v("finalBonesMatrices", transforms);
        }
    }

    void Debug()
    {
        BasicModel::Debug();

        std::cout << "Skinned Model: \"" << path
                  << "\", hasAnimations: " << (hasAnimations ? "yes" : "no")
                  << ", numAnimations: " << numAnimations
                  << ", bonesCount: " << bonesCount
                  << ", meshes: " << meshes.size()
                  << std::endl;

        for (const auto& mesh : meshes)
            for (const auto& texture : mesh.GetTextures())
                std::cout << "Texture: " << texture.path << ", type: " << texture.type << std::endl;

        for (unsigned int i = 0; i < numAnimations; ++i)
        {
            aiAnimation* animation = scene->mAnimations[i];
            std::cout << "Animation: " << animation->mName.C_Str()
                      << ", Duration: " << animation->mDuration
                      << ", TicksPerSecond: " << animation->mTicksPerSecond  << std::endl;
        }
    }

    bool HasAnimations() { return hasAnimations; }
    unsigned int GetNumAnimations() { return numAnimations; }

private:
    #define MAX_BONE_INFLUENCE 4

    struct BoneMatrix
    {
        glm::mat4 BoneOffset;
        glm::mat4 FinalTransformation;

        BoneMatrix()
        {
            BoneOffset = glm::mat4(0.0f);
            FinalTransformation = glm::mat4(0.0f);
        }
    };

    std::string path;
    Assimp::Importer importer;
    const aiScene* scene;
    std::string directory;
    std::vector<Texture> loadedTextures;
    std::map<std::string, unsigned int> boneMapping;
    std::vector<BoneMatrix> boneMatrices;
    unsigned int bonesCount;
    glm::mat4 globalInverseTransform;
    bool hasAnimations;
    unsigned int numAnimations;
    unsigned int currentAnimation;
    float ticksPerSecond;
    float animDuration;

    void loadModel(const std::string& modelPath)
    {
        const aiScene* pScene = importer.ReadFile(modelPath,
            aiProcessPreset_TargetRealtime_Fast | aiProcess_LimitBoneWeights | aiProcess_FlipUVs);

        if (!pScene || (pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !pScene->mRootNode)
            throw std::runtime_error("ERROR::ASSIMP: " + std::string(importer.GetErrorString()));

        path = modelPath;
        scene = pScene;
        directory = path.substr(0, path.find_last_of("/"));
        hasAnimations = scene->HasAnimations();
        numAnimations = scene->mNumAnimations;
        globalInverseTransform = glm::inverse(GetGLMMat4(scene->mRootNode->mTransformation));
        boneMatrices.reserve(100);
        processNode(scene->mRootNode);
        setAnimParams();
    }

    // Process a node recursively and convert it to meshes
    void processNode(aiNode* node)
    {
        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
            AddMesh(processMesh(scene->mMeshes[node->mMeshes[i]]));

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i]);
    }

    Mesh processMesh(const aiMesh* mesh)
    {
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;
        std::vector<Texture> textures;

        // Process vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            vertices.emplace_back(Vertex{
                .Position    = GetGLMVec3(mesh->mVertices[i]),
                .Normal      = GetGLMVec3(mesh->mNormals[i]),
                .TexCoords   = mesh->mTextureCoords[0]
                                ? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
                                : glm::vec2(0.0f, 0.0f),
                .BoneIDs     = glm::ivec4(-1),
                .BoneWeights = glm::vec4(0.0f) // Bone Weights are initialised in the next for loop
            });
        }

        // Process bone weights
        for (unsigned int i = 0; i < mesh->mNumBones; ++i)
        {
            unsigned int boneIndex = 0;
            std::string boneName(mesh->mBones[i]->mName.data);

            if (boneMapping.find(boneName) == boneMapping.end())
            {
                // allocate an index for the new bone
                boneIndex = bonesCount;
                BoneMatrix boneMatrix;
                boneMatrices.push_back(boneMatrix);

                boneMatrices[boneIndex].BoneOffset = GetGLMMat4(mesh->mBones[i]->mOffsetMatrix);
                boneMapping[boneName] = boneIndex;
                bonesCount++;
            }
            else
                boneIndex = boneMapping[boneName];

            for (unsigned int j = 0; j < mesh->mBones[i]->mNumWeights; ++j)
            {
                unsigned int vertexID = mesh->mBones[i]->mWeights[j].mVertexId;
                float weight = mesh->mBones[i]->mWeights[j].mWeight;

                for (unsigned int g = 0; g < MAX_BONE_INFLUENCE; ++g)
                {
                    if (vertices[vertexID].BoneWeights[g] == 0.0f)
                    {
                        vertices[vertexID].BoneIDs[g] = boneIndex;
                        vertices[vertexID].BoneWeights[g] = weight;
                        break;
                    }
                }
            }
        }

        // Process indices
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
        {
            const aiFace& face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
                indices.emplace_back(face.mIndices[j]);
        }

        // Process materials
        if (mesh->mMaterialIndex >= 0)
        {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

            auto diffuseMaps = loadMaterialTextures(scene, material, aiTextureType_DIFFUSE, "texture_diffuse");
            textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
            auto specularMaps = loadMaterialTextures(scene, material, aiTextureType_SPECULAR, "texture_specular");
            textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
            auto normalMaps = loadMaterialTextures(scene, material, aiTextureType_HEIGHT, "texture_normal");
            textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        }

        return Mesh(std::move(vertices), std::move(indices), std::move(textures));
    }

    void boneTransform(float timeInSeconds, std::vector<glm::mat4>& transforms)
    {
        float timeInTicks = timeInSeconds * ticksPerSecond;
        float animationTimeTicks = fmod(timeInTicks, animDuration);
        readNodeHeirarchy(animationTimeTicks, scene->mRootNode, glm::mat4(1.0f));
        transforms.resize(bonesCount);

        for (unsigned int i = 0; i < bonesCount; ++i)
            transforms[i] = boneMatrices[i].FinalTransformation;
    }

    unsigned int findPosition(float animationTime, const aiNodeAnim* nodeAnim)
    {
        assert(nodeAnim->mNumPositionKeys > 0);
        for (unsigned int index = 0; index < nodeAnim->mNumPositionKeys - 1; ++index)
            if (animationTime < (float)nodeAnim->mPositionKeys[index + 1].mTime)
                return index;
        std::cerr << "Position animationTime is out of bound: " << animationTime << std::endl;
        return 0;
    }

    unsigned int findRotation(float animationTime, const aiNodeAnim* nodeAnim)
    {
        assert(nodeAnim->mNumRotationKeys > 0);
        for (unsigned int index = 0; index < nodeAnim->mNumRotationKeys - 1; ++index)
            if (animationTime < (float)nodeAnim->mRotationKeys[index + 1].mTime)
                return index;
        std::cerr << "Rotation animationTime is out of bound: " << animationTime << std::endl;
        return 0;
    }

    unsigned int findScaling(float animationTime, const aiNodeAnim* nodeAnim)
    {
        assert(nodeAnim->mNumScalingKeys > 0);
        for (unsigned int index = 0; index < nodeAnim->mNumScalingKeys - 1; ++index)
            if (animationTime < (float)nodeAnim->mScalingKeys[index + 1].mTime)
                return index;
        std::cerr << "Scaling animationTime is out of bound: " << animationTime << std::endl;
        return 0;
    }

    void calcInterpolatedPosition(aiVector3D& out, float animationTime, const aiNodeAnim* nodeAnim)
    {
        if (nodeAnim->mNumPositionKeys == 1)
        {
            out = nodeAnim->mPositionKeys[0].mValue;
            return;
        }

        unsigned int positionIndex = findPosition(animationTime, nodeAnim);
        unsigned int nextPositionIndex = (positionIndex + 1);
        assert(nextPositionIndex < nodeAnim->mNumPositionKeys);
        float deltaTime = (float)(nodeAnim->mPositionKeys[nextPositionIndex].mTime - nodeAnim->mPositionKeys[positionIndex].mTime);
        float factor = (animationTime - (float)nodeAnim->mPositionKeys[positionIndex].mTime) / deltaTime;
        if (factor < 0.0f && factor > 1.0f)
            std::cerr << "Factor is out of bound: " << factor << std::endl;
        const aiVector3D& startPosition = nodeAnim->mPositionKeys[positionIndex].mValue;
        const aiVector3D& endPosition   = nodeAnim->mPositionKeys[nextPositionIndex].mValue;
        aiVector3D delta = endPosition - startPosition;
        out = startPosition + factor * delta;
    }

    void calcInterpolatedRotation(aiQuaternion& out, float animationTime, const aiNodeAnim* nodeAnim)
    {
        if (nodeAnim->mNumRotationKeys == 1)
        {
            out = nodeAnim->mRotationKeys[0].mValue;
            return;
        }

        unsigned int rotationIndex = findRotation(animationTime, nodeAnim);
        unsigned int nextRotationIndex = (rotationIndex + 1);
        assert(nextRotationIndex < nodeAnim->mNumRotationKeys);
        float deltaTime = (float)(nodeAnim->mRotationKeys[nextRotationIndex].mTime - nodeAnim->mRotationKeys[rotationIndex].mTime);
        float factor = (animationTime - (float)nodeAnim->mRotationKeys[rotationIndex].mTime) / deltaTime;
        if (factor < 0.0f && factor > 1.0f)
            std::cerr << "Factor is out of bound: " << factor << std::endl;
        const aiQuaternion& startRotationQ = nodeAnim->mRotationKeys[rotationIndex].mValue;
        const aiQuaternion& endRotationQ   = nodeAnim->mRotationKeys[nextRotationIndex].mValue;
        aiQuaternion::Interpolate(out, startRotationQ, endRotationQ, factor);
        out = out.Normalize();
    }

    void calcInterpolatedScaling(aiVector3D& out, float animationTime, const aiNodeAnim* nodeAnim)
    {
        if (nodeAnim->mNumScalingKeys == 1)
        {
            out = nodeAnim->mScalingKeys[0].mValue;
            return;
        }

        unsigned int scalingIndex = findScaling(animationTime, nodeAnim);
        unsigned int nextScalingIndex = (scalingIndex + 1);
        assert(nextScalingIndex < nodeAnim->mNumScalingKeys);
        float deltaTime = (float)(nodeAnim->mScalingKeys[nextScalingIndex].mTime - nodeAnim->mScalingKeys[scalingIndex].mTime);
        float factor = (animationTime - (float)nodeAnim->mScalingKeys[scalingIndex].mTime) / deltaTime;
        if (factor < 0.0f && factor > 1.0f)
            std::cerr << "Factor is out of bound: " << factor << std::endl;
        const aiVector3D& startScaling = nodeAnim->mScalingKeys[scalingIndex].mValue;
        const aiVector3D& endScaling   = nodeAnim->mScalingKeys[nextScalingIndex].mValue;
        aiVector3D delta = endScaling - startScaling;
        out = startScaling + factor * delta;
    }

    void readNodeHeirarchy(float animationTime, const aiNode* node, const glm::mat4& parentTransform)
    {
        std::string nodeName(node->mName.data);
        glm::mat4 nodeTransformation = GetGLMMat4(node->mTransformation);

        const aiAnimation* animation = scene->mAnimations[currentAnimation];
        const aiNodeAnim* nodeAnim = findNodeAnim(animation, nodeName);

        if (nodeAnim)
        {
            aiVector3D scalingV;
            calcInterpolatedScaling(scalingV, animationTime, nodeAnim);
            glm::mat4 scalingM = glm::scale(glm::mat4(1.0f), GetGLMVec3(scalingV));

            aiQuaternion rotationQ;
            calcInterpolatedRotation(rotationQ, animationTime, nodeAnim);
            glm::mat4 rotationM = glm::toMat4(GetGLMQuat(rotationQ));

            aiVector3D translationV;
            calcInterpolatedPosition(translationV, animationTime, nodeAnim);
            glm::mat4 translationM = glm::translate(glm::mat4(1.0f), GetGLMVec3(translationV));

            nodeTransformation = translationM * rotationM * scalingM;
        }

        glm::mat4 globalTransformation = parentTransform * nodeTransformation;

        if (boneMapping.find(nodeName) != boneMapping.end())
        {
            unsigned int boneIndex = boneMapping[nodeName];
            boneMatrices[boneIndex].FinalTransformation = globalInverseTransform * globalTransformation * boneMatrices[boneIndex].BoneOffset;
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            readNodeHeirarchy(animationTime, node->mChildren[i], globalTransformation);
    }

    const aiNodeAnim* findNodeAnim(const aiAnimation* animation, const std::string nodeName)
    {
        for (unsigned int i = 0; i < animation->mNumChannels; ++i)
        {
            const aiNodeAnim* nodeAnim = animation->mChannels[i];
            if (std::string(nodeAnim->mNodeName.data) == nodeName)
                return nodeAnim;
        }

        return nullptr;
    }

    void setAnimParams()
    {
        ticksPerSecond = (float)(scene->mAnimations[currentAnimation]->mTicksPerSecond != 0.0
                               ? scene->mAnimations[currentAnimation]->mTicksPerSecond : 25.0f);
        animDuration = (float)scene->mAnimations[currentAnimation]->mDuration;
    }

    // Load material textures from Assimp
    std::vector<Texture> loadMaterialTextures(const aiScene* scene, aiMaterial* material, aiTextureType textureType, const std::string& typeName)
    {
        std::vector<Texture> textures;
        for (unsigned int i = 0; i < material->GetTextureCount(textureType); ++i)
        {
            aiString textureFilename;
            material->GetTexture(textureType, i, &textureFilename);

            // check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
            bool skip = false;
            for (unsigned int j = 0; j < loadedTextures.size(); ++j)
            {
                if (std::strcmp(loadedTextures[j].path.data(), textureFilename.C_Str()) == 0)
                {
                    textures.emplace_back(loadedTextures[j]);
                    skip = true; // a texture with the same filepath has already been loaded, continue to next one. (optimization)
                    break;
                }
            }

            if (!skip) // if texture hasn't been loaded already, load it
            {
                Texture2D texture2D;
                if (auto texture = scene->GetEmbeddedTexture(textureFilename.C_Str()))
                    texture2D = Texture2D(reinterpret_cast<unsigned char*>(texture->pcData), texture->mWidth, texture->mHeight);
                else
                    texture2D = Texture2D(std::string(directory + "/" + std::string(textureFilename.C_Str())));

                Texture tex = { texture2D, typeName, std::string(textureFilename.C_Str()) };
                textures.push_back(tex);
                loadedTextures.push_back(tex);
            }
        }
        return textures;
    }

    std::string getTextureFilename(const aiTexture* texture) const
    {
        std::string filename = texture->mFilename.C_Str();
        size_t lastSlash = filename.rfind("/");
        if (lastSlash != std::string::npos)
            filename = filename.substr(lastSlash + 1);

        return filename;
    }
};
