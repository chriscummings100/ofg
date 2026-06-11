// Deterministic CPU mipmap generation for renderer-owned RGBA8 texture arrays.
// Browser TypeScript supplies only mip 0 pixels; Rust owns the derived mip chain.

#[derive(Clone, Debug, Eq, PartialEq)]
pub(crate) struct Rgba8MipLevel {
    pub(crate) width: u32,
    pub(crate) height: u32,
    pub(crate) data: Vec<u8>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) enum TextureMipError {
    InvalidShape,
    InvalidDataLength { actual: usize, expected: usize },
}

impl std::fmt::Display for TextureMipError {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::InvalidShape => formatter.write_str("invalid texture shape for mipmaps"),
            Self::InvalidDataLength { actual, expected } => write!(
                formatter,
                "invalid texture data length for mipmaps: {actual}; expected {expected}"
            ),
        }
    }
}

/// Returns the full mip count for a 2D texture extent.
pub(crate) fn texture_mip_level_count(width: u32, height: u32) -> u32 {
    let max_dimension = width.max(height);
    if max_dimension == 0 {
        return 0;
    }

    u32::BITS - max_dimension.leading_zeros()
}

/// Builds a layered RGBA8 mip chain from mip 0 pixel bytes.
pub(crate) fn build_rgba8_mip_chain(
    width: u32,
    height: u32,
    layers: u32,
    data: &[u8],
) -> Result<Vec<Rgba8MipLevel>, TextureMipError> {
    if width == 0 || height == 0 || layers == 0 {
        return Err(TextureMipError::InvalidShape);
    }
    let expected_bytes =
        rgba8_layered_byte_len(width, height, layers).ok_or(TextureMipError::InvalidShape)?;
    if data.len() != expected_bytes {
        return Err(TextureMipError::InvalidDataLength {
            actual: data.len(),
            expected: expected_bytes,
        });
    }

    let mut levels = Vec::with_capacity(texture_mip_level_count(width, height) as usize);
    levels.push(Rgba8MipLevel {
        width,
        height,
        data: data.to_vec(),
    });
    while levels
        .last()
        .is_some_and(|level| level.width > 1 || level.height > 1)
    {
        let previous = levels.last().expect("mip chain has a base level");
        levels.push(downsample_rgba8_mip(previous, layers));
    }

    Ok(levels)
}

/// Downsamples one layered RGBA8 mip level into the next smaller level.
fn downsample_rgba8_mip(previous: &Rgba8MipLevel, layers: u32) -> Rgba8MipLevel {
    let next_width = (previous.width / 2).max(1);
    let next_height = (previous.height / 2).max(1);
    let target_bytes = rgba8_layered_byte_len(next_width, next_height, layers)
        .expect("mip dimensions shrink from a validated texture");
    let mut data = vec![0; target_bytes];

    for layer in 0..layers as usize {
        for y in 0..next_height as usize {
            let source_y_start = y * previous.height as usize / next_height as usize;
            let source_y_end = ((y + 1) * previous.height as usize / next_height as usize)
                .max(source_y_start + 1)
                .min(previous.height as usize);
            for x in 0..next_width as usize {
                let source_x_start = x * previous.width as usize / next_width as usize;
                let source_x_end = ((x + 1) * previous.width as usize / next_width as usize)
                    .max(source_x_start + 1)
                    .min(previous.width as usize);
                let mut sum = [0_u32; 4];
                let mut sample_count = 0_u32;
                for source_y in source_y_start..source_y_end {
                    for source_x in source_x_start..source_x_end {
                        let source_index = ((layer * previous.height as usize + source_y)
                            * previous.width as usize
                            + source_x)
                            * 4;
                        sum[0] += u32::from(previous.data[source_index]);
                        sum[1] += u32::from(previous.data[source_index + 1]);
                        sum[2] += u32::from(previous.data[source_index + 2]);
                        sum[3] += u32::from(previous.data[source_index + 3]);
                        sample_count += 1;
                    }
                }
                let target_index =
                    ((layer * next_height as usize + y) * next_width as usize + x) * 4;
                data[target_index] = ((sum[0] + sample_count / 2) / sample_count) as u8;
                data[target_index + 1] = ((sum[1] + sample_count / 2) / sample_count) as u8;
                data[target_index + 2] = ((sum[2] + sample_count / 2) / sample_count) as u8;
                data[target_index + 3] = ((sum[3] + sample_count / 2) / sample_count) as u8;
            }
        }
    }

    Rgba8MipLevel {
        width: next_width,
        height: next_height,
        data,
    }
}

fn rgba8_layered_byte_len(width: u32, height: u32, layers: u32) -> Option<usize> {
    (width as usize)
        .checked_mul(height as usize)?
        .checked_mul(layers as usize)?
        .checked_mul(4)
}

#[cfg(test)]
mod tests {
    use super::{build_rgba8_mip_chain, texture_mip_level_count, TextureMipError};

    #[test]
    fn texture_mip_level_count_matches_texture_extent() {
        assert_eq!(texture_mip_level_count(1, 1), 1);
        assert_eq!(texture_mip_level_count(2, 2), 2);
        assert_eq!(texture_mip_level_count(4, 2), 3);
        assert_eq!(texture_mip_level_count(3, 5), 3);
        assert_eq!(texture_mip_level_count(1024, 1024), 11);
    }

    #[test]
    fn rgba8_mip_chain_downsamples_layers_independently() {
        let data = [
            0, 10, 20, 255, 4, 10, 20, 255, 8, 10, 20, 255, 12, 10, 20, 255, 100, 20, 40, 128, 104,
            20, 40, 128, 108, 20, 40, 128, 112, 20, 40, 128,
        ];

        let chain = build_rgba8_mip_chain(2, 2, 2, &data).unwrap();

        assert_eq!(chain.len(), 2);
        assert_eq!(chain[1].width, 1);
        assert_eq!(chain[1].height, 1);
        assert_eq!(chain[1].data, vec![6, 10, 20, 255, 106, 20, 40, 128]);
    }

    #[test]
    fn rgba8_mip_chain_rejects_invalid_inputs() {
        assert_eq!(
            build_rgba8_mip_chain(0, 1, 1, &[]),
            Err(TextureMipError::InvalidShape)
        );
        assert_eq!(
            build_rgba8_mip_chain(u32::MAX, u32::MAX, 1, &[]),
            Err(TextureMipError::InvalidShape)
        );
        assert_eq!(
            build_rgba8_mip_chain(1, 1, 1, &[255]),
            Err(TextureMipError::InvalidDataLength {
                actual: 1,
                expected: 4
            })
        );
    }
}
