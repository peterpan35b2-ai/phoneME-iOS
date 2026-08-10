import { unzipSync } from "fflate";
import type { JarMetadata } from "./types";

function decodeText(bytes: Uint8Array) {
  try {
    return new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch {
    return new TextDecoder("iso-8859-1").decode(bytes);
  }
}

function parseManifest(text: string) {
  const attributes = new Map<string, string>();
  let currentKey = "";

  for (const rawLine of text.replace(/\r\n?/g, "\n").split("\n")) {
    if (rawLine.startsWith(" ") && currentKey) {
      attributes.set(currentKey, `${attributes.get(currentKey) ?? ""}${rawLine.slice(1)}`);
      continue;
    }
    const separator = rawLine.indexOf(":");
    if (separator <= 0) {
      currentKey = "";
      continue;
    }
    currentKey = rawLine.slice(0, separator);
    attributes.set(currentKey, rawLine.slice(separator + 1).trim());
  }
  return attributes;
}

function extensionMime(path: string) {
  const extension = path.split(".").pop()?.toLowerCase();
  if (extension === "jpg" || extension === "jpeg") return "image/jpeg";
  if (extension === "gif") return "image/gif";
  if (extension === "webp") return "image/webp";
  return "image/png";
}

function toDataUrl(bytes: Uint8Array, mime: string) {
  if (bytes.byteLength > 512 * 1024) return undefined;
  let binary = "";
  const chunkSize = 0x8000;
  for (let offset = 0; offset < bytes.length; offset += chunkSize) {
    binary += String.fromCharCode(...bytes.subarray(offset, offset + chunkSize));
  }
  return `data:${mime};base64,${btoa(binary)}`;
}

export async function readJarMetadata(file: File): Promise<JarMetadata> {
  const MAX_JAR_BYTES = 64 * 1024 * 1024;
  const MAX_MANIFEST_BYTES = 1024 * 1024;
  if (file.size <= 0 || file.size > MAX_JAR_BYTES) {
    throw new Error("JAR quá lớn để xử lý an toàn trong trình duyệt");
  }
  const jarBytes = new Uint8Array(await file.arrayBuffer());
  const manifestArchive = unzipSync(jarBytes, {
    filter: (entry) =>
      entry.name.toLowerCase() === "meta-inf/manifest.mf" &&
      entry.originalSize <= MAX_MANIFEST_BYTES
  });
  const manifestPair = Object.entries(manifestArchive)[0];
  if (!manifestPair) throw new Error("JAR không có META-INF/MANIFEST.MF hợp lệ");
  const [, manifestBytes] = manifestPair;

  const attributes = parseManifest(decodeText(manifestBytes));
  const midlet = (attributes.get("MIDlet-1") ?? "")
    .split(",")
    .map((part) => part.trim());
  const mainClass = midlet[2] ?? "";
  if (!mainClass) throw new Error("Manifest không khai báo MIDlet-1 hợp lệ");

  const title = attributes.get("MIDlet-Name")?.trim() || midlet[0] || file.name.replace(/\.jar$/i, "");
  const vendor = attributes.get("MIDlet-Vendor")?.trim() || "Không rõ nhà phát hành";
  const version = attributes.get("MIDlet-Version")?.trim() || "";
  const rawIconPath = midlet[1]?.replace(/^\//, "").toLowerCase();
  let iconDataUrl: string | undefined;
  if (rawIconPath) {
    const iconArchive = unzipSync(jarBytes, {
      filter: (entry) => entry.name.toLowerCase() === rawIconPath && entry.originalSize <= 512 * 1024
    });
    const iconPair = Object.entries(iconArchive)[0];
    if (iconPair) iconDataUrl = toDataUrl(iconPair[1], extensionMime(iconPair[0]));
  }

  return {
    title,
    vendor,
    version,
    mainClass,
    iconDataUrl
  };
}
