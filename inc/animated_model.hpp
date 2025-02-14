#pragma once

#include "assimp/matrix4x4.h"
#include "basic_model.hpp"

#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_float.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/maths/transform.h"
#include "ozz/base/memory/allocator.h"
#include "ozz/base/memory/unique_ptr.h"
#include "shader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <map>
#include <memory>
#include <vector>
#include <string>
#include <iostream>

using RuntimeSkeleton = ozz::unique_ptr<ozz::animation::Skeleton>;
using RuntimeAnimation = ozz::unique_ptr<ozz::animation::Animation>;

struct Joint
{
    std::string name;
    int parentIndex;
    ozz::math::Transform localTransform;
    glm::mat4 invBindPose;
};

static inline glm::mat4 OzzToGlmMat4(const ozz::math::Float4x4& m) {
    glm::mat4 result;
    memcpy(glm::value_ptr(result), &m.cols[0], sizeof(glm::mat4));
    return result;
}

class AnimatedModel : public BasicModel
{
public:
    AnimatedModel() {}

    void Draw(const Shader& shader) const override
    {
        for (const auto& mesh : meshes)
            mesh.Draw(shader);
    }

    void SetJoints(std::vector<Joint>& j) { joints = j; }

    void SetSkeleton(const ozz::animation::offline::RawSkeleton& rawSkeleton)
    {
        ozz::animation::offline::SkeletonBuilder skelBuilder;
        skeleton = skelBuilder(rawSkeleton);
        numJoints = skeleton->num_joints();
        jointMatrices.resize(numJoints);
    }

    void AddAnimation(const ozz::animation::offline::RawAnimation& animation)
    {
        animationsMap[std::string(animation.name)] = animations.size();

        ozz::animation::offline::AnimationBuilder animBuilder;
        animations.emplace_back(animBuilder(animation));
    }

    void SampleAnimation(float deltaTime, const ozz::animation::Animation& animation, ozz::animation::Skeleton& skeleton)
    {
        static float animationTime = 0.0f;
        animationTime += deltaTime; // Advance animation time

        // Wrap around animation time if it exceeds duration
        if (animationTime > animation.duration())
            animationTime = fmod(animationTime, animation.duration());

        // Step 1: Sample animation
        std::vector<ozz::math::SoaTransform> localTransforms(numJoints);
        ozz::animation::SamplingJob samplingJob;
        samplingJob.animation = &animation;
        samplingJob.context = &context;
        samplingJob.ratio = animationTime / animation.duration();
        samplingJob.output = ozz::make_span(localTransforms);

        if (!samplingJob.Run())
        {
            std::cerr << "Failed to sample animation" << std::endl;
            return;
        }

        // Step 2: Convert to model space (world transform)
        std::vector<ozz::math::Float4x4> modelSpaceTransforms(numJoints);

        ozz::animation::LocalToModelJob localToModelJob;
        localToModelJob.skeleton = &skeleton;
        localToModelJob.input = ozz::make_span(localTransforms);
        localToModelJob.output = ozz::make_span(modelSpaceTransforms);

        if (!localToModelJob.Run())
        {
            std::cerr << "Failed to convert local to model transforms" << std::endl;
            return;
        }

        // Step 3: Convert to glm::mat4 for GPU
        for (size_t i = 0; i < modelSpaceTransforms.size(); ++i)
            jointMatrices[i] = OzzToGlmMat4(modelSpaceTransforms[i]) * joints[i].invBindPose;
    }

    void UpdateAnimation(float deltaTime)
    {
        if (!animations.empty() && skeleton)
            SampleAnimation(deltaTime, *animations[currentAnimation], *skeleton);
    }

    void SetBoneTransformations(const Shader& shader)
    {
        shader.Use();
        shader.SetBool("animated", true);
        shader.SetMat4v("finalBonesMatrices", jointMatrices);
    }

    void SetCurrentAnimation(const std::string& animName)
    {
        for (auto it = animationsMap.begin(); it != animationsMap.end(); ++it)
            if (it->first == animName)
                currentAnimation = animationsMap[it->first];
        context.Resize(animations[currentAnimation]->num_tracks());
    }

    void SetCurrentAnimation(const unsigned int index)
    {
        if (index < animationsMap.size())
        {
            for (auto it = animationsMap.begin(); it != animationsMap.end(); ++it)
                if (it->second == index)
                    currentAnimation = animationsMap[it->first];
            context.Resize(animations[currentAnimation]->num_tracks());
        }
    }

