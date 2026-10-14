#version 450

layout(location = 0) out vec3 outColor;

layout(push_constant) uniform SceneObjectPushConstants {
    vec4 position;
    vec4 rotation;
    vec4 scale;
    vec4 color;
    vec4 material;
    vec4 camera;
} objectData;

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
    vec3 p = positions[indices[gl_VertexIndex]] * objectData.scale.xyz;
    float rx = objectData.rotation.x;
    float ry = objectData.rotation.y;
    float rz = objectData.rotation.z;
    p = vec3(p.x, cos(rx) * p.y - sin(rx) * p.z, sin(rx) * p.y + cos(rx) * p.z);
    p = vec3(cos(ry) * p.x + sin(ry) * p.z, p.y, -sin(ry) * p.x + cos(ry) * p.z);
    p = vec3(cos(rz) * p.x - sin(rz) * p.y, sin(rz) * p.x + cos(rz) * p.y, p.z);
    p += objectData.position.xyz;

    float cy = cos(objectData.camera.x);
    float sy = sin(objectData.camera.x);
    float cx = cos(objectData.camera.y);
    float sx = sin(objectData.camera.y);
    p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    p = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);
    gl_Position = vec4(p.x * 0.72 * objectData.camera.z,
                       p.y * 0.72 * objectData.camera.z, 0.5 + p.z * 0.10, 1.0);

    const vec3 faceShade[6] = vec3[](
        vec3(0.72, 0.84, 1.00), vec3(0.42, 0.54, 0.72),
        vec3(0.62, 0.76, 0.94), vec3(0.95, 1.00, 1.00),
        vec3(0.48, 0.61, 0.79), vec3(0.86, 0.94, 1.00)
    );
    vec3 face = faceShade[gl_VertexIndex / 6];
    float metallic = clamp(objectData.material.x, 0.0, 1.0);
    float roughness = clamp(objectData.material.y, 0.04, 1.0);
    float emission = clamp(objectData.material.z, 0.0, 4.0);
    float diffuse = dot(face, vec3(0.3333333));
    float specular = pow(clamp(diffuse, 0.0, 1.0), mix(32.0, 3.0, roughness)) * metallic * 0.32;
    vec3 lit = objectData.color.rgb * face * (1.0 - metallic * 0.22);
    lit += vec3(specular);
    lit += objectData.color.rgb * emission;
    outColor = clamp(lit, 0.0, 1.0);
}
