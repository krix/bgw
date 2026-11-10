// Small dependency-free loader for the original 8-bit and 24-bit BMP assets
// and the 000.map tile file. The browser's image decoder cannot be relied on
// for palette BMPs with a black colour key, so decode the pixels here and turn
// them into canvases ourselves.

export async function fetchBinary(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`cannot load ${url}: HTTP ${response.status}`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

export function decodeBmp(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint16(0, true) !== 0x4d42) {
    throw new Error("not a BMP file");
  }
  const dataOffset = view.getUint32(10, true);
  const headerSize = view.getUint32(14, true);
  const width = view.getInt32(18, true);
  const rawHeight = view.getInt32(22, true);
  const bitsPerPixel = view.getUint16(28, true);
  const compression = view.getUint32(30, true);
  if (compression !== 0) {
    throw new Error(`unsupported BMP compression ${compression}`);
  }
  if (bitsPerPixel !== 8 && bitsPerPixel !== 24 && bitsPerPixel !== 32) {
    throw new Error(`unsupported BMP depth ${bitsPerPixel}`);
  }
  const height = Math.abs(rawHeight);
  const topDown = rawHeight < 0;
  const rowSize = Math.floor((bitsPerPixel * width + 31) / 32) * 4;
  const paletteOffset = 14 + headerSize;
  const paletteCount = bitsPerPixel === 8
    ? (view.getUint32(46, true) || 256)
    : 0;

  const data = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    const sourceY = topDown ? y : height - 1 - y;
    const row = dataOffset + sourceY * rowSize;
    for (let x = 0; x < width; x += 1) {
      let r = 0;
      let g = 0;
      let b = 0;
      let a = 255;
      if (bitsPerPixel === 8) {
        const index = bytes[row + x];
        if (index < paletteCount) {
          const p = paletteOffset + index * 4;
          b = bytes[p];
          g = bytes[p + 1];
          r = bytes[p + 2];
        }
      } else if (bitsPerPixel === 24) {
        const p = row + x * 3;
        b = bytes[p];
        g = bytes[p + 1];
        r = bytes[p + 2];
      } else {
        const p = row + x * 4;
        b = bytes[p];
        g = bytes[p + 1];
        r = bytes[p + 2];
        a = bytes[p + 3];
      }
      const out = (y * width + x) * 4;
      data[out] = r;
      data[out + 1] = g;
      data[out + 2] = b;
      data[out + 3] = a;
    }
  }
  return { width, height, data };
}

export function toCanvas(image, { keyBlack = false } = {}) {
  const canvas = document.createElement("canvas");
  canvas.width = image.width;
  canvas.height = image.height;
  const context = canvas.getContext("2d");
  const imageData = new ImageData(image.data, image.width, image.height);
  if (keyBlack) {
    const data = imageData.data;
    for (let i = 0; i < data.length; i += 4) {
      if (data[i] === 0 && data[i + 1] === 0 && data[i + 2] === 0) {
        data[i + 3] = 0;
      }
    }
  }
  context.putImageData(imageData, 0, 0);
  return canvas;
}

export function parseMap(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const columns = view.getInt32(0, true);
  const rows = view.getInt32(4, true);
  const tileWidth = view.getInt32(8, true);
  const tileHeight = view.getInt32(12, true);
  const tiles = new Uint16Array(columns * rows);
  for (let i = 0; i < tiles.length; i += 1) {
    const offset = 24 + i * 2;
    tiles[i] = offset + 1 < bytes.length ? view.getUint16(offset, true) : 0;
  }
  return { columns, rows, tileWidth, tileHeight, tiles };
}
