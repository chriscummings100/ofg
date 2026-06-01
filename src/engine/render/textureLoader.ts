import { Texture } from "./Texture.js";

export async function loadRgbaTextureFromUrl(
  id: string,
  url: string
): Promise<Texture> {
  const image = await loadImageBitmap(url);
  try {
    return textureFromRgbaPixels(id, image.width, image.height, imageBitmapToRgba(image, url));
  } finally {
    image.close();
  }
}

export async function loadRgbaTextureArrayFromUrls(
  id: string,
  urls: readonly string[]
): Promise<Texture> {
  if (urls.length === 0) {
    throw new Error("Texture array must contain at least one URL.");
  }

  const images = await Promise.all(urls.map(loadImageBitmap));
  try {
    const first = images[0];
    const bytesPerLayer = first.width * first.height * 4;
    const data = new Uint8Array(bytesPerLayer * images.length);

    for (let layer = 0; layer < images.length; layer += 1) {
      const image = images[layer];
      if (image.width !== first.width || image.height !== first.height) {
        throw new Error(
          `Texture array '${id}' layer ${layer} has dimensions ` +
          `${image.width}x${image.height}; expected ${first.width}x${first.height}.`
        );
      }

      data.set(imageBitmapToRgba(image, urls[layer]), layer * bytesPerLayer);
    }

    return new Texture(id, first.width, first.height, "rgba8unorm", {
      data,
      layers: images.length
    });
  } finally {
    for (const image of images) {
      image.close();
    }
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

async function loadImageBitmap(url: string): Promise<ImageBitmap> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Failed to load texture '${url}': ${response.status} ${response.statusText}`);
  }

  return createImageBitmap(await response.blob());
}

function imageBitmapToRgba(image: ImageBitmap, url: string): Uint8Array {
  const canvas = document.createElement("canvas");
  canvas.width = image.width;
  canvas.height = image.height;

  const context = canvas.getContext("2d", { willReadFrequently: true });
  if (context === null) {
    throw new Error(`Failed to create 2D canvas context for texture '${url}'.`);
  }

  context.drawImage(image, 0, 0);
  return new Uint8Array(context.getImageData(0, 0, image.width, image.height).data);
}
