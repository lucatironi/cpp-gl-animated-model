#pragma once

#include "basic_model.hpp"
#include "shader.hpp"

#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/blending_job.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/memory/unique_ptr.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

using RuntimeSkeleton = ozz::unique_ptr<ozz::animation::Skeleton>;
using RuntimeAnimation = ozz::unique_ptr<ozz::animation::Animation>;

struct Joint
{
    std::string name;
    int parentIndex;
    ozz::math::Transform localTransform;
    glm::mat4 invBindPose;
};

static inline glm::mat4 OzzToGlmMat4(const ozz::math::Float4x4& from)
{
    glm::mat4 to;
    memcpy(glm::value_ptr(to), &from.cols[0], sizeof(glm::mat4));
    return to;
}

class AnimatedModel : public BasicModel
{
public:
    AnimatedModel() : numJoints(0), currentAnimation(0), animationTime(0.0f) {}
    ~AnimatedModel() = default;

    void Draw(const Shader& shader) const override
    {
        for (const auto& mesh : meshes)
            mesh.Draw(shader);
    }

    void SetJoints(std::vector<Joint>& j) { joints = j; }

    void SetSkeleton(RuntimeSkeleton skel)
    {
        skeleton = std::move(skel);
        numJoints = skeleton->num_joints();
        int numSoa = skeleton->num_soa_joints();

        jointMatrices.resize(numJoints);
        currentLocal.resize(numSoa);
        previousLocal.resize(numSoa);
        blendedLocal.resize(numSoa);
        modelSpaceTransforms.resize(numJoints);

        // Both contexts must be resized to num_joints
        context.Resize(numJoints);
        previousContext.Resize(numJoints);
    }

    void AddAnimation(RuntimeAnimation animation)
    {
        animationsMap[std::string(animation->name())] = (unsigned int)animations.size();
        animations.emplace_back(std::move(animation));
    }

    void SampleAnimation(float deltaTime)
    {
        if (!skeleton || animations.empty()) return;

        // 1. Advance timelines
        animationTime += deltaTime;
        if (animationTime > animations[currentAnimation]->duration())
            animationTime = fmod(animationTime, animations[currentAnimation]->duration());

        if (isBlending)
        {
            previousAnimationTime += deltaTime;
            if (previousAnimationTime > animations[previousAnimation]->duration())
                previousAnimationTime = fmod(previousAnimationTime, animations[previousAnimation]->duration());

            blendWeight += deltaTime / blendDuration;
            if (blendWeight >= 1.0f)
            {
                blendWeight = 1.0f;
                isBlending = false;
            }
        }

        // 2. Sample Current
        ozz::animation::SamplingJob samplingCurrent;
        samplingCurrent.animation = animations[currentAnimation].get();
        samplingCurrent.context = &context;
        samplingCurrent.ratio = animationTime / animations[currentAnimation]->duration();
        samplingCurrent.output = ozz::make_span(currentLocal);
        if (!samplingCurrent.Run()) return;

        if (isBlending)
        {
            // 3. Sample Previous
            ozz::animation::SamplingJob samplingPrevious;
            samplingPrevious.animation = animations[previousAnimation].get();
            samplingPrevious.context = &previousContext;
            samplingPrevious.ratio = previousAnimationTime / animations[previousAnimation]->duration();
            samplingPrevious.output = ozz::make_span(previousLocal);
            if (!samplingPrevious.Run()) return;

            // 4. Setup Blending Job
            ozz::animation::BlendingJob blendJob;

            // Use {} to value-initialize the layers (sets spans to empty/null)
            ozz::animation::BlendingJob::Layer layers[2] = {};

            layers[0].transform = ozz::make_span(previousLocal);
            layers[0].weight = 1.0f - blendWeight;
            // Do NOT touch layers[0].joint_weights; it is now a null span of the correct type

            layers[1].transform = ozz::make_span(currentLocal);
            layers[1].weight = blendWeight;
            // Do NOT touch layers[1].joint_weights

            blendJob.layers = ozz::make_span(layers);
            blendJob.output = ozz::make_span(blendedLocal);
            blendJob.rest_pose = skeleton->joint_rest_poses();

            if (!blendJob.Run())
            {
                // If it fails, we fall back to current animation
                std::copy(currentLocal.begin(), currentLocal.end(), blendedLocal.begin());
            }
        }
        else
        {
            std::copy(currentLocal.begin(), currentLocal.end(), blendedLocal.begin());
        }

        // 5. Local to Model Space
        ozz::animation::LocalToModelJob ltmJob;
        ltmJob.skeleton = skeleton.get();
        ltmJob.input = ozz::make_span(blendedLocal);
        ltmJob.output = ozz::make_span(modelSpaceTransforms);
        ltmJob.Run();

        // 6. Finalize for GPU
        for (int i = 0; i < (int)numJoints; ++i)
        {
            jointMatrices[i] = OzzToGlmMat4(modelSpaceTransforms[i]) * joints[i].invBindPose;
        }
    }

    void UpdateAnimation(float deltaTime)
    {
        if (animations.empty() || !skeleton) return;
        SampleAnimation(deltaTime);
    }

    void PlayAnimation(const std::string& animName, float duration = 0.2f)
    {
        auto it = animationsMap.find(animName);
        if (it != animationsMap.end() && it->second != currentAnimation)
        {
            previousAnimation = currentAnimation;
            previousAnimationTime = animationTime;

            // --- PHASE SYNC START ---
            // Calculate how far through the old animation we were (0.0 to 1.0)
            float ratio = animationTime / animations[previousAnimation]->duration();

            currentAnimation = it->second;

            // Start the new animation at the same relative spot
            animationTime = ratio * animations[currentAnimation]->duration();
            // --- PHASE SYNC END ---

            blendWeight = 0.0f;
            blendDuration = duration;
            isBlending = true;
        }
    }

    void SetBoneTransformations(const Shader& shader)
    {
        shader.Use();
        shader.SetBool("animated", !animations.empty());
        if (!animations.empty())
            shader.SetMat4v("finalBonesMatrices", jointMatrices);
    }

    const bool HasAnimations() { return !animations.empty(); }
    const unsigned int GetNumAnimations() { return (unsigned int)animations.size(); }
    std::map<std::string, unsigned int>& GetAnimationList() { return animationsMap; }
    const ozz::animation::Skeleton& GetSkeleton() const { return *skeleton; }

    void Debug()
    {
        std::cout << "Animated Model: "
            << ", hasAnimations: " << (HasAnimations() ? "yes" : "no")
            << ", numAnimations: " << GetNumAnimations()
            << ", bonesCount: " << numJoints
            << ", meshes: " << meshes.size()
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
    ozz::animation::SamplingJob::Context previousContext;

    unsigned int previousAnimation = 0;
    unsigned int currentAnimation = 0;
    float blendWeight = 1.0f;
    float blendDuration = 0.5f;
    bool isBlending = false;
    float previousAnimationTime = 0.0f;
    float animationTime = 0.0f;

    std::vector<glm::mat4> jointMatrices;
    std::vector<ozz::math::SoaTransform> currentLocal;
    std::vector<ozz::math::SoaTransform> previousLocal;
    std::vector<ozz::math::SoaTransform> blendedLocal;
    std::vector<ozz::math::Float4x4> modelSpaceTransforms;
};
