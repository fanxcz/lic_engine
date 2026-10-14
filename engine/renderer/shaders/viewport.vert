#version 450

layout(location = 0) out vec3 outColor;

const vec3 positions[8] = vec3[](
    vec3(-0.62, -0.62, -0.62),
    vec3( 0.62, -0.62, -0.62),
    vec3( 0.62,  0.62, -0.62),
    vec3(-0.62,  0.62, -0.62),
    vec3(-0.62, -0.62,  0.62),
    vec3( 0.62, -0.62,  0.62),
    vec3( 0.62,  0.62,  0.62),
    vec3(-0.62,  0.62,  0.62)
);

const int indices[36] = int[](
    4, 5, 6, 6, 7, 4,
    1, 0, 3, 3, 2, 1,
    0, 4, 7, 7, 3, 0,
    5, 1, 2, 2, 6, 5,
    0, 1, 5, 5, 4, 0,
    3, 7, 6, 6, 2, 3
);

void main() {
    vec3 p = positions[indices[gl_VertexIndex]];

    // A fixed, slightly isometric camera is enough for the renderer smoke scene.
    float cy = cos(0.68);
    float sy = sin(0.68);
    float cx = cos(0.38);
    float sx = sin(0.38);
    p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    p = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);

    gl_Position = vec4(p.x * 0.92, p.y * 0.92, 0.5 + p.z * 0.25, 1.0);

    const vec3 faceColors[6] = vec3[](
        vec3(0.33, 0.62, 0.91),
        vec3(0.16, 0.29, 0.45),
        vec3(0.24, 0.47, 0.70),
        vec3(0.42, 0.72, 0.94),
        vec3(0.18, 0.34, 0.52),
        vec3(0.52, 0.77, 0.94)
    );
    outColor = faceColors[gl_VertexIndex / 6];
}
