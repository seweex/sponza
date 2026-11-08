#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexcoord;

layout(location = 0) out vec3 fragColor;

void main() {
    const mat4 u_MVP = mat4(
            vec4( 0.0,       0.0,      -1.002004, -1.0),
            vec4( 0.0,      -0.509525,  0.0,       0.0),
            vec4( 0.286612,  0.0,       0.0,       0.0),
            vec4(-0.143306,  0.601240,  3.867736,  3.96)
    );

    gl_Position = u_MVP * vec4(inPosition, 1.0);
    fragColor.x = inTexcoord.x;
    fragColor.y = inTexcoord.y;
    fragColor.z = 0;
}
