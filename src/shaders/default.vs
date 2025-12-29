#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;
layout(location = 3) in ivec4 aBoneIds;
layout(location = 4) in vec4 aWeights;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec4 FragPosLightSpace;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat3 normalMatrix;
uniform mat4 lightSpaceMatrix;
uniform bool shadowPass;

uniform bool animated;
const int MAX_BONES = 100;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 finalBonesMatrices[MAX_BONES];

void main()
{
    vec4 totalPosition = vec4(0.0);
    vec3 localNormal = vec3(0.0);

    if (animated)
    {
        // Calculate the single weighted bone matrix for THIS vertex
        mat4 boneTransform = mat4(0.0);
        for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
        {
            if (aBoneIds[i] == -1) continue;
            if (aBoneIds[i] >= MAX_BONES) break;

            boneTransform += finalBonesMatrices[aBoneIds[i]] * aWeights[i];
        }

        // Apply it to position
        totalPosition = boneTransform * vec4(aPos, 1.0f);

        // Apply it to normal (using mat3 to ignore translation)
        localNormal = mat3(boneTransform) * aNormal;

        // Final World Space Normal
        Normal = normalize(normalMatrix * localNormal);
    }
    else
    {
        totalPosition = vec4(aPos, 1.0f);
        Normal = normalize(normalMatrix * aNormal);
    }

    vec4 worldPos = modelMatrix * totalPosition;
    FragPos = worldPos.xyz;
    TexCoords = aTexCoords;
    FragPosLightSpace = lightSpaceMatrix * vec4(FragPos, 1.0);

    if (shadowPass)
        gl_Position = lightSpaceMatrix * worldPos;
    else
        gl_Position = projectionMatrix * viewMatrix * worldPos;
}