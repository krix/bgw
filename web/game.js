const canvas = document.querySelector("#game");
const ctx = canvas.getContext("2d");
ctx.imageSmoothingEnabled = false;

const W = canvas.width;
const H = canvas.height;
const PLAYER_SIZE = 40;
const keys = new Set();
const pressed = new Set();
const assets = {
  title: image("../build/bmp/title.bmp"),
  background: image("../build/bmp/bg001.bmp"),
  player: indexedImage("../build/bmp/chr001.bmp"),
  tiles: image("../build/bmp/chr002.BMP"),
  objects: indexedImage("../build/bmp/chr005.bmp"),
  font: indexedImage("../build/bmp/font8x8-01.BMP"),
};
const sounds = {
  jump: new Audio("../build/wav/jump.wav"),
  hit: new Audio("../build/wav/break.wav"),
  get: new Audio("../build/wav/get.wav"),
};

let mode = "menu";
let difficulty = 1;
let score = 0;
let lives = 3;
let breakableScoreIndex = 0;
let jumpEnergy = 30;
let introFrames = 0;
let lastTime = performance.now();
let player;
let platforms;
  let mapData;
let cameraX = 0;
const playerStart = { x: 128, y: 178 };

fetch("../build/map/000.map")
  .then((response) => response.ok ? response.arrayBuffer() : Promise.reject(new Error("map unavailable")))
  .then((buffer) => {
    const view = new DataView(buffer);
    const height = view.getInt32(0, true);
    const width = view.getInt32(4, true);
    const tiles = [];
    for (let index = 0; index < width * height; index += 1) {
      const offset = 24 + index * 2;
      tiles.push(offset + 1 < buffer.byteLength ? view.getUint16(offset, true) : 0);
    }
    mapData = { width, height, cellSize: view.getInt32(8, true), tiles };
  })
  .catch(() => { mapData = null; });

function image(src) {
  const result = new Image();
  result.src = src;
  return result;
}

function indexedImage(src) {
  const result = { canvas: null, ready: false };
  fetch(src)
    .then((response) => response.ok ? response.arrayBuffer() : Promise.reject(new Error("image unavailable")))
    .then((buffer) => {
      const view = new DataView(buffer);
      const width = view.getInt32(18, true);
      const height = Math.abs(view.getInt32(22, true));
      const bits = view.getUint16(28, true);
      if (bits !== 8) throw new Error("indexed image expected");
      const pixels = document.createElement("canvas");
      pixels.width = width;
      pixels.height = height;
      const output = pixels.getContext("2d");
      const imageData = output.createImageData(width, height);
      const paletteOffset = view.getUint32(10, true) - 256 * 4;
      const rowBytes = (width + 3) & ~3;
      const bottomUp = view.getInt32(22, true) > 0;
      for (let y = 0; y < height; y += 1) {
        const sourceY = bottomUp ? height - 1 - y : y;
        for (let x = 0; x < width; x += 1) {
          const index = view.getUint8(view.getUint32(10, true) + sourceY * rowBytes + x);
          const color = paletteOffset + index * 4;
          const target = (y * width + x) * 4;
          imageData.data[target] = view.getUint8(color + 2);
          imageData.data[target + 1] = view.getUint8(color + 1);
          imageData.data[target + 2] = view.getUint8(color);
          imageData.data[target + 3] = index === 0 ? 0 : 255;
        }
      }
      output.putImageData(imageData, 0, 0);
      result.canvas = pixels;
      result.ready = true;
    })
    .catch(() => {});
  return result;
}

function play(sound) {
  sound.currentTime = 0;
  sound.play().catch(() => {});
}

function resetGame() {
  score = 0;
  lives = 3;
  breakableScoreIndex = 0;
  jumpEnergy = 30;
  introFrames = 0x78;
  cameraX = 0;
  player = { x: playerStart.x, y: playerStart.y, vx: 0, vy: 0, frame: 0, frameTime: 0, invulnerable: 0 };
  platforms = mapPlatforms() || [
    { x: 0, y: 216, w: 320, h: 24 },
    { x: 48, y: 176, w: 60, h: 8 },
    { x: 144, y: 152, w: 56, h: 8 },
    { x: 232, y: 184, w: 64, h: 8 },
  ];
  mode = "intro";
}

function mapPlatforms() {
  if (!mapData) return null;
  const result = [];
  const size = mapData.cellSize;
  const width = mapData.width;
  const height = mapData.height;
  for (let row = 0; row < height; row += 1) {
    let runStart = -1;
    for (let column = 0; column <= width; column += 1) {
      const value = column < width ? mapData.tiles[row * width + column] : 0;
      // FUN_00452774 treats tile 1 and tiles 4..15 as blocking geometry.
      // Tile 2 is handled by the breakable-block routine and tile 3 is the
      // level-clear trigger, so neither belongs in the platform list.
      const solid = value === 1 || (value >= 4 && value <= 15);
       if (solid && runStart < 0) runStart = column;
      if (!solid && runStart >= 0) {
        result.push({ x: runStart * size, y: row * size, w: (column - runStart) * size, h: size });
        runStart = -1;
      }
    }
  }
  return result.length ? result : null;
}

