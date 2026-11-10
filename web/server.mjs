import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const mime = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".txt": "text/plain; charset=utf-8",
  ".ini": "text/plain; charset=utf-8",
  ".bmp": "image/bmp",
  ".ico": "image/x-icon",
  ".wav": "audio/wav",
  ".mid": "audio/midi",
  ".map": "application/octet-stream",
  ".dat": "application/octet-stream",
};

const server = http.createServer((request, response) => {
  if (request.method !== "GET" && request.method !== "HEAD") {
    response.writeHead(405, { Allow: "GET, HEAD" });
    response.end("Method not allowed");
    return;
  }

  let requested;
  try {
    requested = decodeURIComponent((request.url || "/").split("?")[0]);
  } catch {
    response.writeHead(400);
    response.end("Bad request");
    return;
  }

  const relative = requested === "/"
    ? "/web/index.html"
    : (requested.endsWith("/") ? requested + "index.html" : requested);
  const file = path.resolve(root, "." + relative);
  if (!file.startsWith(root + path.sep)) {
    response.writeHead(403);
    response.end("Forbidden");
    return;
  }

  fs.readFile(file, (error, data) => {
    if (error) {
      response.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
      response.end("Not found");
      return;
    }
    response.writeHead(200, {
      "Content-Type": mime[path.extname(file).toLowerCase()] || "application/octet-stream",
      "Content-Length": data.length,
      "Cache-Control": "no-store",
    });
    if (request.method === "HEAD") {
      response.end();
    } else {
      response.end(data);
    }
  });
});

const port = Number(process.env.PORT) || 8080;
server.listen(port, "127.0.0.1", () => {
  console.log(`Bunguru WA web port: http://127.0.0.1:${port}/web/`);
  console.log(`Serving ${root}`);
});
