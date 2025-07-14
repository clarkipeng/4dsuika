#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

// Outputs to the fragment shader (these will be smoothly interpolated)
out vec2 TexCoords;
out vec3 WorldPos;
out vec3 Normal;

// Uniforms for our transformation matrices
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // Pass texture coordinates straight through
    TexCoords = aTexCoords;

    // Calculate position and normal in world space FOR EACH VERTEX
    WorldPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;

    // The GPU will then interpolate these 'out' variables for every pixel
    // before passing them to the fragment shader.

    // Calculate the final screen position
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
// #version 330 core
// layout (location = 0) in vec3 aPos;
// layout (location = 1) in vec3 aNormal;
// layout (location = 2) in vec2 aTexCoords;

// out vec2 TexCoords;
// out vec3 WorldPos;
// out vec3 Normal;

// uniform mat4 projection;
// uniform mat4 view;
// uniform mat4 model;
// uniform mat3 normalMatrix;

// void main()
// {
//     TexCoords = aTexCoords;
//     WorldPos = vec3(model * vec4(aPos, 1.0));
//     // Normal = normalMatrix * aNormal;
//     Normal = mat3(transpose(inverse(model))) * aNormal;
// 
//     gl_Position =  projection * view * vec4(WorldPos, 1.0);
// 