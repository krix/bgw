import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const mime = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".bmp": "image/bmp",
  ".wav": "audio/wav",
};

const server = http.createServer((request, response) => {
  const requested = decodeURIComponent((request.url || "/").split("?")[0]);
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
      response.writeHead(404);
      response.end("Not found");
      return;
    }
    response.writeHead(200, { "Content-Type": mime[path.extname(file).toLowerCase()] || "application/octet-stream" });
    response.end(data);
  });
});

server.listen(Number(process.env.PORT || 8123), "127.0.0.1", () => {
  console.log(`BGW web server listening on http://127.0.0.1:${process.env.PORT || 8123}`);
});
