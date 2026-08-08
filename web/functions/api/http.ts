const MAX_BODY_BYTES = 64 * 1024 * 1024;
const MAX_HEADER_BYTES = 64 * 1024;
const encoder = new TextEncoder();
const decoder = new TextDecoder();

function readU32(view: DataView, offset: number) {
  return view.getUint32(offset, true);
}

function writeU32(view: DataView, offset: number, value: number) {
  view.setUint32(offset, value >>> 0, true);
}

function isPrivateLiteral(hostname: string) {
  const host = hostname.toLowerCase().replace(/^\[|\]$/g, "");
  if (host === "localhost" || host === "::1" || host === "0.0.0.0") return true;
  if (host.startsWith("fc") || host.startsWith("fd") || host.startsWith("fe8") || host.startsWith("fe9") || host.startsWith("fea") || host.startsWith("feb")) return true;
  const match = host.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/);
  if (!match) return false;
  const octets = match.slice(1).map(Number);
  if (octets.some((value) => value < 0 || value > 255)) return true;
  return octets[0] === 10 || octets[0] === 127 ||
    (octets[0] === 169 && octets[1] === 254) ||
    (octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31) ||
    (octets[0] === 192 && octets[1] === 168);
}

function parseHeaderBlock(value: string) {
  const headers = new Headers();
  for (const line of value.split(/\r?\n/)) {
    if (!line) continue;
    const separator = line.indexOf(":");
    if (separator <= 0) continue;
    const name = line.slice(0, separator).trim();
    const normalized = name.toLowerCase();
    if (!name || normalized === "host" || normalized === "content-length" ||
        normalized === "connection" || normalized === "transfer-encoding" ||
        normalized.startsWith("proxy-") || normalized.startsWith("sec-")) {
      continue;
    }
    const valuePart = line.slice(separator + 1).trim();
    try { headers.append(name, valuePart); } catch { /* Ignore invalid legacy headers. */ }
  }
  return headers;
}

function serializeHeaders(headers: Headers) {
  let serialized = "";
  headers.forEach((value, name) => {
    const line = `${name}: ${value}\r\n`;
    if (serialized.length + line.length <= MAX_HEADER_BYTES) serialized += line;
  });
  const getSetCookie = (headers as Headers & { getSetCookie?: () => string[] }).getSetCookie;
  if (typeof getSetCookie === "function") {
    for (const cookie of getSetCookie.call(headers)) {
      const line = `set-cookie: ${cookie}\r\n`;
      if (serialized.length + line.length <= MAX_HEADER_BYTES) serialized += line;
    }
  }
  return serialized;
}

function encodeResponse(status: number, finalUrl: string, reason: string, headers: string, body: Uint8Array) {
  const finalUrlBytes = encoder.encode(finalUrl);
  const reasonBytes = encoder.encode(reason);
  const headerBytes = encoder.encode(headers);
  const output = new Uint8Array(24 + finalUrlBytes.length + reasonBytes.length + headerBytes.length + body.length);
  output.set([0x50, 0x4d, 0x52, 0x31], 0); // PMR1
  const view = new DataView(output.buffer);
  view.setInt32(4, status, true);
  writeU32(view, 8, finalUrlBytes.length);
  writeU32(view, 12, reasonBytes.length);
  writeU32(view, 16, headerBytes.length);
  writeU32(view, 20, body.length);
  let cursor = 24;
  output.set(finalUrlBytes, cursor); cursor += finalUrlBytes.length;
  output.set(reasonBytes, cursor); cursor += reasonBytes.length;
  output.set(headerBytes, cursor); cursor += headerBytes.length;
  output.set(body, cursor);
  return output;
}

function errorResponse(message: string, status = 400) {
  return new Response(message, {
    status,
    headers: {
      "Content-Type": "text/plain; charset=utf-8",
      "Cache-Control": "no-store",
      "Cross-Origin-Resource-Policy": "same-origin"
    }
  });
}

export const onRequestPost = async (context: { request: Request }) => {
  try {
    const incomingUrl = new URL(context.request.url);
    const origin = context.request.headers.get("Origin");
    if (origin && origin !== incomingUrl.origin) return errorResponse("Cross-origin bridge access denied", 403);

    const bytes = new Uint8Array(await context.request.arrayBuffer());
    if (bytes.length < 20 || bytes.length > MAX_BODY_BYTES + MAX_HEADER_BYTES + 256 * 1024) {
      return errorResponse("Invalid HTTP bridge request size", 413);
    }
    if (bytes[0] !== 0x50 || bytes[1] !== 0x4d || bytes[2] !== 0x48 || bytes[3] !== 0x31) {
      return errorResponse("Invalid HTTP bridge request magic");
    }

    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const methodLength = readU32(view, 4);
    const urlLength = readU32(view, 8);
    const headerLength = readU32(view, 12);
    const bodyLength = readU32(view, 16);
    const payloadLength = methodLength + urlLength + headerLength + bodyLength;
    if (headerLength > MAX_HEADER_BYTES || bodyLength > MAX_BODY_BYTES || payloadLength > bytes.length - 20) {
      return errorResponse("Truncated HTTP bridge request");
    }

    let cursor = 20;
    const method = decoder.decode(bytes.subarray(cursor, cursor + methodLength)).trim().toUpperCase(); cursor += methodLength;
    const targetText = decoder.decode(bytes.subarray(cursor, cursor + urlLength)); cursor += urlLength;
    const headerText = decoder.decode(bytes.subarray(cursor, cursor + headerLength)); cursor += headerLength;
    const body = bytes.slice(cursor, cursor + bodyLength);
    if (!/^[A-Z][A-Z0-9!#$%&'*+.^_`|~-]{0,31}$/.test(method)) return errorResponse("Invalid upstream method");

    const target = new URL(targetText);
    if (target.protocol !== "http:" && target.protocol !== "https:") return errorResponse("Only HTTP/HTTPS targets are allowed");
    if (target.username || target.password || isPrivateLiteral(target.hostname)) return errorResponse("Unsafe upstream target", 403);

    const headers = parseHeaderBlock(headerText);
    const upstream = await fetch(target.href, {
      method,
      headers,
      body: method === "GET" || method === "HEAD" ? undefined : body,
      redirect: "follow"
    });
    const responseBody = method === "HEAD" ? new Uint8Array() : new Uint8Array(await upstream.arrayBuffer());
    if (responseBody.length > MAX_BODY_BYTES) return errorResponse("Upstream response is too large", 502);
    const envelope = encodeResponse(
      upstream.status,
      upstream.url || target.href,
      upstream.statusText || "",
      serializeHeaders(upstream.headers),
      responseBody
    );
    return new Response(envelope, {
      status: 200,
      headers: {
        "Content-Type": "application/octet-stream",
        "Cache-Control": "no-store",
        "Cross-Origin-Resource-Policy": "same-origin"
      }
    });
  } catch (error) {
    return errorResponse(error instanceof Error ? error.message : String(error), 502);
  }
};

export const onRequestGet = () => errorResponse("Use POST", 405);
