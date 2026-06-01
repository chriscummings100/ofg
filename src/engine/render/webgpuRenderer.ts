import { UBER_SHADER_METADATA, UBER_SHADER_SOURCE } from "../../generated/render/uberShader.js";
import { getFloatsPerVertex } from "../world/terrainMesh.js";
import { FRAME_UNIFORM_BYTES, FRAME_UNIFORM_FLOATS, buildFrameUniformValues } from "./FrameUniforms.js";
import type { Mesh } from "./Mesh.js";
import { OBJECT_UNIFORM_BYTES, OBJECT_UNIFORM_FLOATS, buildObjectUniformValues } from "./ObjectUniforms.js";
import type { RenderItem, RenderWorld } from "./RenderWorld.js";
import { Texture } from "./Texture.js";

type GpuMesh = {
  readonly vertexBuffer: GpuAny;
  readonly indexBuffer: GpuAny;
  readonly indexCount: number;
};

type GpuObject = {
  readonly uniformBuffer: GpuAny;
  readonly uniformValues: Float32Array;
  readonly bindGroup: GpuAny;
  readonly albedoTexture: GpuTexture;
  readonly normalTexture: GpuTexture;
  readonly materialTexture: GpuTexture;
};

type GpuTexture = {
  readonly texture: GpuAny;
  readonly view: GpuAny;
};

const FALLBACK_ALBEDO_TEXTURE = new Texture(
  "texture:fallback.white",
  1,
  1,
  "rgba8unorm",
  { data: new Uint8Array([255, 255, 255, 255]) }
);
const FALLBACK_NORMAL_TEXTURE = new Texture(
  "texture:fallback.normal",
  1,
  1,
  "rgba8unorm",
  { data: new Uint8Array([128, 128, 255, 255]) }
);
const FALLBACK_MATERIAL_TEXTURE = new Texture(
  "texture:fallback.material",
  1,
  1,
  "rgba8unorm",
  { data: new Uint8Array([0, 255, 255, 128]) }
);

export class WebGpuRenderer {
  private readonly canvas: HTMLCanvasElement;
  private context: GpuAny = undefined;
  private device: GpuAny = undefined;
  private format = "";
  private skyPipeline: GpuAny = undefined;
  private pipeline: GpuAny = undefined;
  private cameraUniformBuffer: GpuAny = undefined;
  private cameraBindGroup: GpuAny = undefined;
  private objectBindGroupLayout: GpuAny = undefined;
  private albedoSampler: GpuAny = undefined;
  private fallbackAlbedoTexture: GpuTexture | undefined;
  private fallbackNormalTexture: GpuTexture | undefined;
  private fallbackMaterialTexture: GpuTexture | undefined;
  private depthTexture: GpuAny = undefined;
  private readonly meshCache = new WeakMap<Mesh, GpuMesh>();
  private readonly textureCache = new WeakMap<Texture, GpuTexture>();
  private readonly objectUniforms = new Map<string, GpuObject>();
  private readonly frameUniformValues = new Float32Array(FRAME_UNIFORM_FLOATS);
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

    if (adapter.limits.maxTextureArrayLayers < 16) {
      throw new Error(
        `WebGPU adapter only supports ${adapter.limits.maxTextureArrayLayers} texture array layers; ` +
        "terrain materials require at least 16."
      );
    }

    this.device = await adapter.requestDevice({
      requiredLimits: {
        maxTextureArrayLayers: 16
      }
    });
    const context = this.canvas.getContext("webgpu");
    if (context === null) {
      throw new Error("Unable to create a WebGPU canvas context.");
    }

    this.context = context;
    this.format = gpu.getPreferredCanvasFormat();
    this.cameraUniformBuffer = this.device.createBuffer({
      label: "camera uniforms",
      size: FRAME_UNIFORM_BYTES,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });

