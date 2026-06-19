//! Browser-free render smoke for the OFG bootstrap renderer.

use std::{
    error::Error,
    fs::{self, File},
    path::{Path, PathBuf},
    sync::mpsc,
    time::Duration,
};

use ofg_render::{clear_color_rgba8, BootstrapRenderer};
use serde::{Deserialize, Serialize};

const BYTES_PER_PIXEL: u32 = 4;
const MAP_TIMEOUT: Duration = Duration::from_secs(15);
const TEXTURE_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba8Unorm;

#[derive(Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
struct SmokeContract {
    width: u32,
    height: u32,
    resize_probe_width: u32,
    resize_probe_height: u32,
    clear_color_rgba8: [u8; 4],
    sample_step: usize,
    color_distance_tolerance: f64,
    bucket_divisor: u8,
    min_triangle_ratio: f64,
    min_background_ratio: f64,
    min_non_background_color_buckets: usize,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct RenderSmokeReport {
    png_path: String,
    report_path: String,
    width: u32,
    height: u32,
    texture_format: String,
    adapter_name: String,
    backend: String,
    clear_color: [u8; 4],
    thresholds: SmokeThresholds,
    sampled_pixels: u32,
    triangle_pixels: u32,
    background_pixels: u32,
    triangle_ratio: f64,
    background_ratio: f64,
    non_background_color_buckets: usize,
    passed: bool,
    failure_reason: Option<String>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct SmokeThresholds {
    sample_step: usize,
    color_distance_tolerance: f64,
    bucket_divisor: u8,
    min_triangle_ratio: f64,
    min_background_ratio: f64,
    min_non_background_color_buckets: usize,
}

#[derive(Debug)]
struct PixelReport {
    sampled_pixels: u32,
    triangle_pixels: u32,
    background_pixels: u32,
    triangle_ratio: f64,
    background_ratio: f64,
    non_background_color_buckets: usize,
    failure_reason: Option<String>,
}

#[derive(Debug, Clone, Copy)]
struct RenderExtent {
    width: u32,
    height: u32,
}

fn main() {
    if let Err(error) = pollster::block_on(run()) {
        eprintln!("Native render smoke failed: {error}");
        std::process::exit(1);
    }
}

async fn run() -> Result<(), Box<dyn Error>> {
    let out_dir = parse_out_dir()?;
    fs::create_dir_all(&out_dir)?;
    let contract = read_smoke_contract()?;
    if contract.clear_color_rgba8 != clear_color_rgba8() {
        return Err(format!(
            "Smoke contract clear color {:?} does not match ofg_render {:?}.",
            contract.clear_color_rgba8,
            clear_color_rgba8()
        )
        .into());
    }

    let extent = RenderExtent {
        width: contract.width,
        height: contract.height,
    };
    let instance = wgpu::Instance::default();
    let adapter = instance
        .request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::HighPerformance,
            force_fallback_adapter: false,
            compatible_surface: None,
        })
        .await
        .map_err(|error| format!("No native WebGPU adapter was available: {error}"))?;
    let adapter_info = adapter.get_info();
    let backend = format!("{:?}", adapter_info.backend);
    let adapter_name = adapter_info.name;
    let (device, queue) = adapter
        .request_device(&wgpu::DeviceDescriptor {
            label: Some("ofg native smoke device"),
            required_features: wgpu::Features::empty(),
            required_limits: wgpu::Limits::downlevel_webgl2_defaults(),
            ..Default::default()
        })
        .await?;