function mapTile(column, row) {
  if (!mapData || column < 0 || row < 0 || column >= mapData.width || row >= mapData.height) {
    return 0;
  }
  return mapData.tiles[row * mapData.width + column] || 0;
}

function startOrSelect() {
  if (mode === "menu" || mode === "gameover" || mode === "clear") {
    resetGame();
  }
}

function update(dt) {
  if (mode === "intro") {
    introFrames -= 1;
    if (introFrames <= 0) mode = "game";
    return;
  }
  if (mode !== "game") return;
  player.invulnerable = Math.max(0, player.invulnerable - dt);

  const direction = (keys.has("ArrowRight") ? 1 : 0) - (keys.has("ArrowLeft") ? 1 : 0);
  // FUN_00453B64 shifts the -1/0/+1 input by six fixed-point bits:
  // two pixels per native 16 ms frame.
  player.vx = direction * 120;
  if (direction !== 0) {
    player.frameTime += dt;
    player.frame = Math.floor(player.frameTime * 8) % 4;
  } else {
    player.frame = 0;
    player.frameTime = 0;
  }
  if (pressed.has("Space") && player.y >= 170 && jumpEnergy > 0) {
    // FUN_00453B64 starts the jump at -0x100 fixed units: -8 px/frame.
    player.vy = -480;
    jumpEnergy = Math.max(0, jumpEnergy - 10);
    play(sounds.jump);
  }
  player.vy = Math.min(360, player.vy + 450 * dt);
  const oldY = player.y;
  const oldBottom = player.y + PLAYER_SIZE;
  player.x += player.vx * dt;
  player.y += player.vy * dt;
  const worldWidth = mapData ? mapData.width * mapData.cellSize : W;
  player.x = Math.max(0, Math.min(worldWidth - PLAYER_SIZE, player.x));

  if (mapData && player.vy < 0) {
    const tileSize = mapData.cellSize;
    const firstColumn = Math.floor((player.x + 4) / tileSize);
    const lastColumn = Math.floor((player.x + PLAYER_SIZE - 4) / tileSize);
    const oldTop = Math.floor(oldY / tileSize);
    const newTop = Math.floor(player.y / tileSize);
    for (let column = firstColumn; column <= lastColumn; column += 1) {
      for (let row = newTop; row <= oldTop; row += 1) {
        if (mapTile(column, row) !== 2) continue;
        const tileBottom = (row + 1) * tileSize;
        if (oldY >= tileBottom && player.y <= tileBottom) {
           mapData.tiles[row * mapData.width + column] = 0;
           player.y = tileBottom;
           player.vy = 0;
           score += [100, 500, 1000][Math.min(breakableScoreIndex, 2)];
           breakableScoreIndex += 1;
        }
      }
    }
  }

  for (const platform of platforms) {
    const bottom = player.y + PLAYER_SIZE;
    if (player.vy >= 0 && oldBottom <= platform.y && bottom >= platform.y &&
        player.x + PLAYER_SIZE > platform.x && player.x < platform.x + platform.w) {
      player.y = platform.y - PLAYER_SIZE;
      player.vy = 0;
    }
  }

  // FUN_00453E50 sends the player to the miss state once its fixed-point
  // vertical position exceeds 0x100 pixels.
  if (player.y > 256) {
    lives -= 1;
    player.x = playerStart.x;
    player.y = playerStart.y;
    player.vy = 0;
    jumpEnergy = 30;
    play(sounds.hit);
    if (lives <= 0) mode = "gameover";
  }

  const tileSize = mapData?.cellSize || 0;
  const playerColumn = tileSize ? Math.floor((player.x + 16) / tileSize) : -1;
  const playerRow = tileSize ? Math.floor((player.y + PLAYER_SIZE / 2) / tileSize) : -1;
  const onGoalTile = mapTile(playerColumn, playerRow) === 3;
  if (onGoalTile) {
    play(sounds.get);
    mode = "clear";
  }

  cameraX = Math.max(0, Math.min(Math.max(0, worldWidth - W), player.x - W * 0.35));
}

function draw() {
  ctx.fillStyle = "#17233b";
  ctx.fillRect(0, 0, W, H);
  if (mode === "menu") drawMenu();
  else if (mode === "intro") drawIntro();
  else if (mode === "game") drawGame();
  else drawEndScreen();
  pressed.clear();
}

function drawMenu() {
  if (assets.title.complete && assets.title.naturalWidth) {
    ctx.drawImage(assets.title, 0, 0, W, H);
  } else {
    ctx.fillStyle = "#253c65";
    ctx.fillRect(0, 0, W, H);
  }
  panel("BOMB MOGURA", 74, 56, 172, 54);
  text("ENTER  START", 92, 128);
  text("ARROWS  DIFFICULTY " + difficulty, 58, 148);
  text("SPACE  JUMP", 105, 168);
}