    const bool HasAnimations() { return !animations.empty(); }
    const unsigned int GetNumAnimations() { return animations.size(); }
    std::map<std::string, unsigned int>& GetAnimationList() { return animationsMap; }

    void Debug()
    {
        std::cout << "Animated Model: "
                  << ", hasAnimations: " << (HasAnimations() ? "yes" : "no")
                  << ", numAnimations: " << GetNumAnimations()
                  << ", bonesCount: " << numJoints
                  << ", meshes: " << meshes.size()
                  << std::endl;

        BasicModel::Debug();

        for (const auto& animation : animations)
            std::cout << "Animation: " << animation->name()
                      << ", Duration: " << animation->duration()  << std::endl;
    }


private:
    RuntimeSkeleton skeleton;
    std::vector<Joint> joints;
    unsigned int numJoints;
    std::vector<RuntimeAnimation> animations;
    std::map<std::string, unsigned int> animationsMap;
    ozz::animation::SamplingJob::Context context;
    unsigned int currentAnimation;
    std::vector<glm::mat4> jointMatrices;
};

namespace GLTFLoader
{
    constexpr unsigned int MAX_BONE_INFLUENCE = 4;
    static std::string Directory;
    static std::vector<Texture> CachedTextures;

    static inline glm::mat4 AiToGlmMat4(const aiMatrix4x4& from)
    {
        glm::mat4 to;
        // the a,b,c,d in assimp is the row; the 1,2,3,4 is the column
        to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
        to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
        to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
        to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
        return to;
    }
    static inline glm::vec3 AiToGlmVec3(const aiVector3D& vec) { return glm::vec3(vec.x, vec.y, vec.z); }
    static inline glm::quat AiToGlmQuat(const aiQuaternion& qat) { return glm::quat(qat.w, qat.x, qat.y, qat.z); }

    static inline ozz::math::Transform AiToOzzTransform(const aiMatrix4x4& aiTransform)
    {
        aiVector3D scale, translation;
        aiQuaternion rotation;
        aiTransform.Decompose(scale, rotation, translation);

        ozz::math::Transform ozzTransform = {
            ozz::math::Float3(translation.x, translation.y, translation.z),
            ozz::math::Quaternion(rotation.x, rotation.y, rotation.z, rotation.w),
            ozz::math::Float3(scale.x, scale.y, scale.z)
        };
        return ozzTransform;
    }

    static std::vector<Texture> ExtractTextures(const aiScene* scene, aiMaterial* material, aiTextureType textureType, const std::string& typeName)
    {
        std::vector<Texture> textures;
        for (unsigned int i = 0; i < material->GetTextureCount(textureType); ++i)
        {
            aiString textureFilename;
            material->GetTexture(textureType, i, &textureFilename);

            // check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
            bool skip = false;
            for (unsigned int j = 0; j < CachedTextures.size(); ++j)
            {
                if (std::strcmp(CachedTextures[j].path.data(), textureFilename.C_Str()) == 0)
                {
                    textures.emplace_back(CachedTextures[j]);
                    skip = true; // a texture with the same filepath has already been loaded, continue to next one. (optimization)
                    break;
                }
            }

            if (!skip) // if texture hasn't been loaded already, load it
            {
                Texture2D texture2D;
                if (const auto& texture = scene->GetEmbeddedTexture(textureFilename.C_Str()))
                    texture2D = Texture2D(reinterpret_cast<unsigned char*>(texture->pcData), texture->mWidth, texture->mHeight);
                else
                    texture2D = Texture2D(std::string(Directory + "/" + std::string(textureFilename.C_Str())));

                Texture tex = { texture2D, typeName, std::string(textureFilename.C_Str()) };
                textures.push_back(tex);
                CachedTextures.push_back(tex);
            }
        }
        return textures;
    }

    static void ExtractJoints(const aiNode* node, int parentIndex, std::vector<Joint>& joints, std::map<std::string, int>& boneMap)
    {
        int jointIndex = 0;
        std::string boneName(node->mName.data);
        if (boneMap.find(boneName) == boneMap.end())
        {
            jointIndex = static_cast<int>(boneMap.size());
            boneMap[boneName] = jointIndex;
        }
        else
            jointIndex = boneMap[boneName];

        // Store joint
        joints.push_back({
            .name           = boneName,
            .parentIndex    = parentIndex,
            .localTransform = AiToOzzTransform(node->mTransformation),
            .invBindPose    = glm::mat4(1.0f)
        });

        // Recursively process child nodes
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            ExtractJoints(node->mChildren[i], jointIndex, joints, boneMap);
    }

