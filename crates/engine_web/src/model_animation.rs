// Rust-owned glTF animation import, sampling, and blending for model node
// transforms. The runtime supports node-local translation, rotation, and scale
// channels; morph target animation remains out of scope.

use crate::model_assets::{ModelAssetError, ModelNode, ModelNodeTransform};

#[derive(Clone, Debug, PartialEq)]
pub struct ModelAnimationClip {
    pub name: Option<String>,
    pub duration_seconds: f32,
    pub channels: Vec<ModelAnimationChannel>,
}

impl ModelAnimationClip {
    /// Samples this clip over imported model node transforms, wrapping time.
    pub fn sample_transforms(
        &self,
        base_transforms: &[ModelNodeTransform],
        time_seconds: f32,
    ) -> Result<Vec<ModelNodeTransform>, ModelAssetError> {
        if !time_seconds.is_finite() {
            return Err(ModelAssetError::InvalidAnimationTime);
        }

        for (channel_index, channel) in self.channels.iter().enumerate() {
            validate_animation_channel_shape(0, channel_index, channel)?;
        }

        let sample_time = self.wrapped_time(time_seconds);
        let mut transforms = base_transforms.to_vec();
        for channel in &self.channels {
            let transform = transforms.get_mut(channel.target_node).ok_or(
                ModelAssetError::InvalidAnimationTargetNode {
                    node_index: channel.target_node,
                },
            )?;
            channel.apply_sample(transform, sample_time);
        }

        Ok(transforms)
    }

    /// Samples this clip over imported model node transforms, wrapping time.
    pub fn sample_node_transforms(
        &self,
        nodes: &[ModelNode],
        time_seconds: f32,
    ) -> Result<Vec<ModelNodeTransform>, ModelAssetError> {
        let base_transforms: Vec<ModelNodeTransform> =
            nodes.iter().map(|node| node.local_transform).collect();
        self.sample_transforms(&base_transforms, time_seconds)
    }

    /// Returns a clip-local looping time in seconds.
    pub fn wrapped_time(&self, time_seconds: f32) -> f32 {
        if self.duration_seconds > f32::EPSILON {
            time_seconds.rem_euclid(self.duration_seconds)
        } else {
            time_seconds
        }
    }
}

/// Blends two sampled node-local poses with normalized TRS interpolation.
pub fn blend_node_transforms(
    from: &[ModelNodeTransform],
    to: &[ModelNodeTransform],
    amount: f32,
) -> Result<Vec<ModelNodeTransform>, ModelAssetError> {
    if from.len() != to.len() {
        return Err(ModelAssetError::InvalidAnimationBlendTransformCount {
            from_count: from.len(),
            to_count: to.len(),
        });
    }
    if !amount.is_finite() {
        return Err(ModelAssetError::InvalidAnimationTime);
    }

    let amount = amount.clamp(0.0, 1.0);
    Ok(from
        .iter()
        .zip(to.iter())
        .map(|(from, to)| ModelNodeTransform {
            translation: lerp_vec3(from.translation, to.translation, amount),
            rotation: slerp_quat(from.rotation, to.rotation, amount),
            scale: lerp_vec3(from.scale, to.scale, amount),
        })
        .collect())
}

#[derive(Clone, Debug, PartialEq)]
pub struct ModelAnimationChannel {
    pub target_node: usize,
    pub target: ModelAnimationTarget,
    pub interpolation: ModelAnimationInterpolation,
    pub inputs: Vec<f32>,
    pub outputs: ModelAnimationOutputs,
}

