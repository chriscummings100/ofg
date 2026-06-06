// CPU skinning helpers for imported glTF model primitives.
// This module evaluates joint matrices and skins vertices into the existing
// static model vertex format; per-frame GPU skinning is intentionally out of
// scope for the first skinned milestone.

use crate::model_assets::{
    ModelAsset, ModelAssetError, ModelNodeTransform, ModelPrimitive, ModelVertex,
};

const IDENTITY_MATRIX: [f32; 16] = [
    1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0,
];

/// Computes all skin joint matrices for the selected skin and node pose.
pub fn skin_joint_matrices(
    model: &ModelAsset,
    skin_index: usize,
    node_transforms: &[ModelNodeTransform],
) -> Result<Vec<[f32; 16]>, ModelAssetError> {
    let skin = model
        .skins
        .get(skin_index)
        .ok_or(ModelAssetError::InvalidSkinIndex { skin_index })?;
    let world_matrices = model_node_world_matrices(model, node_transforms)?;
    let mut joint_matrices = Vec::with_capacity(skin.joints.len());

    for (joint_index, node_index) in skin.joints.iter().copied().enumerate() {
        let joint_world =
            world_matrices
                .get(node_index)
                .ok_or(ModelAssetError::InvalidSkinJoint {
                    skin_index,
                    joint_index,
                    node_index,
                })?;
        joint_matrices.push(matrix_mul(
            *joint_world,
            skin.inverse_bind_matrices[joint_index],
        ));
    }

    Ok(joint_matrices)
}

/// Skins one model primitive's vertices with precomputed joint matrices.
pub fn skin_primitive_vertices(
    primitive: &ModelPrimitive,
    joint_matrices: &[[f32; 16]],
) -> Result<Vec<ModelVertex>, ModelAssetError> {
    primitive
        .vertices
        .iter()
        .enumerate()
        .map(|(vertex_index, vertex)| {
            skin_vertex(primitive.mesh_index, vertex_index, *vertex, joint_matrices)
        })
        .collect()
}

/// Computes model node world matrices for a sampled node-local pose.
pub fn model_node_world_matrices(
    model: &ModelAsset,
    node_transforms: &[ModelNodeTransform],
) -> Result<Vec<[f32; 16]>, ModelAssetError> {
    if node_transforms.len() != model.nodes.len() {
        return Err(ModelAssetError::InvalidSkinNodeTransformCount {
            node_count: model.nodes.len(),
            transform_count: node_transforms.len(),
        });
    }

    let mut world_matrices = vec![None; model.nodes.len()];
    for node_index in 0..model.nodes.len() {
        compute_node_world_matrix(model, node_transforms, node_index, &mut world_matrices)?;
    }

    Ok(world_matrices
        .into_iter()
        .map(|matrix| matrix.unwrap_or(IDENTITY_MATRIX))
        .collect())
}

/// Recursively computes one node world matrix after its parent matrix.
fn compute_node_world_matrix(
    model: &ModelAsset,
    node_transforms: &[ModelNodeTransform],
    node_index: usize,
    world_matrices: &mut [Option<[f32; 16]>],
) -> Result<[f32; 16], ModelAssetError> {
    if let Some(matrix) = world_matrices[node_index] {
        return Ok(matrix);
    }

    let local = transform_to_matrix(node_transforms[node_index]);
    let world = if let Some(parent_index) = model.nodes[node_index].parent {
        let parent =
            compute_node_world_matrix(model, node_transforms, parent_index, world_matrices)?;
        matrix_mul(parent, local)
    } else {
        local
    };
    world_matrices[node_index] = Some(world);

    Ok(world)
}