    static bool ExtractSkeleton(const aiScene* pScene, std::vector<Joint>& joints, std::map<std::string, int>& boneMap, AnimatedModel& model)
    {
        // Extract joints from gltfModel and populate model.joints
        ExtractJoints(pScene->mRootNode, -1, joints, boneMap);

        if(joints.empty())
        {
            std::cerr << "Failed to extract joints" << std::endl;
            return false;
        }

        ozz::animation::offline::RawSkeleton rawSkeleton;
        rawSkeleton.roots.resize(1); // Root joint

        std::function<void(int, ozz::animation::offline::RawSkeleton::Joint&)> buildHierarchy =
            [&](int jointIndex, ozz::animation::offline::RawSkeleton::Joint& outJoint)
            {
                const Joint& joint = joints[jointIndex];
                outJoint.name = joint.name;
                outJoint.transform = joint.localTransform;

                // Add children
                for (size_t i = 0; i < joints.size(); ++i)
                {
                    if (joints[i].parentIndex == jointIndex)
                    {
                        outJoint.children.emplace_back();
                        buildHierarchy(i, outJoint.children.back());
                    }
                }
            };

        // Build hierarchy starting from root joints
        for (size_t i = 0; i < joints.size(); ++i)
            if (joints[i].parentIndex == -1) // Root joints
                buildHierarchy(i, rawSkeleton.roots.back());

        if (!rawSkeleton.Validate())
        {
            std::cerr <<  "Failed to validate Ozz Skeleton" << std::endl;
            return false;
        }

        model.SetSkeleton(rawSkeleton);

        return true;
    }

    static bool ExtractAnimations(const aiScene* scene, std::vector<Joint>& joints, const std::map<std::string, int>& boneMap, AnimatedModel& model)
    {
        if (!scene->HasAnimations())
        {
            std::cerr << "No animations found in this model" << std::endl;
            return false;
        }

        for (unsigned int animIndex = 0; animIndex < scene->mNumAnimations; ++animIndex)
        {
            aiAnimation* aiAnim = scene->mAnimations[animIndex];
            ozz::animation::offline::RawAnimation rawAnimation;
            rawAnimation.duration = static_cast<float>(aiAnim->mDuration / aiAnim->mTicksPerSecond);
            rawAnimation.tracks.resize(boneMap.size());
            rawAnimation.name = aiAnim->mName.C_Str();

            for (unsigned int channelIndex = 0; channelIndex < aiAnim->mNumChannels; ++channelIndex)
            {
                aiNodeAnim* channel = aiAnim->mChannels[channelIndex];
                auto it = boneMap.find(channel->mNodeName.C_Str());

                if (it == boneMap.end())
                    continue; // Skip non-skeletal nodes

                int jointIndex = it->second;
                auto& track = rawAnimation.tracks[jointIndex];

                // Translation Keys
                for (unsigned int i = 0; i < channel->mNumPositionKeys; ++i)
                {
                    const aiVectorKey& key = channel->mPositionKeys[i];
                    track.translations.push_back({
                        static_cast<float>(key.mTime / aiAnim->mTicksPerSecond),
                        ozz::math::Float3(key.mValue.x, key.mValue.y, key.mValue.z)
                    });
                }

                // Rotation Keys
                for (unsigned int i = 0; i < channel->mNumRotationKeys; ++i)
                {
                    const aiQuatKey& key = channel->mRotationKeys[i];
                    track.rotations.push_back({
                        static_cast<float>(key.mTime / aiAnim->mTicksPerSecond),
                        ozz::math::Quaternion(key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w)
                    });
                }

                // Scale Keys
                for (unsigned int i = 0; i < channel->mNumScalingKeys; ++i)
                {
                    const aiVectorKey& key = channel->mScalingKeys[i];
                    track.scales.push_back({
                        static_cast<float>(key.mTime / aiAnim->mTicksPerSecond),
                        ozz::math::Float3(key.mValue.x, key.mValue.y, key.mValue.z)
                    });
                }
            }

            if (!rawAnimation.Validate())
            {
                std::cerr << "Failed to validate Ozz Animation: " << aiAnim->mName.C_Str() << std::endl;
                continue;
            }

            model.AddAnimation(rawAnimation);
        }

        return true;
    }

