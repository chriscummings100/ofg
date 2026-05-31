import { getFloatsPerVertex, type MeshData } from "../world/terrainMesh.js";

type GpuMesh = {
  readonly vertexBuffer: GpuAny;
  readonly indexBuffer: GpuAny;
  readonly indexCount: number;
};

const SHADER_SOURCE = `
struct Camera {
  viewProjection: mat4x4<f32>,
};

@group(0) @binding(0) var<uniform> camera: Camera;

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
  output.clipPosition = camera.viewProjection * vec4<f32>(input.position, 1.0);
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
  private uniformBuffer: GpuAny = undefined;
  private bindGroup: GpuAny = undefined;
  private depthTexture: GpuAny = undefined;
  private terrainMesh: GpuMesh | undefined;
  private actorMesh: GpuMesh | undefined;
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
    this.uniformBuffer = this.device.createBuffer({
      label: "camera uniforms",
      size: 64,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });

    const shaderModule = this.device.createShaderModule({
      label: "seed terrain shader",
      code: SHADER_SOURCE
    });
    const bindGroupLayout = this.device.createBindGroupLayout({
      label: "camera bind group layout",
      entries: [
        {
          binding: 0,
          visibility: GPUShaderStage.VERTEX,
          buffer: { type: "uniform" }
        }
      ]
    });

    this.bindGroup = this.device.createBindGroup({
      label: "camera bind group",
      layout: bindGroupLayout,
      entries: [
        {
          binding: 0,
          resource: { buffer: this.uniformBuffer }
        }
      ]
    });

    this.pipeline = this.device.createRenderPipeline({
      label: "seed terrain pipeline",
      layout: this.device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] }),
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

  setTerrainMesh(mesh: MeshData): void {
    this.terrainMesh = this.createGpuMesh("terrain", mesh);
  }

  setActorMesh(mesh: MeshData | undefined): void {
    this.actorMesh = mesh === undefined ? undefined : this.createGpuMesh("actor", mesh);
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

  render(viewProjection: Float32Array): void {
    if (this.terrainMesh === undefined) {
      return;
    }

    this.resize();
    this.device.queue.writeBuffer(this.uniformBuffer, 0, viewProjection);

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
    pass.setBindGroup(0, this.bindGroup);
    this.drawMesh(pass, this.terrainMesh);

    if (this.actorMesh !== undefined) {
      this.drawMesh(pass, this.actorMesh);
    }

    pass.end();
    this.device.queue.submit([encoder.finish()]);
  }

  private createGpuMesh(label: string, mesh: MeshData): GpuMesh {
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

  private drawMesh(pass: GpuAny, mesh: GpuMesh): void {
    pass.setVertexBuffer(0, mesh.vertexBuffer);
    pass.setIndexBuffer(mesh.indexBuffer, "uint32");
    pass.drawIndexed(mesh.indexCount);
  }
}
