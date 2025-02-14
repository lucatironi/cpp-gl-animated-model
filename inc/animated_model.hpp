#pragma once

#include "basic_model.hpp"
#include "shader.hpp"

#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/base/maths/soa_transform.h"

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