    const shaderModule = this.device.createShaderModule({
      label: `${UBER_SHADER_METADATA.id} shader`,
      code: UBER_SHADER_SOURCE
    });
    const cameraBindGroupLayout = this.device.createBindGroupLayout({
      label: "camera bind group layout",
      entries: [
        {
          binding: 0,
          visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
          buffer: { type: "uniform" }
        }
      ]
    });
    this.albedoSampler = this.device.createSampler({
      label: "albedo sampler",
      addressModeU: "repeat",
      addressModeV: "repeat",
      magFilter: "linear",
      minFilter: "linear"
    });
    this.objectBindGroupLayout = this.device.createBindGroupLayout({
      label: "object bind group layout",
      entries: [
        {
          binding: 0,
          visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT,
          buffer: { type: "uniform" }
        },
        {
          binding: 1,
          visibility: GPUShaderStage.FRAGMENT,
          texture: { sampleType: "float", viewDimension: "2d-array" }
        },
        {
          binding: 2,
          visibility: GPUShaderStage.FRAGMENT,
          texture: { sampleType: "float", viewDimension: "2d-array" }
        },
        {
          binding: 3,
          visibility: GPUShaderStage.FRAGMENT,
          texture: { sampleType: "float", viewDimension: "2d-array" }
        },
        {
          binding: 4,
          visibility: GPUShaderStage.FRAGMENT,
          sampler: { type: "filtering" }
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
        entryPoint: UBER_SHADER_METADATA.vertexEntryPoint,
        buffers: [
          {
            arrayStride: getFloatsPerVertex() * Float32Array.BYTES_PER_ELEMENT,
            attributes: [
              { shaderLocation: 0, offset: 0, format: "float32x3" },
              {
                shaderLocation: 1,
                offset: 3 * Float32Array.BYTES_PER_ELEMENT,
                format: "float32x3"
              },
              {
                shaderLocation: 2,
                offset: 6 * Float32Array.BYTES_PER_ELEMENT,
                format: "float32x3"
              },
              {
                shaderLocation: 3,
                offset: 9 * Float32Array.BYTES_PER_ELEMENT,
                format: "float32x2"
              },
              {
                shaderLocation: 4,
                offset: 11 * Float32Array.BYTES_PER_ELEMENT,
                format: "float32x4"
              },
              {
                shaderLocation: 5,
                offset: 15 * Float32Array.BYTES_PER_ELEMENT,
                format: "float32x4"
              }
            ]
          }
        ]
      },
      fragment: {
        module: shaderModule,
        entryPoint: UBER_SHADER_METADATA.fragmentEntryPoint,
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
    this.skyPipeline = this.device.createRenderPipeline({
      label: "sky pipeline",
      layout: this.device.createPipelineLayout({ bindGroupLayouts: [cameraBindGroupLayout] }),
      vertex: {
        module: shaderModule,
        entryPoint: UBER_SHADER_METADATA.skyVertexEntryPoint
      },
      fragment: {
        module: shaderModule,
        entryPoint: UBER_SHADER_METADATA.skyFragmentEntryPoint,
        targets: [{ format: this.format }]
      },
      primitive: {
        topology: "triangle-list",
        cullMode: "none"
      },
      depthStencil: {
        format: "depth24plus",
        depthWriteEnabled: false,
        depthCompare: "always"
      }
    });
    this.fallbackAlbedoTexture = this.createGpuTexture(FALLBACK_ALBEDO_TEXTURE);
    this.fallbackNormalTexture = this.createGpuTexture(FALLBACK_NORMAL_TEXTURE);
    this.fallbackMaterialTexture = this.createGpuTexture(FALLBACK_MATERIAL_TEXTURE);

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
      buildFrameUniformValues(renderWorld.camera, renderWorld.mainLight, this.frameUniformValues)
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

    pass.setBindGroup(0, this.cameraBindGroup);
    pass.setPipeline(this.skyPipeline);
    pass.draw(3);

    pass.setPipeline(this.pipeline);
    const seenItemIds = new Set<string>();
    for (const item of renderWorld.items) {
      seenItemIds.add(item.id);
      this.drawItem(pass, item);
    }
    this.pruneObjectUniforms(seenItemIds);

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

  private getGpuTexture(texture?: Texture): GpuTexture {
    if (texture === undefined) {
      if (this.fallbackAlbedoTexture === undefined) {
        throw new Error("Fallback albedo texture has not been initialized.");
      }

      return this.fallbackAlbedoTexture;
    }

    const cached = this.textureCache.get(texture);
    if (cached !== undefined) {
      return cached;
    }

    const gpuTexture = this.createGpuTexture(texture);
    this.textureCache.set(texture, gpuTexture);
    return gpuTexture;
  }

  private createGpuTexture(texture: Texture): GpuTexture {
    const gpuTexture = this.device.createTexture({
      label: texture.id,
      size: [texture.width, texture.height, texture.layers],
      format: texture.format,
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
    });
    const data = texture.data ?? createOpaqueWhiteTextureData(texture.width, texture.height, texture.layers);
    const bytesPerLayer = texture.width * texture.height * 4;
    for (let layer = 0; layer < texture.layers; layer += 1) {
      this.device.queue.writeTexture(
        { texture: gpuTexture, origin: [0, 0, layer] },
        data.subarray(layer * bytesPerLayer, (layer + 1) * bytesPerLayer),
        {
          bytesPerRow: texture.width * 4,
          rowsPerImage: texture.height
        },
        [texture.width, texture.height, 1]
      );
    }

    return {
      texture: gpuTexture,
      view: gpuTexture.createView({
        dimension: "2d-array",
        arrayLayerCount: texture.layers
      })
    };
  }

  private getGpuObject(item: RenderItem): GpuObject {
    const albedoTexture = this.getGpuTexture(item.albedoTexture);
    const normalTexture = this.getGpuTextureOrFallback(item.normalTexture, this.fallbackNormalTexture, "normal");
    const materialTexture = this.getGpuTextureOrFallback(item.materialTexture, this.fallbackMaterialTexture, "material");
    const cached = this.objectUniforms.get(item.id);
    if (
      cached !== undefined &&
      cached.albedoTexture === albedoTexture &&
      cached.normalTexture === normalTexture &&
      cached.materialTexture === materialTexture
    ) {
      return cached;
    }

    const uniformBuffer = cached?.uniformBuffer ?? this.device.createBuffer({
      label: `${item.id} object uniforms`,
      size: OBJECT_UNIFORM_BYTES,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    const uniformValues = cached?.uniformValues ?? new Float32Array(OBJECT_UNIFORM_FLOATS);
    const bindGroup = this.device.createBindGroup({
      label: `${item.id} object bind group`,
      layout: this.objectBindGroupLayout,
      entries: [
        {
          binding: 0,
          resource: { buffer: uniformBuffer }
        },
        {
          binding: 1,
          resource: albedoTexture.view
        },
        {
          binding: 2,
          resource: normalTexture.view
        },
        {
          binding: 3,
          resource: materialTexture.view
        },
        {
          binding: 4,
          resource: this.albedoSampler
        }
      ]
    });
    const gpuObject = {
      uniformBuffer,
      uniformValues,
      bindGroup,
      albedoTexture,
      normalTexture,
      materialTexture
    };
    this.objectUniforms.set(item.id, gpuObject);
    return gpuObject;
  }

  private getGpuTextureOrFallback(
    texture: Texture | undefined,
    fallback: GpuTexture | undefined,
    label: string
  ): GpuTexture {
    if (texture !== undefined) {
      return this.getGpuTexture(texture);
    }

    if (fallback === undefined) {
      throw new Error(`Fallback ${label} texture has not been initialized.`);
    }

    return fallback;
  }

  private pruneObjectUniforms(seenItemIds: Set<string>): void {
    for (const [id, object] of this.objectUniforms) {
      if (seenItemIds.has(id)) {
        continue;
      }

      object.uniformBuffer.destroy?.();
      this.objectUniforms.delete(id);
    }
  }

  private drawItem(pass: GpuAny, item: RenderItem): void {
    const mesh = this.getGpuMesh(item.mesh);
    const object = this.getGpuObject(item);

    this.device.queue.writeBuffer(
      object.uniformBuffer,
      0,
      buildObjectUniformValues(item.worldMatrix, item.material, object.uniformValues)
    );
    pass.setBindGroup(1, object.bindGroup);
    pass.setVertexBuffer(0, mesh.vertexBuffer);
    pass.setIndexBuffer(mesh.indexBuffer, "uint32");
    pass.drawIndexed(mesh.indexCount);
  }
}

function createOpaqueWhiteTextureData(width: number, height: number, layers: number): Uint8Array {
  const data = new Uint8Array(width * height * layers * 4);
  for (let offset = 0; offset < data.length; offset += 4) {
    data[offset] = 255;
    data[offset + 1] = 255;
    data[offset + 2] = 255;
    data[offset + 3] = 255;
  }

  return data;
}
