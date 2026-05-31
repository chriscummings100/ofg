import { getFloatsPerVertex } from "../world/terrainMesh.js";
import type { Mesh } from "./Mesh.js";
import type { RenderItem, RenderWorld } from "./RenderWorld.js";

type GpuMesh = {
  readonly vertexBuffer: GpuAny;
  readonly indexBuffer: GpuAny;
  readonly indexCount: number;
};

type GpuObject = {
  readonly uniformBuffer: GpuAny;
  readonly bindGroup: GpuAny;
};

const SHADER_SOURCE = `
struct Camera {
  viewProjection: mat4x4<f32>,
};

struct ObjectUniforms {
  world: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> camera: Camera;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;

struct VertexInput {
  @location(0) position: vec3<f32>,
  @location(1) color: vec3<f32>,
};

struct VertexOutput {
  @builtin(position) clipPosition: vec4<f32>,
  @location(0) color: vec3<f32>,
};

@vertex
fn vertexMain(input: VertexInput) -> VertexOutput {
  var output: VertexOutput;
  output.clipPosition = camera.viewProjection * object.world * vec4<f32>(input.position, 1.0);
  output.color = input.color;
  return output;
}

@fragment
fn fragmentMain(input: VertexOutput) -> @location(0) vec4<f32> {
  return vec4<f32>(input.color, 1.0);
}
`;

export class WebGpuRenderer {
  private readonly canvas: HTMLCanvasElement;
  private context: GpuAny = undefined;
  private device: GpuAny = undefined;
  private format = "";
  private pipeline: GpuAny = undefined;
  private cameraUniformBuffer: GpuAny = undefined;
  private cameraBindGroup: GpuAny = undefined;
  private objectBindGroupLayout: GpuAny = undefined;
  private depthTexture: GpuAny = undefined;
  private readonly meshCache = new WeakMap<Mesh, GpuMesh>();
  private readonly objectUniforms = new Map<string, GpuObject>();
  private width = 0;
  private height = 0;

  constructor(canvas: HTMLCanvasElement) {
    this.canvas = canvas;
  }

  async initialize(): Promise<void> {
    const gpu = navigator.gpu;
    if (gpu === undefined) {
      throw new Error("WebGPU is not available in this browser.");
    }

    const adapter = await gpu.requestAdapter();
    if (adapter === null) {
      throw new Error("No WebGPU adapter is available.");
    }

    this.device = await adapter.requestDevice();
    const context = this.canvas.getContext("webgpu");
    if (context === null) {
      throw new Error("Unable to create a WebGPU canvas context.");
    }

    this.context = context;
    this.format = gpu.getPreferredCanvasFormat();
    this.cameraUniformBuffer = this.device.createBuffer({
      label: "camera uniforms",
      size: 64,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });

    const shaderModule = this.device.createShaderModule({
      label: "seed terrain shader",
      code: SHADER_SOURCE
    });
    const cameraBindGroupLayout = this.device.createBindGroupLayout({
      label: "camera bind group layout",
      entries: [
        {
          binding: 0,
          visibility: GPUShaderStage.VERTEX,
          buffer: { type: "uniform" }
        }
      ]
    });
    this.objectBindGroupLayout = this.device.createBindGroupLayout({
      label: "object bind group layout",
      entries: [
        {
          binding: 0,
          visibility: GPUShaderStage.VERTEX,
          buffer: { type: "uniform" }
        }
      ]
    });

    this.cameraBindGroup = this.device.createBindGroup({
      label: "camera bind group",
      layout: cameraBindGroupLayout,
      entries: [
        {
          binding: 0,
          resource: { buffer: this.cameraUniformBuffer }
        }
      ]
    });

    this.pipeline = this.device.createRenderPipeline({
      label: "seed terrain pipeline",
      layout: this.device.createPipelineLayout({
        bindGroupLayouts: [cameraBindGroupLayout, this.objectBindGroupLayout]
      }),
      vertex: {
        module: shaderModule,
        entryPoint: "vertexMain",
        buffers: [
          {
            arrayStride: getFloatsPerVertex() * Float32Array.BYTES_PER_ELEMENT,
            attributes: [
              { shaderLocation: 0, offset: 0, format: "float32x3" },
              {
                shaderLocation: 1,
                offset: 3 * Float32Array.BYTES_PER_ELEMENT,
                format: "float32x3"
              }
            ]
          }
        ]
      },
      fragment: {
        module: shaderModule,
        entryPoint: "fragmentMain",
        targets: [{ format: this.format }]
      },
      primitive: {
        topology: "triangle-list",
        cullMode: "back"
      },
      depthStencil: {
        format: "depth24plus",
        depthWriteEnabled: true,
        depthCompare: "less"
      }
    });

    this.resize();
  }

