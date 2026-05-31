import { Texture } from "./Texture.js";

export async function loadRgbaTextureFromUrl(
  id: string,
  url: string
): Promise<Texture> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to load texture '${url}': ${response.status} ${response.statusText}`);
  }

  const image = await createImageBitmap(await response.blob());
  try {
    const canvas = document.createElement("canvas");
    canvas.width = image.width;
    canvas.height = image.height;

    const context = canvas.getContext("2d", { willReadFrequently: true });
    if (context === null) {
      throw new Error(`Failed to create 2D canvas context for texture '${url}'.`);
    }

    context.drawImage(image, 0, 0);
    const imageData = context.getImageData(0, 0, image.width, image.height);

    return textureFromRgbaPixels(id, image.width, image.height, imageData.data);
  } finally {
    image.close();
  }
}

export function textureFromRgbaPixels(
  id: string,
  width: number,
  height: number,
  data: Uint8Array | Uint8ClampedArray
): Texture {
  return new Texture(id, width, height, "rgba8unorm", {
    data: data instanceof Uint8Array ? data : new Uint8Array(data)
  });
}