    static bool ExtractMeshes(const aiScene* scene, std::vector<Joint>& joints, std::map<std::string, int>& boneMap, AnimatedModel& model)
    {
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            const aiMesh* mesh = scene->mMeshes[i];

            std::vector<Vertex> vertices;
            std::vector<GLuint> indices;
            std::vector<Texture> textures;

            // Process vertices
            for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
            {
                vertices.emplace_back(Vertex{
                    .Position    = AiToGlmVec3(mesh->mVertices[i]),
                    .Normal      = AiToGlmVec3(mesh->mNormals[i]),
                    .TexCoords   = mesh->mTextureCoords[0]
                                    ? glm::vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y)
                                    : glm::vec2(0.0f, 0.0f),
                    .BoneIDs     = glm::ivec4(-1),
                    .BoneWeights = glm::vec4(0.0f) // Bone Weights are initialised in another loop
                });
            }

            // Process bones
            for (unsigned int i = 0; i < mesh->mNumBones; ++i)
            {
                aiBone* bone = mesh->mBones[i];

                int boneID = 0;
                std::string boneName(bone->mName.data);
                if (boneMap.find(boneName) != boneMap.end())
                    boneID = boneMap[boneName];
                else
                {
                    std::cerr << "Found missing joint \"" << boneName << std::endl;
                    return false;
                }

                joints[boneID].invBindPose = AiToGlmMat4(bone->mOffsetMatrix);

                for (unsigned int j = 0; j < bone->mNumWeights; ++j)
                {
                    unsigned int vertexID = bone->mWeights[j].mVertexId;
                    float weight = bone->mWeights[j].mWeight;

                    for (unsigned int g = 0; g < MAX_BONE_INFLUENCE; ++g)
                    {
                        if (vertices[vertexID].BoneWeights[g] == 0.0f)
                        {
                            vertices[vertexID].BoneIDs[g] = boneID;
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

            // Process textures
            if (mesh->mMaterialIndex >= 0)
            {
                aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

                auto diffuseTextures = ExtractTextures(scene, material, aiTextureType_DIFFUSE, "texture_diffuse");
                textures.insert(textures.end(), diffuseTextures.begin(), diffuseTextures.end());
                auto specularTextures = ExtractTextures(scene, material, aiTextureType_SPECULAR, "texture_specular");
                textures.insert(textures.end(), specularTextures.begin(), specularTextures.end());
                auto normalTextures = ExtractTextures(scene, material, aiTextureType_HEIGHT, "texture_normal");
                textures.insert(textures.end(), normalTextures.begin(), normalTextures.end());
            }

            model.AddMesh(Mesh(std::move(vertices), std::move(indices), std::move(textures)));
            model.SetJoints(joints);
        }

        return true;
    }

    static bool LoadFromGLTF(const std::string& path, AnimatedModel& model)
    {
        Assimp::Importer importer;
        importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, 0.01f);
        const aiScene* pScene = importer.ReadFile(path,
            aiProcessPreset_TargetRealtime_Fast | aiProcess_GlobalScale | aiProcess_LimitBoneWeights | aiProcess_FlipUVs);

        if (!pScene || (pScene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !pScene->mRootNode)
            throw std::runtime_error("ERROR::ASSIMP: " + std::string(importer.GetErrorString()));

        Directory = path.substr(0, path.find_last_of("/"));

        std::vector<Joint> joints;
        std::map<std::string, int> boneMap;

        // Extract skeleton from gltfModel and set model.skeleton
        if(!ExtractSkeleton(pScene, joints, boneMap, model))
        {
            std::cerr << "Error extracting skeleton from model \"" << path << "\"" << std::endl;
            return false;
        }
        // Extract animations from gltfModel and populate model.animations
        if(!ExtractAnimations(pScene, joints, boneMap, model))
        {
            std::cerr << "Error extracting animations from model \"" << path << "\"" << std::endl;
            return false;
        }
        // Extract meshes from gltfModel and populate model.meshes
        if(!ExtractMeshes(pScene, joints, boneMap, model))
        {
            std::cerr << "Error extracting meshes from model \"" << path << "\"" << std::endl;
            return false;
        }

        return true;
    }
};