#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

// Enumerates camera movement directions.
enum MovementDirection
{
    MOVE_FORWARD,
    MOVE_BACKWARD,
    MOVE_LEFT,
    MOVE_RIGHT,
    MOVE_UP,
    MOVE_DOWN
};

class FPSCamera
{
public:
    // Default camera values.
    static constexpr float DEFAULT_YAW = -90.0f;
    static constexpr float DEFAULT_PITCH = 0.0f;
    static constexpr float DEFAULT_SPEED = 5.0f;
    static constexpr float DEFAULT_SENSITIVITY = 0.1f;
    static constexpr float DEFAULT_HEAD_HEIGHT = 1.75f;
    static constexpr float DEFAULT_FOV = 75.0f;
    static constexpr float DEFAULT_ASPECT_RATIO = 16.0f / 9.0f;
    static constexpr float DEFAULT_NEAR_PLANE = 0.01f;
    static constexpr float DEFAULT_FAR_PLANE = 100.0f;
    static constexpr glm::vec3 DEFAULT_POSITION = glm::vec3(0.0f);
    static constexpr glm::vec3 DEFAULT_UP = glm::vec3(0.0f, 1.0f, 0.0f);
    static constexpr glm::vec3 DEFAULT_FRONT = glm::vec3(0.0f, 0.0f, -1.0f);

    // Camera Attributes
    glm::vec3 Position;
    glm::vec3 Velocity;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    float FOV;
    float AspectRatio;
    float NearPlane;
    float FarPlane;

    // Camera options
    float HeadHeight;
    float MovementSpeed;
    float MouseSensitivity;
    bool Constrained;

    // Constructor
    FPSCamera(const glm::vec3& position = DEFAULT_POSITION, bool constrained = false)
        : Position(position), Front(DEFAULT_FRONT), WorldUp(DEFAULT_UP),
          HeadHeight(DEFAULT_HEAD_HEIGHT), Constrained(constrained),
          MovementSpeed(DEFAULT_SPEED), MouseSensitivity(DEFAULT_SENSITIVITY),
          yawAngle(DEFAULT_YAW), pitchAngle(DEFAULT_PITCH),
          FOV(DEFAULT_FOV), AspectRatio(DEFAULT_ASPECT_RATIO),
          NearPlane(DEFAULT_NEAR_PLANE), FarPlane(DEFAULT_FAR_PLANE)
    {
        updateCameraVectors();
    }

    // Returns the view matrix using Euler angles and the LookAt matrix.
    glm::mat4 GetViewMatrix() const
    {
        return glm::lookAt(Position, Position + Front, Up);
    }

    glm::mat4 GetProjectionMatrix() const
    {
        return glm::perspective(glm::radians(FOV), AspectRatio, NearPlane, FarPlane);
    }

    // Return a quaternion that represents the camera's rotation
    glm::quat GetRotation() const
    {
        return glm::quatLookAt(Front, Up);
    }

    glm::vec3 GetAngles() const
    {
        return glm::vec3(glm::radians(pitchAngle), glm::radians(yawAngle), 0.0f);
    }

    // Processes keyboard-like input for camera movement.
    void Move(const MovementDirection direction, const float deltaTime)
    {
        if (deltaTime <= 0.0f)
            return;

        Velocity = glm::vec3(0.0f);

        switch (direction)
        {
        case MOVE_FORWARD:
            Velocity += Front;
            break;
        case MOVE_BACKWARD:
            Velocity -= Front;
            break;
        case MOVE_LEFT:
            Velocity -= Right;
            break;
        case MOVE_RIGHT:
            Velocity += Right;
            break;
        case MOVE_UP:
            if (!Constrained)
                Velocity += WorldUp;
            break;
        case MOVE_DOWN:
            if (!Constrained)
                Velocity -= WorldUp;
            break;
        }

        if (glm::length(Velocity) > 0.0f)
            Velocity = glm::normalize(Velocity) * MovementSpeed;

        Position += Velocity * deltaTime;

        if (Constrained)
            Position.y = HeadHeight;
    }

    // Processes mouse input for camera rotation.
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true)
    {
        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        yawAngle += xoffset;
        pitchAngle += yoffset;

        if (constrainPitch)
            pitchAngle = glm::clamp(pitchAngle, -80.0f, 80.0f); // Constrain pitch to avoid flipping.

        updateCameraVectors();
    }

private:
    // Euler Angles
    float yawAngle;
    float pitchAngle;

    // Updates the camera's vectors based on the current yaw and pitch angles.
    void updateCameraVectors()
    {
        glm::vec3 front;
        front.x = cos(glm::radians(yawAngle)) * cos(glm::radians(pitchAngle));
        front.y = sin(glm::radians(pitchAngle));
        front.z = sin(glm::radians(yawAngle)) * cos(glm::radians(pitchAngle));
        Front = glm::normalize(front);

        // Recalculate Right and Up vectors.
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up = glm::normalize(glm::cross(Right, Front));
    }
};