    let renderer = BootstrapRenderer::new(&device, TEXTURE_FORMAT);
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("ofg native smoke texture"),
        size: wgpu::Extent3d {
            width: extent.width,
            height: extent.height,
            depth_or_array_layers: 1,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: TEXTURE_FORMAT,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT | wgpu::TextureUsages::COPY_SRC,
        view_formats: &[],
    });
    let view = texture.create_view(&wgpu::TextureViewDescriptor::default());

    let unpadded_bytes_per_row = extent.width * BYTES_PER_PIXEL;
    let padded_bytes_per_row = align_to(unpadded_bytes_per_row, wgpu::COPY_BYTES_PER_ROW_ALIGNMENT);
    let readback_size = padded_bytes_per_row as u64 * extent.height as u64;
    let readback_buffer = device.create_buffer(&wgpu::BufferDescriptor {
        label: Some("ofg native smoke readback buffer"),
        size: readback_size,
        usage: wgpu::BufferUsages::COPY_DST | wgpu::BufferUsages::MAP_READ,
        mapped_at_creation: false,
    });

    let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label: Some("ofg native smoke encoder"),
    });
    renderer.render_to_view(&mut encoder, &view);
    encoder.copy_texture_to_buffer(
        wgpu::TexelCopyTextureInfo {
            texture: &texture,
            mip_level: 0,
            origin: wgpu::Origin3d::ZERO,
            aspect: wgpu::TextureAspect::All,
        },
        wgpu::TexelCopyBufferInfo {
            buffer: &readback_buffer,
            layout: wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(padded_bytes_per_row),
                rows_per_image: Some(extent.height),
            },
        },
        wgpu::Extent3d {
            width: extent.width,
            height: extent.height,
            depth_or_array_layers: 1,
        },
    );

    let submission_index = queue.submit(std::iter::once(encoder.finish()));
    let pixels = read_pixels(
        &device,
        &readback_buffer,
        submission_index,
        unpadded_bytes_per_row,
        padded_bytes_per_row,
        extent,
        &adapter_name,
        &backend,
    )?;

    let png_path = out_dir.join("bootstrap.png");
    let report_path = out_dir.join("report.json");
    write_png(&png_path, &pixels, extent)?;
    let pixel_report = inspect_pixels(&pixels, extent, &contract)?;
    let passed = pixel_report.failure_reason.is_none();
    let report = build_report(
        &png_path,
        &report_path,
        extent,
        &adapter_name,
        &backend,
        &contract,
        &pixel_report,
        passed,
    );
    fs::write(
        &report_path,
        format!("{}\n", serde_json::to_string_pretty(&report)?),
    )?;

    println!("Native render smoke PNG: {}", png_path.display());
    println!("Native render smoke report: {}", report_path.display());

    if let Some(reason) = pixel_report.failure_reason {
        return Err(reason.into());
    }
    Ok(())
}

fn parse_out_dir() -> Result<PathBuf, Box<dyn Error>> {
    let mut args = std::env::args().skip(1);
    match (args.next().as_deref(), args.next(), args.next()) {
        (None, None, None) => Ok(PathBuf::from("artifacts/render-smoke")),
        (Some("--out"), Some(path), None) => Ok(PathBuf::from(path)),
        _ => Err("Usage: ofg-render-frame [--out artifacts/render-smoke]".into()),
    }
}

fn read_smoke_contract() -> Result<SmokeContract, Box<dyn Error>> {
    let path = Path::new("tools/smoke-contract.json");
    let contract: SmokeContract = serde_json::from_str(&fs::read_to_string(path)?)?;
    if contract.width == 0 || contract.height == 0 {
        return Err("Smoke contract dimensions must be non-zero.".into());
    }
    if contract.sample_step == 0 {
        return Err("Smoke contract sampleStep must be non-zero.".into());
    }
    if contract.bucket_divisor == 0 {
        return Err("Smoke contract bucketDivisor must be non-zero.".into());
    }
    Ok(contract)
}

fn read_pixels(
    device: &wgpu::Device,
    buffer: &wgpu::Buffer,
    submission_index: wgpu::SubmissionIndex,
    unpadded_bytes_per_row: u32,
    padded_bytes_per_row: u32,
    extent: RenderExtent,
    adapter_name: &str,
    backend: &str,
) -> Result<Vec<u8>, Box<dyn Error>> {
    let slice = buffer.slice(..);
    let (sender, receiver) = mpsc::channel();
    slice.map_async(wgpu::MapMode::Read, move |result| {
        let _ = sender.send(result);
    });
    device.poll(wgpu::PollType::Wait {
        submission_index: Some(submission_index),
        timeout: Some(MAP_TIMEOUT),
    })?;
    receiver.recv_timeout(MAP_TIMEOUT).map_err(|error| {
        format!(
            "Timed out waiting for GPU readback on adapter '{adapter_name}' backend {backend}: {error}"
        )
    })??;

    let mapped = slice.get_mapped_range();
    let expected_len = (unpadded_bytes_per_row * extent.height) as usize;
    let mut pixels = vec![0; expected_len];
    for row in 0..extent.height as usize {
        let src_start = row * padded_bytes_per_row as usize;
        let src_end = src_start + unpadded_bytes_per_row as usize;
        let dst_start = row * unpadded_bytes_per_row as usize;
        let dst_end = dst_start + unpadded_bytes_per_row as usize;
        pixels[dst_start..dst_end].copy_from_slice(&mapped[src_start..src_end]);
    }
    drop(mapped);
    buffer.unmap();
    Ok(pixels)
}