function drawIntro() {
  drawGame();
  panel("LEVEL 1", 104, 92, 112, 36);
}

function drawGame() {
  if (assets.background.complete && assets.background.naturalWidth) {
    const pattern = ctx.createPattern(assets.background, "repeat");
    ctx.fillStyle = pattern || "#263b58";
    ctx.fillRect(0, 0, W, H);
  }
  if (mapData) {
    const size = mapData.cellSize;
    const firstColumn = Math.max(0, Math.floor(cameraX / size) - 1);
    const lastColumn = Math.min(mapData.width, Math.ceil((cameraX + W) / size) + 1);
    for (let row = 0; row < mapData.height; row += 1) {
      for (let column = firstColumn; column < lastColumn; column += 1) {
        const value = mapData.tiles[row * mapData.width + column];
        if (!value) continue;
        const drawX = column * size - cameraX;
        const drawY = row * size;
        if (value >= 4 && value <= 15 && assets.tiles.complete && assets.tiles.naturalWidth) {
          const tile = value - 4;
          ctx.drawImage(assets.tiles, (tile % 4) * size, Math.floor(tile / 4) * size,
            size, size, drawX, drawY, size, size);
        } else if ((value === 2 || value === 3) && assets.objects.ready) {
          ctx.drawImage(assets.objects.canvas, (value - 2) * size, 0, size, size,
            drawX, drawY, size, size);
        } else {
          ctx.fillStyle = value === 3 ? "#e8bd55" : value === 2 ? "#99644c" : "#27324b";
          ctx.fillRect(drawX, drawY, size, size);
        }
      }
    }
  }
  ctx.fillStyle = "#27324b";
  for (const platform of platforms) {
    ctx.fillRect(platform.x - cameraX, platform.y, platform.w, platform.h);
  }
  if (player.invulnerable === 0 || Math.floor(player.invulnerable * 12) % 2 === 0) {
    if (assets.player.ready) {
      ctx.drawImage(assets.player.canvas, player.frame * 40, 0, 40, 40,
        player.x - cameraX, player.y, 40, 40);
    } else { ctx.fillStyle = "#f4dc93"; ctx.fillRect(player.x - cameraX, player.y, 40, 40); }
  }
  panel("SCORE " + String(score).padStart(5, "0") + "   LIFE " + lives, 4, 4, 170, 18);
  ctx.fillStyle = "#382b28";
  ctx.fillRect(301, 114, 8, 31);
  ctx.fillStyle = "#e8bd55";
  ctx.fillRect(301, 145 - jumpEnergy, 8, jumpEnergy);
}

function drawEndScreen() {
  ctx.fillStyle = mode === "clear" ? "#244c42" : "#4b2735";
  ctx.fillRect(0, 0, W, H);
  panel(mode === "clear" ? "STAGE CLEAR" : "GAME OVER", 72, 70, 176, 44);
  text("SCORE " + score, 116, 136);
  text("ENTER  RETRY", 104, 166);
  text("ESC  MENU", 112, 184);
}

function panel(label, x, y, w, h) {
  ctx.fillStyle = "#10131bd9";
  ctx.fillRect(x, y, w, h);
  ctx.strokeStyle = "#d8c89a";
  ctx.strokeRect(x, y, w, h);
  text(label, x + 10, y + h / 2 + 5);
}

function text(value, x, y) {
  if (assets.font.ready) {
    let cursor = x;
    for (const character of value) {
      const code = character.charCodeAt(0) - 32;
      if (code >= 0 && code < 64) {
        ctx.drawImage(assets.font.canvas, (code % 32) * 8, Math.floor(code / 32) * 8,
          8, 8, cursor, y - 8, 8, 8);
      }
      cursor += 8;
    }
    return;
  }
  ctx.fillStyle = "#f6e8bb";
  ctx.font = "10px monospace";
  ctx.fillText(value, x, y);
}

window.addEventListener("keydown", (event) => {
  if (!["ArrowLeft", "ArrowRight", "ArrowUp", "Space", "Enter", "Escape"].includes(event.code)) return;
  event.preventDefault();
  if (!keys.has(event.code)) pressed.add(event.code);
  keys.add(event.code);
  if (event.code === "Enter") startOrSelect();
  if (event.code === "Escape") mode = "menu";
  if (mode === "menu" && event.code === "ArrowLeft") difficulty = Math.max(1, difficulty - 1);
  if (mode === "menu" && event.code === "ArrowRight") difficulty = Math.min(3, difficulty + 1);
});

window.addEventListener("keyup", (event) => keys.delete(event.code));

function loop(now) {
  const dt = Math.min(0.05, (now - lastTime) / 1000);
  lastTime = now;
  update(dt);
  draw();
  requestAnimationFrame(loop);
}

requestAnimationFrame(loop);
