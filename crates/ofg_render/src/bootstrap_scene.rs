//! Deterministic scene data for the first OFG WebGPU frame.

use bytemuck::{Pod, Zeroable};

/// One colored 2D vertex for the bootstrap triangle.
#[repr(C)]
#[derive(Clone, Copy, Debug, Pod, Zeroable)]
pub struct BootstrapVertex {
    pub position: [f32; 2],
    pub color: [f32; 3],
}

impl BootstrapVertex {
    /// Describes the vertex buffer layout expected by `bootstrap.wgsl`.
    pub fn layout<'a>() -> wgpu::VertexBufferLayout<'a> {
        const ATTRIBUTES: [wgpu::VertexAttribute; 2] =
            wgpu::vertex_attr_array![0 => Float32x2, 1 => Float32x3];

        wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<BootstrapVertex>() as wgpu::BufferAddress,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &ATTRIBUTES,
        }
    }
}

/// Large center triangle with red, green, and blue vertex colors.
pub const BOOTSTRAP_VERTICES: [BootstrapVertex; 3] = [
    BootstrapVertex {
        position: [-0.72, -0.58],
        color: [1.0, 0.05, 0.04],
    },
    BootstrapVertex {
        position: [0.72, -0.58],
        color: [0.05, 0.95, 0.18],
    },
    BootstrapVertex {
        position: [0.0, 0.7],
        color: [0.08, 0.28, 1.0],
    },
];

/// Clear color used by CSS, browser smoke, native smoke, and the renderer.
pub fn clear_color() -> wgpu::Color {
    wgpu::Color {
        r: 27.0 / 255.0,
        g: 37.0 / 255.0,
        b: 50.0 / 255.0,
        a: 1.0,
    }
}

/// Clear color in byte form for smoke-test pixel classification.
pub fn clear_color_rgba8() -> [u8; 4] {
    [27, 37, 50, 255]
}

#[cfg(test)]
mod tests {
    use super::{clear_color_rgba8, BootstrapVertex, BOOTSTRAP_VERTICES};

    #[test]
    fn bootstrap_scene_has_three_colored_vertices() {
        assert_eq!(BOOTSTRAP_VERTICES.len(), 3);
        assert_eq!(clear_color_rgba8(), [27, 37, 50, 255]);
        assert_eq!(std::mem::size_of::<BootstrapVertex>(), 20);
    }
}