fn write_png(path: &Path, pixels: &[u8], extent: RenderExtent) -> Result<(), Box<dyn Error>> {
    let expected_len = (extent.width * extent.height * BYTES_PER_PIXEL) as usize;
    if pixels.len() != expected_len {
        return Err(format!(
            "Expected {expected_len} RGBA bytes for PNG, got {}.",
            pixels.len()
        )
        .into());
    }

    let file = File::create(path)?;
    let mut encoder = png::Encoder::new(file, extent.width, extent.height);
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header()?;
    writer.write_image_data(pixels)?;
    Ok(())
}

fn inspect_pixels(
    pixels: &[u8],
    extent: RenderExtent,
    contract: &SmokeContract,
) -> Result<PixelReport, Box<dyn Error>> {
    let expected_len = (extent.width * extent.height * BYTES_PER_PIXEL) as usize;
    if pixels.len() != expected_len {
        return Err(format!(
            "Expected {expected_len} RGBA bytes for inspection, got {}.",
            pixels.len()
        )
        .into());
    }

    let mut background_pixels = 0;
    let mut triangle_pixels = 0;
    let mut buckets = std::collections::BTreeSet::new();

    for y in (0..extent.height as usize).step_by(contract.sample_step) {
        for x in (0..extent.width as usize).step_by(contract.sample_step) {
            let index = (extent.width as usize * y + x) * BYTES_PER_PIXEL as usize;
            let pixel = [
                pixels[index],
                pixels[index + 1],
                pixels[index + 2],
                pixels[index + 3],
            ];
            if color_distance(pixel, contract.clear_color_rgba8)
                <= contract.color_distance_tolerance
            {
                background_pixels += 1;
            } else {
                triangle_pixels += 1;
                buckets.insert((
                    pixel[0] / contract.bucket_divisor,
                    pixel[1] / contract.bucket_divisor,
                    pixel[2] / contract.bucket_divisor,
                ));
            }
        }
    }

    let sampled_pixels = background_pixels + triangle_pixels;
    let triangle_ratio = triangle_pixels as f64 / sampled_pixels as f64;
    let background_ratio = background_pixels as f64 / sampled_pixels as f64;
    let mut failures = Vec::new();
    if triangle_ratio < contract.min_triangle_ratio {
        failures.push(format!("Triangle coverage too low: {triangle_ratio}"));
    }
    if background_ratio < contract.min_background_ratio {
        failures.push(format!("Background coverage too low: {background_ratio}"));
    }
    if buckets.len() < contract.min_non_background_color_buckets {
        failures.push(format!(
            "Expected at least {} non-background color buckets; got {}.",
            contract.min_non_background_color_buckets,
            buckets.len()
        ));
    }

    Ok(PixelReport {
        sampled_pixels,
        triangle_pixels,
        background_pixels,
        triangle_ratio,
        background_ratio,
        non_background_color_buckets: buckets.len(),
        failure_reason: (!failures.is_empty()).then(|| failures.join(" ")),
    })
}

fn build_report(
    png_path: &Path,
    report_path: &Path,
    extent: RenderExtent,
    adapter_name: &str,
    backend: &str,
    contract: &SmokeContract,
    pixel_report: &PixelReport,
    passed: bool,
) -> RenderSmokeReport {
    RenderSmokeReport {
        png_path: png_path.display().to_string(),
        report_path: report_path.display().to_string(),
        width: extent.width,
        height: extent.height,
        texture_format: format!("{TEXTURE_FORMAT:?}"),
        adapter_name: adapter_name.to_string(),
        backend: backend.to_string(),
        clear_color: clear_color_rgba8(),
        thresholds: SmokeThresholds {
            sample_step: contract.sample_step,
            color_distance_tolerance: contract.color_distance_tolerance,
            bucket_divisor: contract.bucket_divisor,
            min_triangle_ratio: contract.min_triangle_ratio,
            min_background_ratio: contract.min_background_ratio,
            min_non_background_color_buckets: contract.min_non_background_color_buckets,
        },
        sampled_pixels: pixel_report.sampled_pixels,
        triangle_pixels: pixel_report.triangle_pixels,
        background_pixels: pixel_report.background_pixels,
        triangle_ratio: pixel_report.triangle_ratio,
        background_ratio: pixel_report.background_ratio,
        non_background_color_buckets: pixel_report.non_background_color_buckets,
        passed,
        failure_reason: pixel_report.failure_reason.clone(),
    }
}

fn color_distance(left: [u8; 4], right: [u8; 4]) -> f64 {
    let dr = f64::from(left[0]) - f64::from(right[0]);
    let dg = f64::from(left[1]) - f64::from(right[1]);
    let db = f64::from(left[2]) - f64::from(right[2]);
    (dr * dr + dg * dg + db * db).sqrt()
}

fn align_to(value: u32, alignment: u32) -> u32 {
    value.div_ceil(alignment) * alignment
}