/// Skins a single vertex, preserving static attributes and joint metadata.
fn skin_vertex(
    mesh_index: usize,
    vertex_index: usize,
    vertex: ModelVertex,
    joint_matrices: &[[f32; 16]],
) -> Result<ModelVertex, ModelAssetError> {
    let mut skinned_position = [0.0, 0.0, 0.0];
    let mut skinned_normal = [0.0, 0.0, 0.0];
    let mut total_weight = 0.0_f32;

    for slot in 0..4 {
        let weight = vertex.weights0[slot];
        if weight <= f32::EPSILON {
            continue;
        }
        if !weight.is_finite() {
            return Err(ModelAssetError::InvalidFloatData {
                mesh_index,
                attribute: "WEIGHTS_0",
            });
        }

        let joint_index = vertex.joints0[slot] as usize;
        let joint_matrix =
            joint_matrices
                .get(joint_index)
                .ok_or(ModelAssetError::InvalidSkinVertexJoint {
                    mesh_index,
                    vertex_index,
                    joint_index,
                })?;
        let position = transform_point(*joint_matrix, vertex.position);
        let normal = transform_vector(*joint_matrix, vertex.normal);

        skinned_position[0] += position[0] * weight;
        skinned_position[1] += position[1] * weight;
        skinned_position[2] += position[2] * weight;
        skinned_normal[0] += normal[0] * weight;
        skinned_normal[1] += normal[1] * weight;
        skinned_normal[2] += normal[2] * weight;
        total_weight += weight;
    }

    if total_weight <= f32::EPSILON {
        return Ok(vertex);
    }

    Ok(ModelVertex {
        position: skinned_position,
        normal: normalize_vec3(skinned_normal).unwrap_or(vertex.normal),
        ..vertex
    })
}

/// Converts a glTF TRS transform into a column-major matrix.
fn transform_to_matrix(transform: ModelNodeTransform) -> [f32; 16] {
    let rotation = normalize_quat(transform.rotation);
    let x2 = rotation[0] + rotation[0];
    let y2 = rotation[1] + rotation[1];
    let z2 = rotation[2] + rotation[2];
    let xx = rotation[0] * x2;
    let yy = rotation[1] * y2;
    let zz = rotation[2] * z2;
    let xy = rotation[0] * y2;
    let xz = rotation[0] * z2;
    let yz = rotation[1] * z2;
    let wx = rotation[3] * x2;
    let wy = rotation[3] * y2;
    let wz = rotation[3] * z2;

    [
        (1.0 - (yy + zz)) * transform.scale[0],
        (xy + wz) * transform.scale[0],
        (xz - wy) * transform.scale[0],
        0.0,
        (xy - wz) * transform.scale[1],
        (1.0 - (xx + zz)) * transform.scale[1],
        (yz + wx) * transform.scale[1],
        0.0,
        (xz + wy) * transform.scale[2],
        (yz - wx) * transform.scale[2],
        (1.0 - (xx + yy)) * transform.scale[2],
        0.0,
        transform.translation[0],
        transform.translation[1],
        transform.translation[2],
        1.0,
    ]
}

/// Multiplies two column-major 4x4 matrices.
fn matrix_mul(a: [f32; 16], b: [f32; 16]) -> [f32; 16] {
    let mut out = [0.0; 16];
    for column in 0..4 {
        for row in 0..4 {
            out[column * 4 + row] = a[row] * b[column * 4]
                + a[4 + row] * b[column * 4 + 1]
                + a[8 + row] * b[column * 4 + 2]
                + a[12 + row] * b[column * 4 + 3];
        }
    }

    out
}

/// Transforms a position by a column-major matrix.
fn transform_point(matrix: [f32; 16], point: [f32; 3]) -> [f32; 3] {
    [
        matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
        matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
        matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
    ]
}

/// Transforms a direction by a column-major matrix without translation.
fn transform_vector(matrix: [f32; 16], vector: [f32; 3]) -> [f32; 3] {
    [
        matrix[0] * vector[0] + matrix[4] * vector[1] + matrix[8] * vector[2],
        matrix[1] * vector[0] + matrix[5] * vector[1] + matrix[9] * vector[2],
        matrix[2] * vector[0] + matrix[6] * vector[1] + matrix[10] * vector[2],
    ]
}

/// Normalizes a quaternion, returning identity for degenerate input.
fn normalize_quat(value: [f32; 4]) -> [f32; 4] {
    let length =
        (value[0] * value[0] + value[1] * value[1] + value[2] * value[2] + value[3] * value[3])
            .sqrt();
    if length <= f32::EPSILON {
        return [0.0, 0.0, 0.0, 1.0];
    }

    [
        value[0] / length,
        value[1] / length,
        value[2] / length,
        value[3] / length,
    ]
}

/// Normalizes a vector, returning none for degenerate input.
fn normalize_vec3(value: [f32; 3]) -> Option<[f32; 3]> {
    let length = (value[0] * value[0] + value[1] * value[1] + value[2] * value[2]).sqrt();
    if length <= f32::EPSILON {
        return None;
    }

    Some([value[0] / length, value[1] / length, value[2] / length])
}
