#pragma once

#include "basic_model.hpp"
#include "shader.hpp"

#include "glm/ext/matrix_transform.hpp"
#include "glm/fwd.hpp"
#include "glm/gtc/quaternion.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/maths/transform.h"
#include "ozz/base/memory/unique_ptr.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>

using RuntimeSkeleton = ozz::unique_ptr<ozz::animation::Skeleton>;
using RuntimeAnimation = ozz::unique_ptr<ozz::animation::Animation>;

struct Joint
{
    std::string name;
    int parentIndex;
    ozz::math::Transform localTransform;
    glm::mat4 invBindPose;
};

static inline glm::mat4 OzzFloat4x4ToGlmMat4(const ozz::math::Float4x4& from) {
    glm::mat4 to;
    memcpy(glm::value_ptr(to), &from.cols[0], sizeof(glm::mat4));
    return to;
}

static inline glm::mat4 OzzTransformToGlmMat4(const ozz::math::Transform& from) {
    glm::mat4 translation, rotation, scale;
    translation = glm::translate(glm::mat4(1.0f), glm::vec3(from.translation.x, from.translation.y, from.translation.z));
    rotation = glm::mat4_cast(glm::quat(from.rotation.w, from.rotation.x, from.rotation.y, from.rotation.z));
    scale = glm::scale(glm::mat4(1.0f), glm::vec3(from.scale.x, from.scale.y, from.scale.z));
    return translation * rotation * scale;
}

class AnimatedModel : public BasicModel
{
public:
    AnimatedModel() : numJoints(0), currentAnimation(0), animationTime(0.0f) {}
    ~AnimatedModel() = default;

    void Draw(const Shader& shader) const override
    {
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            shader.SetBool("multiMesh", true);
            shader.SetMat4("meshModel", OzzTransformToGlmMat4(joints[i+1].localTransform));
            meshes[i].Draw(shader);
        }
    }

    void SetJoints(std::vector<Joint>& j) { joints = j; }

    void SetSkeleton(RuntimeSkeleton skel)
    {
        skeleton = std::move(skel);
        numJoints = skeleton->num_joints();
        jointMatrices.resize(numJoints);
    }

    void AddAnimation(RuntimeAnimation animation)
    {
        animationsMap[std::string(animation->name())] = animations.size();
        animations.emplace_back(std::move(animation));
    }

    void UpdateAnimation(float deltaTime)
    {
        if (currentAnimation >= animations.size()) return; // Prevent out-of-bounds access
        if (!HasAnimations() || !skeleton) return;
        sampleAnimation(deltaTime, *animations[currentAnimation], *skeleton);
    }

    void SetBoneTransformations(const Shader& shader)
    {
        shader.Use();
        shader.SetBool("animated", HasAnimations());
        if (HasAnimations())
            shader.SetMat4v("finalBonesMatrices", jointMatrices);
    }

    void SetCurrentAnimation(const std::string& animName)
    {
        auto it = animationsMap.find(animName);
        if (it != animationsMap.end())
        {
            currentAnimation = it->second;
            context.Resize(animations[currentAnimation]->num_tracks());
        }
    }

    void SetCurrentAnimation(const unsigned int index)
    {
        if (index < animations.size())
        {
            currentAnimation = index;
            context.Resize(animations[currentAnimation]->num_tracks());
        }
    }

    const bool HasSkeleton() { return numJoints > 0; }
    const bool HasAnimations() { return !animations.empty(); }
    const unsigned int GetNumAnimations() { return animations.size(); }
    std::map<std::string, unsigned int>& GetAnimationList() { return animationsMap; }

    void Debug()
    {
        std::cout << "Animated Model: "
            << "hasAnimations: " << (HasAnimations() ? "yes" : "no")
            << ", bonesCount: " << numJoints
            << ", numAnimations: " << GetNumAnimations()
            << ", meshes: " << meshes.size()
            << std::endl;

        for (const auto& joint : joints)
            std::cout << "Joint: " << joint.name
                << ", localTransform: " << glm::to_string(OzzTransformToGlmMat4(joint.localTransform))
                << std::endl;

        BasicModel::Debug();

        for (const auto& [name, index] : animationsMap)
            std::cout << "Animation: " << name
                << ", Index: " << index
                << ", Duration: " << animations[index]->duration()
                << std::endl;
    }

private:
    RuntimeSkeleton skeleton;
    std::vector<Joint> joints;
    unsigned int numJoints;
    std::vector<RuntimeAnimation> animations;
    std::map<std::string, unsigned int> animationsMap;
    ozz::animation::SamplingJob::Context context;
    unsigned int currentAnimation;
    float animationTime;
    std::vector<glm::mat4> jointMatrices;

    void sampleAnimation(float deltaTime, const ozz::animation::Animation& animation, ozz::animation::Skeleton& skeleton)
    {
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
            std::fill(jointMatrices.begin(), jointMatrices.end(), glm::mat4(1.0f)); // Reset to identity
            return;
        }

        // Step 3: Convert to glm::mat4 for GPU
        for (size_t i = 0; i < modelSpaceTransforms.size(); ++i)
            jointMatrices[i] = OzzFloat4x4ToGlmMat4(modelSpaceTransforms[i]) * joints[i].invBindPose;
    }
};