  resize(): void {
    const pixelRatio = Math.min(window.devicePixelRatio || 1, 2);
    const displayWidth = Math.max(1, Math.floor(this.canvas.clientWidth * pixelRatio));
    const displayHeight = Math.max(1, Math.floor(this.canvas.clientHeight * pixelRatio));

    if (displayWidth === this.width && displayHeight === this.height) {
      return;
    }

    this.width = displayWidth;
    this.height = displayHeight;
    this.canvas.width = displayWidth;
    this.canvas.height = displayHeight;
    this.context.configure({
      device: this.device,
      format: this.format,
      alphaMode: "opaque"
    });
    this.depthTexture = this.device.createTexture({
      label: "depth texture",
      size: [displayWidth, displayHeight],
      format: "depth24plus",
      usage: GPUTextureUsage.RENDER_ATTACHMENT
    });
  }

  getAspectRatio(): number {
    return this.width / this.height;
  }

  render(renderWorld: RenderWorld): void {
    this.resize();
    this.device.queue.writeBuffer(
      this.cameraUniformBuffer,
      0,
      renderWorld.camera.viewProjection
    );

    const encoder = this.device.createCommandEncoder({ label: "frame encoder" });
    const pass = encoder.beginRenderPass({
      colorAttachments: [
        {
          view: this.context.getCurrentTexture().createView(),
          clearValue: { r: 0.08, g: 0.09, b: 0.08, a: 1 },
          loadOp: "clear",
          storeOp: "store"
        }
      ],
      depthStencilAttachment: {
        view: this.depthTexture.createView(),
        depthClearValue: 1,
        depthLoadOp: "clear",
        depthStoreOp: "store"
      }
    });

    pass.setPipeline(this.pipeline);
    pass.setBindGroup(0, this.cameraBindGroup);
    for (const item of renderWorld.items) {
      this.drawItem(pass, item);
    }

    pass.end();
    this.device.queue.submit([encoder.finish()]);
  }

  private getGpuMesh(mesh: Mesh): GpuMesh {
    const cached = this.meshCache.get(mesh);
    if (cached !== undefined) {
      return cached;
    }

    const gpuMesh = this.createGpuMesh(mesh);
    this.meshCache.set(mesh, gpuMesh);
    return gpuMesh;
  }

  private createGpuMesh(mesh: Mesh): GpuMesh {
    if (mesh.layout.floatsPerVertex !== getFloatsPerVertex()) {
      throw new Error(
        `WebGpuRenderer only supports ${getFloatsPerVertex()} floats per vertex; ` +
        `mesh '${mesh.id}' uses ${mesh.layout.floatsPerVertex}.`
      );
    }

    const label = mesh.id;
    const vertexBuffer = this.createBuffer(`${label} vertices`, mesh.vertices, GPUBufferUsage.VERTEX);
    const indexBuffer = this.createBuffer(`${label} indices`, mesh.indices, GPUBufferUsage.INDEX);

    return {
      vertexBuffer,
      indexBuffer,
      indexCount: mesh.indices.length
    };
  }

  private createBuffer(label: string, data: Float32Array | Uint32Array, usage: number): GpuAny {
    const buffer = this.device.createBuffer({
      label,
      size: data.byteLength,
      usage,
      mappedAtCreation: true
    });
    const mappedRange = buffer.getMappedRange();

    if (data instanceof Float32Array) {
      new Float32Array(mappedRange).set(data);
    } else {
      new Uint32Array(mappedRange).set(data);
    }

    buffer.unmap();
    return buffer;
  }

  private getGpuObject(item: RenderItem): GpuObject {
    const cached = this.objectUniforms.get(item.id);
    if (cached !== undefined) {
      return cached;
    }

    const uniformBuffer = this.device.createBuffer({
      label: `${item.id} object uniforms`,
      size: 64,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    const bindGroup = this.device.createBindGroup({
      label: `${item.id} object bind group`,
      layout: this.objectBindGroupLayout,
      entries: [
        {
          binding: 0,
          resource: { buffer: uniformBuffer }
        }
      ]
    });
    const gpuObject = { uniformBuffer, bindGroup };
    this.objectUniforms.set(item.id, gpuObject);
    return gpuObject;
  }

  private drawItem(pass: GpuAny, item: RenderItem): void {
    const mesh = this.getGpuMesh(item.mesh);
    const object = this.getGpuObject(item);

    this.device.queue.writeBuffer(object.uniformBuffer, 0, item.worldMatrix);
    pass.setBindGroup(1, object.bindGroup);
    pass.setVertexBuffer(0, mesh.vertexBuffer);
    pass.setIndexBuffer(mesh.indexBuffer, "uint32");
    pass.drawIndexed(mesh.indexCount);
  }
}
