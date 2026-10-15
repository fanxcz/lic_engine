#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexcoord;
layout(location = 0) out vec3 outColor;

layout(push_constant) uniform SceneObjectPushConstants {
    vec4 position;
    vec4 rotation;
    vec4 scale;
    vec4 color;
    vec4 material;
    vec4 camera;
} objectData;

vec3 rotate_euler(vec3 value, vec3 angles) {
    float rx = angles.x;
    float ry = angles.y;
    float rz = angles.z;
    value = vec3(value.x, cos(rx) * value.y - sin(rx) * value.z,
                 sin(rx) * value.y + cos(rx) * value.z);
    value = vec3(cos(ry) * value.x + sin(ry) * value.z, value.y,
                 -sin(ry) * value.x + cos(ry) * value.z);
    return vec3(cos(rz) * value.x - sin(rz) * value.y,
                sin(rz) * value.x + cos(rz) * value.y, value.z);
}

void main() {
    vec3 p = rotate_euler(inPosition * objectData.scale.xyz, objectData.rotation.xyz);
    p += objectData.position.xyz;
    vec3 n = rotate_euler(inNormal, objectData.rotation.xyz);

    float cy = cos(objectData.camera.x);
    float sy = sin(objectData.camera.x);
    float cx = cos(objectData.camera.y);
    float sx = sin(objectData.camera.y);
    p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    p = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);
    n = vec3(cy * n.x + sy * n.z, n.y, -sy * n.x + cy * n.z);
    n = vec3(n.x, cx * n.y - sx * n.z, sx * n.y + cx * n.z);
    gl_Position = vec4(p.x * 0.72 * objectData.camera.z,
                       p.y * 0.72 * objectData.camera.z, 0.5 + p.z * 0.10, 1.0);

    float metallic = clamp(objectData.material.x, 0.0, 1.0);
    float roughness = clamp(objectData.material.y, 0.04, 1.0);
    float emission = clamp(objectData.material.z, 0.0, 4.0);
    vec3 lightDirection = normalize(vec3(-0.35, 0.78, 0.50));
    float diffuse = 0.22 + 0.78 * max(dot(normalize(n), lightDirection), 0.0);
    vec3 halfVector = normalize(lightDirection + vec3(0.0, 0.0, 1.0));
    float specular = pow(max(dot(normalize(n), halfVector), 0.0),
                         mix(48.0, 3.0, roughness)) * mix(0.04, 0.75, metallic);
    vec3 base = objectData.color.rgb;
    vec3 lit = base * diffuse * (1.0 - metallic * 0.20) + vec3(specular);
    lit += base * emission;
    outColor = clamp(lit, 0.0, 1.0);
}
