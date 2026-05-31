type GpuAny = any;

interface Navigator {
  gpu?: GpuAny;
}

declare const GPUBufferUsage: {
  VERTEX: number;
  INDEX: number;
  UNIFORM: number;
  COPY_DST: number;
};

declare const GPUTextureUsage: {
  RENDER_ATTACHMENT: number;
  TEXTURE_BINDING: number;
  COPY_DST: number;
};

declare const GPUShaderStage: {
  VERTEX: number;
  FRAGMENT: number;
};