impl ModelAnimationChannel {
    /// Applies this channel at the provided clip-local time.
    fn apply_sample(&self, transform: &mut ModelNodeTransform, time_seconds: f32) {
        match self
            .outputs
            .sample(self.interpolation, &self.inputs, time_seconds)
        {
            ModelAnimationSample::Translation(value) => transform.translation = value,
            ModelAnimationSample::Rotation(value) => transform.rotation = value,
            ModelAnimationSample::Scale(value) => transform.scale = value,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ModelAnimationTarget {
    Translation,
    Rotation,
    Scale,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ModelAnimationInterpolation {
    Linear,
    Step,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ModelAnimationOutputs {
    Translations(Vec<[f32; 3]>),
    Rotations(Vec<[f32; 4]>),
    Scales(Vec<[f32; 3]>),
}

impl ModelAnimationOutputs {
    /// Returns the number of sampled output values.
    fn len(&self) -> usize {
        match self {
            Self::Translations(values) => values.len(),
            Self::Rotations(values) => values.len(),
            Self::Scales(values) => values.len(),
        }
    }

    /// Samples this output stream at clip-local time.
    fn sample(
        &self,
        interpolation: ModelAnimationInterpolation,
        inputs: &[f32],
        time_seconds: f32,
    ) -> ModelAnimationSample {
        if inputs.len() == 1 || time_seconds <= inputs[0] {
            return self.output_at(0);
        }
        let last_index = inputs.len() - 1;
        if time_seconds >= inputs[last_index] {
            return self.output_at(last_index);
        }

        let upper = inputs.partition_point(|input| *input < time_seconds);
        if upper < inputs.len() && (inputs[upper] - time_seconds).abs() <= f32::EPSILON {
            return self.output_at(upper);
        }
        if interpolation == ModelAnimationInterpolation::Step {
            return self.output_at(upper.saturating_sub(1));
        }

        let lower = upper.saturating_sub(1);
        let span = inputs[upper] - inputs[lower];
        let amount = if span.abs() <= f32::EPSILON {
            0.0
        } else {
            ((time_seconds - inputs[lower]) / span).clamp(0.0, 1.0)
        };

        self.interpolate(lower, upper, amount)
    }

    /// Reads one output sample without interpolation.
    fn output_at(&self, index: usize) -> ModelAnimationSample {
        match self {
            Self::Translations(values) => ModelAnimationSample::Translation(values[index]),
            Self::Rotations(values) => {
                ModelAnimationSample::Rotation(normalize_quat(values[index]))
            }
            Self::Scales(values) => ModelAnimationSample::Scale(values[index]),
        }
    }

    /// Interpolates between two output samples.
    fn interpolate(&self, lower: usize, upper: usize, amount: f32) -> ModelAnimationSample {
        match self {
            Self::Translations(values) => {
                ModelAnimationSample::Translation(lerp_vec3(values[lower], values[upper], amount))
            }
            Self::Rotations(values) => {
                ModelAnimationSample::Rotation(slerp_quat(values[lower], values[upper], amount))
            }
            Self::Scales(values) => {
                ModelAnimationSample::Scale(lerp_vec3(values[lower], values[upper], amount))
            }
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum ModelAnimationSample {
    Translation([f32; 3]),
    Rotation([f32; 4]),
    Scale([f32; 3]),
}

/// Imports supported glTF animation clips from a parsed document.
pub(crate) fn import_animations(
    document: &gltf::Document,
    buffers: &[Vec<u8>],
) -> Result<Vec<ModelAnimationClip>, ModelAssetError> {
    let mut clips = Vec::new();

    for animation in document.animations() {
        let mut duration_seconds = 0.0_f32;
        let mut channels = Vec::new();
        for channel in animation.channels() {
            let imported = import_animation_channel(animation.index(), channel, buffers)?;
            if let Some(last_input) = imported.inputs.last() {
                duration_seconds = duration_seconds.max(*last_input);
            }
            channels.push(imported);
        }

        clips.push(ModelAnimationClip {
            name: animation.name().map(str::to_owned),
            duration_seconds,
            channels,
        });
    }

    Ok(clips)
}

/// Imports one glTF animation channel with supported target/output types.
fn import_animation_channel(
    animation_index: usize,
    channel: gltf::animation::Channel<'_>,
    buffers: &[Vec<u8>],
) -> Result<ModelAnimationChannel, ModelAssetError> {
    let channel_index = channel.index();
    let interpolation = import_interpolation(animation_index, channel_index, channel.sampler())?;
    let target = import_target(animation_index, channel_index, channel.target().property())?;
    let target_node = channel.target().node().index();
    let reader = channel.reader(|buffer| {
        buffers
            .get(buffer.index())
            .map(|buffer_data| buffer_data.as_slice())
    });

    let inputs: Vec<f32> = reader
        .read_inputs()
        .ok_or(ModelAssetError::MissingAnimationInput {
            animation_index,
            channel_index,
        })?
        .collect();
    let outputs = reader
        .read_outputs()
        .ok_or(ModelAssetError::MissingAnimationOutput {
            animation_index,
            channel_index,
        })
        .and_then(|outputs| import_outputs(animation_index, channel_index, target, outputs))?;
    ensure_animation_channel_shape(animation_index, channel_index, &inputs, &outputs)?;

    Ok(ModelAnimationChannel {
        target_node,
        target,
        interpolation,
        inputs,
        outputs,
    })
}

/// Validates one public animation channel before sampling.
fn validate_animation_channel_shape(
    animation_index: usize,
    channel_index: usize,
    channel: &ModelAnimationChannel,
) -> Result<(), ModelAssetError> {
    match (channel.target, &channel.outputs) {
        (ModelAnimationTarget::Translation, ModelAnimationOutputs::Translations(_))
        | (ModelAnimationTarget::Rotation, ModelAnimationOutputs::Rotations(_))
        | (ModelAnimationTarget::Scale, ModelAnimationOutputs::Scales(_)) => {}
        _ => {
            return Err(ModelAssetError::InvalidAnimationData {
                animation_index,
                channel_index,
                attribute: "target/output",
            });
        }
    }

    ensure_animation_channel_shape(
        animation_index,
        channel_index,
        &channel.inputs,
        &channel.outputs,
    )
}

/// Converts supported glTF interpolation modes into engine animation modes.
fn import_interpolation(
    animation_index: usize,
    channel_index: usize,
    sampler: gltf::animation::Sampler<'_>,
) -> Result<ModelAnimationInterpolation, ModelAssetError> {
    match sampler.interpolation() {
        gltf::animation::Interpolation::Linear => Ok(ModelAnimationInterpolation::Linear),
        gltf::animation::Interpolation::Step => Ok(ModelAnimationInterpolation::Step),
        interpolation => Err(ModelAssetError::UnsupportedAnimationInterpolation {
            animation_index,
            channel_index,
            interpolation: format!("{interpolation:?}"),
        }),
    }
}

/// Converts supported glTF animation target paths.
fn import_target(
    animation_index: usize,
    channel_index: usize,
    property: gltf::animation::Property,
) -> Result<ModelAnimationTarget, ModelAssetError> {
    match property {
        gltf::animation::Property::Translation => Ok(ModelAnimationTarget::Translation),
        gltf::animation::Property::Rotation => Ok(ModelAnimationTarget::Rotation),
        gltf::animation::Property::Scale => Ok(ModelAnimationTarget::Scale),
        property => Err(ModelAssetError::UnsupportedAnimationTarget {
            animation_index,
            channel_index,
            target: format!("{property:?}"),
        }),
    }
}

/// Converts glTF animation outputs into typed engine-owned output arrays.
fn import_outputs(
    animation_index: usize,
    channel_index: usize,
    target: ModelAnimationTarget,
    outputs: gltf::animation::util::ReadOutputs<'_>,
) -> Result<ModelAnimationOutputs, ModelAssetError> {
    match (target, outputs) {
        (
            ModelAnimationTarget::Translation,
            gltf::animation::util::ReadOutputs::Translations(values),
        ) => Ok(ModelAnimationOutputs::Translations(values.collect())),
        (ModelAnimationTarget::Rotation, gltf::animation::util::ReadOutputs::Rotations(values)) => {
            Ok(ModelAnimationOutputs::Rotations(
                values.into_f32().collect(),
            ))
        }
        (ModelAnimationTarget::Scale, gltf::animation::util::ReadOutputs::Scales(values)) => {
            Ok(ModelAnimationOutputs::Scales(values.collect()))
        }
        (_, gltf::animation::util::ReadOutputs::MorphTargetWeights(_)) => {
            Err(ModelAssetError::UnsupportedAnimationTarget {
                animation_index,
                channel_index,
                target: "MorphTargetWeights".to_string(),
            })
        }
        (_, outputs) => Err(ModelAssetError::InvalidAnimationData {
            animation_index,
            channel_index,
            attribute: match outputs {
                gltf::animation::util::ReadOutputs::Translations(_) => "translation output",
                gltf::animation::util::ReadOutputs::Rotations(_) => "rotation output",
                gltf::animation::util::ReadOutputs::Scales(_) => "scale output",
                gltf::animation::util::ReadOutputs::MorphTargetWeights(_) => "morph output",
            },
        }),
    }
}

/// Validates keyframe times and output counts before sampling.
fn ensure_animation_channel_shape(
    animation_index: usize,
    channel_index: usize,
    inputs: &[f32],
    outputs: &ModelAnimationOutputs,
) -> Result<(), ModelAssetError> {
    if inputs.is_empty() || inputs.len() != outputs.len() {
        return Err(ModelAssetError::InvalidAnimationKeyframes {
            animation_index,
            channel_index,
            input_count: inputs.len(),
            output_count: outputs.len(),
        });
    }
    if inputs.iter().any(|value| !value.is_finite())
        || inputs.windows(2).any(|window| window[1] < window[0])
    {
        return Err(ModelAssetError::InvalidAnimationData {
            animation_index,
            channel_index,
            attribute: "input time",
        });
    }
    if !outputs_are_finite(outputs) {
        return Err(ModelAssetError::InvalidAnimationData {
            animation_index,
            channel_index,
            attribute: "output",
        });
    }

    Ok(())
}

/// Returns true when all output samples contain finite float values.
fn outputs_are_finite(outputs: &ModelAnimationOutputs) -> bool {
    match outputs {
        ModelAnimationOutputs::Translations(values) | ModelAnimationOutputs::Scales(values) => {
            values
                .iter()
                .all(|value| value.iter().all(|component| component.is_finite()))
        }
        ModelAnimationOutputs::Rotations(values) => values
            .iter()
            .all(|value| value.iter().all(|component| component.is_finite())),
    }
}

/// Linearly interpolates two 3D vectors.
fn lerp_vec3(a: [f32; 3], b: [f32; 3], amount: f32) -> [f32; 3] {
    [
        a[0] + (b[0] - a[0]) * amount,
        a[1] + (b[1] - a[1]) * amount,
        a[2] + (b[2] - a[2]) * amount,
    ]
}

/// Spherically interpolates two quaternions and keeps the shortest arc.
fn slerp_quat(a: [f32; 4], b: [f32; 4], amount: f32) -> [f32; 4] {
    let a = normalize_quat(a);
    let mut b = normalize_quat(b);
    let mut dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if dot < 0.0 {
        dot = -dot;
        b = [-b[0], -b[1], -b[2], -b[3]];
    }

    if dot > 0.9995 {
        return normalize_quat([
            a[0] + (b[0] - a[0]) * amount,
            a[1] + (b[1] - a[1]) * amount,
            a[2] + (b[2] - a[2]) * amount,
            a[3] + (b[3] - a[3]) * amount,
        ]);
    }

    let theta_0 = dot.clamp(-1.0, 1.0).acos();
    let sin_theta_0 = theta_0.sin();
    if sin_theta_0.abs() <= f32::EPSILON {
        return a;
    }

    let theta = theta_0 * amount;
    let sin_theta = theta.sin();
    let s0 = theta.cos() - dot * sin_theta / sin_theta_0;
    let s1 = sin_theta / sin_theta_0;

    normalize_quat([
        a[0] * s0 + b[0] * s1,
        a[1] * s0 + b[1] * s1,
        a[2] * s0 + b[2] * s1,
        a[3] * s0 + b[3] * s1,
    ])
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
