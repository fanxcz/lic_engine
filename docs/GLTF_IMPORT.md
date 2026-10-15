# glTF/GLB model import

LIC Engine imports glTF 2.0 `.gltf` and binary `.glb` files. In the editor click **Import Model**, paste the full path and select **Import**. The model is validated first, then copied into the current project's `Assets/Models/<model-name>/` folder. Relative external buffer/image dependencies are copied with the same relative paths. The new MeshInstance3D node stores a safe project-relative reference in the scene.

Supported data includes triangle-list primitives, POSITION, NORMAL, TEXCOORD_0, node transforms, indexed or non-indexed triangle geometry, generated normals when missing, material base-color factors, metallic, roughness and emissive factors. The renderer uploads vertex/index buffers and draws primitives separately.

Limits: source file maximum 512 MiB; up to 2 million vertices and 6 million indices per imported mesh. Remote URLs, absolute dependency URIs and path traversal in dependencies are rejected. Non-triangle topology, skins, morph targets, animation, material textures, normal maps, full color-space conversion and physically based lighting are not yet implemented. Texture dependencies can be copied but are not sampled by the current shader.
