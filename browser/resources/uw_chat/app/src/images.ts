// Images the user adds to a message, as the browser takes them: at most
// 1,568 px on the long side, as PNG when they came as PNG and JPEG otherwise.
// See the agent chats plan's agreed scope.

export const MAX_IMAGES = 5;
export const IMAGE_TYPES = "image/png,image/jpeg,image/webp,image/gif";

const LONG_SIDE = 1568;

export async function imageData(file: File): Promise<string> {
  const bitmap = await createImageBitmap(file);
  const scale = Math.min(1, LONG_SIDE / Math.max(bitmap.width, bitmap.height));
  const canvas = new OffscreenCanvas(
    Math.max(1, Math.round(bitmap.width * scale)),
    Math.max(1, Math.round(bitmap.height * scale)),
  );
  canvas.getContext("2d")!.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
  bitmap.close();
  const blob = await canvas.convertToBlob({
    type: file.type === "image/png" ? "image/png" : "image/jpeg",
    quality: 0.85,
  });
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result as string);
    reader.onerror = () => reject(reader.error);
    reader.readAsDataURL(blob);
  });
}
