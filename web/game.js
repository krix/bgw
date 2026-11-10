import { fetchBinary, decodeBmp, toCanvas, parseMap } from "./bmp.js";
import { createAudio, parseMidi } from "./audio.js";

const WIDTH = 320;
const HEIGHT = 240;
const TILE = 32;
const BOX_HALF = 12;
const SPAWN_X = 208;
const SPAWN_Y = 8116;
const SPAWN_CAMERA_X = 80;
const SPAWN_CAMERA_Y = 7938;
const BACKGROUND_SCROLL_SEED = 0x400;
const LEVEL_NAMES = ["EASY", "NOMAL", "HARD", "HARD1", "HARD2"];
const ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.!? ";

const STATE = {
  MenuInit: 0,
  Menu: 1,
  Gameplay: 2,
  LevelIntro: 3,
  Miss: 4,
  GameOver: 5,
  Rank: 6,
  Result: 7,
  Continue: 8,
  Clear: 9,
  PostClear: 10,
  ExitDialog: 99,
};

const canvas = document.querySelector("#game");
const context = canvas.getContext("2d");
context.imageSmoothingEnabled = false;

const keys = new Set();
const audio = createAudio();

const assetBase = new URL("../build/", import.meta.url);

const BMP_FILES = {
  font: "bmp/font8x8-01.BMP",
  title: "bmp/title.bmp",
  player: "bmp/chr001.bmp",
  background: "bmp/bg002.bmp",
  tiles: "bmp/bg001.bmp",
  collapse: "bmp/chr002.BMP",
  power: "bmp/chr003.bmp",
  popup: "bmp/chr004.bmp",
  puff: "bmp/chr005.bmp",
};

const MIDI_FILES = ["init", "main", "miss", "gameover", "rank", "clear"];
const WAV_FILES = ["jump", "break", "get", "pi"];

const assets = {};
let map = null;
let initialTiles = null;
let sm = null;
let paused = false;

const game = {
  playing: false,
  left: false,
  right: false,
  jump: false,
  grounded: false,
  cleared: false,
  playerDead: false,
  playerX: 128,
  playerY: 178,
  playerVelocityY: 0,
  cameraX: 0,
  cameraY: 0,
  backgroundScrollX: BACKGROUND_SCROLL_SEED,
  backgroundScrollY: 0,
  backgroundReseed: false,
  playerSpriteIndex: 0,
  playerAnimFrame: 0,
  playerAnimTick: 0,
  playerFrame: 0,
  bonusAnimation: 0,
  wasPostClear: false,
  playerDirection: 1,
  solidContactFrames: 0,
  score: 0,
  breakableScoreIndex: 0,
  breakStreakTimer: 0,
  breakableCount: 0,
  jumpEnergy: 0,
  timeRemaining: 99,
  timeAccumulator: 0,
  effects: [],
  highScores: new Array(10).fill(0),
  highScoreNames: new Array(10).fill(""),
  exitScreen: false,
};

const DEFAULT_SETTINGS = {
  difficulty: 1,
  wait: false,
  vsync: false,
  jumpButton: 0,
};

function loadSettings() {
  try {
    const stored = JSON.parse(localStorage.getItem("bgw.settings.v1") || "null");
    if (stored && typeof stored === "object") {
      return {
        difficulty: Math.min(3, Math.max(1, Number(stored.difficulty) || 1)),
        wait: Boolean(stored.wait),
        vsync: Boolean(stored.vsync),
        jumpButton: Math.min(15, Math.max(0, Number(stored.jumpButton) || 0)),
      };
    }
  } catch {
    // Ignore malformed storage.
  }
  return { ...DEFAULT_SETTINGS };
}

function saveSettings() {
  if (!sm) return;
  try {
    localStorage.setItem("bgw.settings.v1", JSON.stringify({
      difficulty: sm.difficulty,
      wait: sm.waitEnabled,
      vsync: sm.vsyncEnabled,
      jumpButton: sm.jumpButton,
    }));
  } catch {
    // Storage may be disabled.
  }
}

function saveRank() {
  try {
    localStorage.setItem("bgw.rank.v1", JSON.stringify({
      scores: game.highScores,
      names: game.highScoreNames,
    }));
  } catch {
    // Storage may be disabled.
  }
}

async function loadRank() {
  try {
    const stored = JSON.parse(localStorage.getItem("bgw.rank.v1") || "null");
    if (stored && Array.isArray(stored.scores) && Array.isArray(stored.names) &&
        stored.scores.length === 10 && stored.names.length === 10) {
      game.highScores = stored.scores.map((value) => Number(value) | 0);
      game.highScoreNames = stored.names.map((value) => String(value ?? ""));
      return;
    }
  } catch {
    // Fall through to the shipped defaults.
  }

  try {
    const bytes = await fetchBinary(new URL("data/rank.dat", assetBase));
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    for (let i = 0; i < 10; i += 1) {
      const offset = i * 12;
      game.highScores[i] = view.getInt32(offset, true);
      let name = "";
      for (let j = 0; j < 8; j += 1) {
        const code = bytes[offset + 4 + j];
        if (code === 0) break;
        name += String.fromCharCode(code);
      }
      game.highScoreNames[i] = name;
    }
  } catch {
    game.highScores = [34300, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    game.highScoreNames = ["KRI", "", "", "", "", "", "", "", "", ""];
  }
}

function floorDiv(value, divisor) {
  return Math.floor(value / divisor);
}

function clamp(value, minimum, maximum) {
  return Math.min(maximum, Math.max(minimum, value));
}

function solidTile(value) {
  return value === 1 || (value >= 4 && value <= 15);
}

function animatedThreshold(value) {
  if (value >= 4 && value <= 7) return 8;
  if (value >= 8 && value <= 11) return 4;
  if (value >= 12 && value <= 15) return 2;
  return 0;
}

function tileAt(column, row) {
  if (column < 0 || row < 0 || column >= map.columns || row >= map.rows) {
    return 0;
  }
  const index = row * map.columns + column;
  return index < map.tiles.length ? map.tiles[index] : 0;
}

function applyLevelRemap(level) {
  for (let i = 0; i < map.tiles.length; i += 1) {
    const tile = map.tiles[i];
    if (level === 1) {
      if (tile === 8 || tile === 12) map.tiles[i] = 4;
    } else if (level === 3) {
      if (tile === 4 || tile === 8) map.tiles[i] = 12;
    }
  }
}

const SPRITES = [
  { surface: "player", width: 40, height: 40, columns: 4, rows: 5 },
  { surface: "collapse", width: 32, height: 32, columns: 4, rows: 3 },
  { surface: "popup", width: 32, height: 16, columns: 1, rows: 3 },
  { surface: "puff", width: 32, height: 32, columns: 2, rows: 1 },
];

function lookupSpriteFrame(frame) {
  let first = 0;
  for (const sprite of SPRITES) {
    const count = sprite.columns * sprite.rows;
    if (frame >= first && frame < first + count) {
      const cell = frame - first;
      return {
        canvas: assets[sprite.surface],
        sx: (cell % sprite.columns) * sprite.width,
        sy: floorDiv(cell, sprite.columns) * sprite.height,
        width: sprite.width,
        height: sprite.height,
      };
    }
    first += count;
  }
  return null;
}

function spawnEffect(x, y, typeIndex, animFrame) {
  const base = typeIndex === 2 ? 20 : typeIndex === 3 ? 32 : 35;
  if (game.effects.length >= 512) {
    const index = game.effects.findIndex((effect) => effect.type === 2);
    if (index >= 0) game.effects.splice(index, 1);
    else game.effects.shift();
  }
  game.effects.push({
    type: typeIndex,
    spriteIndex: base,
    animFrame,
    xFixed: x << 5,
    yFixed: y << 5,
    xDelta: 0,
    yDelta: 0,
    cooldown: 0,
    animationTick: 0,
    initialized: 0,
    active: true,
  });
}

function updateEffect(effect) {
  switch (effect.type) {
    case 2:
      effect.yDelta = Math.min(effect.yDelta + 4, 0xc0);
      effect.yFixed += effect.yDelta;
      break;
    case 3:
      if (effect.initialized === 0) {
        effect.initialized = 1;
        effect.cooldown = 0x3c;
      }
      if (effect.cooldown === 0) {
        effect.active = false;
        return;
      }
      break;
    case 4:
      if (effect.initialized === 0) {
        effect.initialized = 1;
        effect.cooldown = 0x3c;
      }
      effect.animationTick += 1;
      if (effect.animationTick > 4) {
        effect.animationTick = 0;
        effect.animFrame += 1;
        if (effect.animFrame > 1) {
          effect.active = false;
          return;
        }
      }
      break;
    default:
      break;
  }
}

function updateEffects() {
  for (const effect of game.effects) {
    if (!effect.active) continue;
    updateEffect(effect);
    if (!effect.active) continue;
    if (effect.type === 2 &&
        (effect.yFixed >> 5) > game.cameraY + HEIGHT + 160) {
      effect.active = false;
      continue;
    }
    if (effect.cooldown !== 0) effect.cooldown -= 1;
  }
  if (game.effects.some((effect) => !effect.active)) {
    game.effects = game.effects.filter((effect) => effect.active);
  }
}

function playSound(name) {
  audio.sfx.play(name);
}

class GameStateMachine {
  constructor() {
    this.state = STATE.MenuInit;
    this.stateBeforeExit = STATE.Menu;
    this.frameCounter = 0;
    this.difficulty = 1;
    this.score = 0;
    this.bonus = 0;
    this.pendingBonus = 0;
    this.phase = 0;
    this.menuIndex = 0;
    this.optionIndex = 0;
    this.menuPage = 0;
    this.jumpButton = 0;
    this.waitEnabled = false;
    this.vsyncEnabled = false;
    this.rankIndex = 0;
    this.nameCursor = 1;
    this.nameLength = 0;
    this.prevButton = false;
    this.prevJump = false;
    this.prevUp = false;
    this.prevDown = false;
    this.buttonEdge = false;
    this.jumpEdge = false;
    this.upEdge = false;
    this.downEdge = false;
  }

  tick() {
    this.pollInput();
    switch (this.state) {
      case STATE.MenuInit: this.tickMenuInit(); break;
      case STATE.Menu: break;
      case STATE.Gameplay: this.tickGameplay(); break;
      case STATE.LevelIntro: break;
      case STATE.Miss: this.tickMiss(); break;
      case STATE.GameOver: this.tickGameOver(); break;
      case STATE.Rank: this.tickRank(); break;
      case STATE.Result: this.tickResult(); break;
      case STATE.Continue: this.tickContinue(); break;
      case STATE.Clear: this.tickClear(); break;
      case STATE.PostClear: this.tickPostClear(); break;
      default: break;
    }
  }

  pollInput() {
    const button = keys.has("Space") || keys.has("KeyZ") || keys.has("KeyX");
    const jump = keys.has("ControlLeft") || keys.has("ControlRight") || keys.has("Space");
    const up = keys.has("ArrowUp");
    const down = keys.has("ArrowDown");

    this.buttonEdge = button && !this.prevButton;
    this.jumpEdge = jump && !this.prevJump;
    this.upEdge = up && !this.prevUp;
    this.downEdge = down && !this.prevDown;

    this.prevButton = button;
    this.prevJump = jump;
    this.prevUp = up;
    this.prevDown = down;
  }

  tickMenuInit() {
    audio.music.stop();
    audio.music.play("init", false);
    this.frameCounter = 0;
    this.menuPage = 0;
    this.menuIndex = 0;
    this.state = STATE.Menu;
  }

  tickGameplay() {
    if (this.state === STATE.Gameplay) {
      this.frameCounter += 1;
      if (this.frameCounter > 0x78) {
        this.frameCounter = 0;
        this.state = STATE.LevelIntro;
      }
    }
  }

  tickMiss() {
    if (this.frameCounter === 0) {
      audio.music.stop();
      audio.music.play("miss", false);
    }
    this.frameCounter += 1;
    if (audio.music.finished()) {
      this.frameCounter = 0;
      this.state = STATE.GameOver;
    }
  }

  tickGameOver() {
    if (this.frameCounter === 0) {
      audio.music.stop();
      audio.music.play("gameover", false);
    }
    this.frameCounter += 1;
    if (audio.music.finished()) {
      this.frameCounter = 0;
      const rank = insertRankScore(this.score);
      if (rank >= 0) {
        this.rankIndex = rank;
        this.nameCursor = 1;
        this.nameLength = 0;
        this.state = STATE.Rank;
      } else {
        this.state = STATE.MenuInit;
      }
    }
  }

  tickRank() {
    if (this.upEdge) {
      this.nameCursor = this.nameCursor <= 1 ? 30 : this.nameCursor - 1;
    }
    if (this.downEdge) {
      this.nameCursor = this.nameCursor >= 30 ? 1 : this.nameCursor + 1;
    }
    if (this.buttonEdge && this.nameLength < 3) {
      writeRankNameByte(this.rankIndex, this.nameLength, this.currentNameChar());
      this.nameLength += 1;
    }
    if (this.nameLength > 2) {
      this.frameCounter = 0;
      this.state = STATE.Result;
    }
  }

  tickResult() {
    this.frameCounter += 1;
    if (this.buttonEdge || this.jumpEdge || this.frameCounter > 600) {
      this.frameCounter = 0;
      this.state = STATE.MenuInit;
    }
  }

  tickContinue() {
    this.frameCounter += 1;
    if (this.buttonEdge) {
      this.frameCounter = floorDiv(this.frameCounter, 0x78) * 0x78 + 0x78;
    }
    if (this.jumpEdge) {
      this.frameCounter = 0;
      this.state = STATE.Gameplay;
    }
    if (this.frameCounter > 0x4af) {
      this.frameCounter = 0;
      this.state = STATE.Result;
    }
  }

  tickClear() {
    this.frameCounter += 1;
    if (audio.music.finished()) {
      this.frameCounter = 0;
      this.state = STATE.PostClear;
    }
  }

  tickPostClear() {
    if (this.phase === 0) {
      this.frameCounter += 1;
      if (this.frameCounter > 0x78) {
        this.frameCounter = 0;
        this.phase = 1;
      }
    }
    if (this.phase === 1) {
      if (this.bonus > 0) {
        const tick = Math.min(this.bonus, 100);
        this.bonus -= tick;
        this.score += tick;
      }
      if (this.bonus <= 0) {
        this.bonus = 0;
        this.frameCounter = 0;
        this.phase = 2;
      }
    }
    if (this.phase === 2) {
      this.frameCounter += 1;
      if (this.frameCounter > 0x4b0) {
        this.frameCounter = 0;
        this.phase = 0;
        this.pendingBonus = 0;
        const rank = insertRankScore(this.score);
        if (rank >= 0) {
          this.rankIndex = rank;
          this.nameCursor = 1;
          this.nameLength = 0;
          this.frameCounter = 1;
          this.state = STATE.Rank;
        } else {
          this.state = STATE.MenuInit;
        }
      }
    }
  }

  setSettings(level, wait, vsync) {
    this.difficulty = clamp(level, 1, 3);
    this.waitEnabled = wait;
    this.vsyncEnabled = vsync;
  }

  setJumpButton(button) {
    this.jumpButton = clamp(button, 0, 15);
  }

  setScore(score) {
    this.score = score;
  }

  setBonus(bonus) {
    this.bonus = bonus;
    this.pendingBonus = bonus;
  }

  startGameplay() {
    this.state = STATE.Gameplay;
    this.frameCounter = 0;
    this.phase = 0;
    this.score = 0;
    this.bonus = 0;
    this.pendingBonus = 0;
  }

  enterMiss() {
    this.state = STATE.Miss;
    this.frameCounter = 0;
  }

  enterClear() {
    this.state = STATE.Clear;
    this.frameCounter = 0;
    this.phase = 0;
    audio.music.stop();
    audio.music.play("clear", false);
  }

  currentNameChar() {
    const index = this.nameCursor >= 1 && this.nameCursor <= 30
      ? this.nameCursor - 1
      : 0;
    return ALPHABET[index];
  }

  menuMove(delta) {
    if (delta === 0) return;
    if (this.menuPage === 0) {
      this.menuIndex = (this.menuIndex + delta + 3) % 3;
    } else if (this.menuPage === 1) {
      this.optionIndex = (this.optionIndex + delta + 6) % 6;
    }
  }

  menuHorizontal(delta) {
    if (delta === 0 || this.menuPage !== 1) return;
    switch (this.optionIndex) {
      case 0:
        if (delta < 0 && this.difficulty > 1) this.difficulty -= 1;
        if (delta > 0 && this.difficulty < 3) this.difficulty += 1;
        break;
      case 1:
        this.jumpButton = (this.jumpButton + delta + 16) % 16;
        break;
      case 2:
        this.waitEnabled = !this.waitEnabled;
        break;
      case 3:
        this.vsyncEnabled = !this.vsyncEnabled;
        break;
      default:
        break;
    }
    saveSettings();
  }

  menuBack() {
    if (this.menuPage === 2) this.menuPage = 1;
    else if (this.menuPage === 1) this.menuPage = 0;
  }

  menuActivate() {
    if (this.menuPage === 2) {
      this.menuPage = 1;
      return "none";
    }
    if (this.menuPage === 1) {
      switch (this.optionIndex) {
        case 4:
          this.menuPage = 2;
          break;
        case 5:
          this.menuPage = 0;
          this.menuIndex = 1;
          break;
        default:
          break;
      }
      return "none";
    }
    switch (this.menuIndex) {
      case 0:
        this.startGameplay();
        return "start";
      case 1:
        this.menuPage = 1;
        this.optionIndex = 0;
        return "none";
      case 2:
        return "exit";
      default:
        return "none";
    }
  }

  onKeyDown(key) {
    if (key === 0x1b) {
      if (this.state === STATE.ExitDialog) {
        this.state = STATE.MenuInit;
      } else {
        this.stateBeforeExit = this.state;
        this.state = STATE.MenuInit;
      }
    }
    if (this.state === STATE.ExitDialog && key !== 0x1b) {
      this.state = this.stateBeforeExit;
    }
    if (key === 0x52) {
      this.state = STATE.MenuInit;
    }
  }
}

function insertRankScore(score) {
  for (let index = 0; index < game.highScores.length; index += 1) {
    if (game.highScores[index] < score) {
      for (let move = game.highScores.length - 1; move > index; move -= 1) {
        game.highScores[move] = game.highScores[move - 1];
        game.highScoreNames[move] = game.highScoreNames[move - 1];
      }
      game.highScores[index] = score;
      game.highScoreNames[index] = "\0\0\0\0";
      saveRank();
      return index;
    }
  }
  return -1;
}

function writeRankNameByte(rank, offset, value) {
  if (rank < 0 || rank >= game.highScoreNames.length) return;
  if (offset < 0 || offset > 3) return;
  let name = game.highScoreNames[rank];
  while (name.length < 4) name += "\0";
  name = name.slice(0, 4);
  game.highScoreNames[rank] = name.slice(0, offset) + value + name.slice(offset + 1);
  saveRank();
}

function startNewGame() {
  sm.startGameplay();
  game.playing = true;
  game.score = 0;
  game.jumpEnergy = 0;
  game.timeRemaining = 99;
  game.timeAccumulator = 0;
  game.playerX = SPAWN_X;
  game.playerY = SPAWN_Y;
  game.playerVelocityY = 0;
  map.tiles.set(initialTiles);
  applyLevelRemap(sm.difficulty);
  game.breakableScoreIndex = 0;
  game.breakStreakTimer = 0;
  game.breakableCount = 0;
  game.solidContactFrames = 0;
  game.effects = [];
  game.cameraX = SPAWN_CAMERA_X;
  game.cameraY = SPAWN_CAMERA_Y;
  game.backgroundScrollX = BACKGROUND_SCROLL_SEED;
  game.backgroundScrollY = 0;
  game.backgroundReseed = false;
  game.playerAnimTick = 0;
  game.playerSpriteIndex = 0;
  game.playerAnimFrame = 0;
  game.playerFrame = 0;
  game.bonusAnimation = 0;
  game.wasPostClear = false;
  game.playerDead = false;
  game.cleared = false;
  game.exitScreen = false;
}

function advanceAnimatedTile(column, row) {
  const index = row * map.columns + column;
  const value = map.tiles[index];
  const threshold = animatedThreshold(value);
  if (threshold === 0) return false;
  if (game.solidContactFrames <= threshold) return false;
  const next = value === 7 || value === 11 || value === 15 ? 0 : value + 1;
  spawnEffect(column * TILE + 16, Math.trunc(game.playerY) + 32, 2, value - 4);
  playSound("break");
  map.tiles[index] = next;
  game.solidContactFrames = 0;
  return true;
}

function updatePlaying() {
  if (!game.playing || !sm) return;
  const state = sm.state;
  const runPlayer = state === STATE.Gameplay || state === STATE.LevelIntro ||
    ((state === STATE.Miss || state === STATE.GameOver) && !game.playerDead);
  if (!runPlayer) {
    updateEffects();
    return;
  }

  const direction = (game.right ? 1 : 0) - (game.left ? 1 : 0);
  if (direction !== 0) game.playerDirection = direction;
  const previousCameraX = game.cameraX;
  const previousCameraY = game.cameraY;
  const wasGrounded = game.grounded;

  if (game.playerVelocityY === 0) {
    game.playerAnimTick += 1;
    if (game.playerAnimTick > 8) {
      game.playerAnimTick = 0;
      game.playerSpriteIndex = 0;
      game.playerAnimFrame = (game.playerAnimFrame + 1) & 1;
    }
  } else {
    game.playerAnimTick += 1;
    if (game.playerAnimTick > 2) {
      game.playerAnimTick = 0;
      game.playerSpriteIndex = 1;
      game.playerAnimFrame += game.playerDirection;
      if (game.playerAnimFrame < 0) game.playerAnimFrame = 15;
      if (game.playerAnimFrame > 15) game.playerAnimFrame = 0;
    }
  }
  game.playerFrame = game.playerSpriteIndex + game.playerAnimFrame;

  updateEffects();

  if (direction !== 0) {
    const step = direction * 2;
    const topRow = floorDiv(Math.trunc(game.playerY) - BOX_HALF, TILE);
    const bottomRow = floorDiv(Math.trunc(game.playerY) + BOX_HALF - 1, TILE);
    const leadColumn = direction > 0
      ? floorDiv(game.playerX + step + BOX_HALF - 1, TILE)
      : floorDiv(game.playerX + step - BOX_HALF, TILE);
    let blocked = false;
    for (let row = topRow; row <= bottomRow && !blocked; row += 1) {
      blocked = solidTile(tileAt(leadColumn, row));
    }
    if (blocked) {
      if (direction > 0) {
        game.playerX = (floorDiv(game.playerX + BOX_HALF - 1, TILE) + 1) * TILE -
          BOX_HALF;
      } else {
        game.playerX = floorDiv(game.playerX - BOX_HALF, TILE) * TILE + BOX_HALF;
      }
    } else {
      game.playerX += step;
    }
  }
  game.playerX = clamp(game.playerX, BOX_HALF, map.columns * TILE - BOX_HALF);

  const boxLeftColumn = floorDiv(game.playerX - BOX_HALF, TILE);
  const boxRightColumn = floorDiv(game.playerX + BOX_HALF - 1, TILE);
  const boxTopRow = floorDiv(Math.trunc(game.playerY) - BOX_HALF, TILE);
  const boxBottomRow = floorDiv(Math.trunc(game.playerY) + BOX_HALF - 1, TILE);

  for (let row = boxTopRow; row <= boxBottomRow; row += 1) {
    for (let column = boxLeftColumn; column <= boxRightColumn; column += 1) {
      if (tileAt(column, row) !== 2) continue;
      const index = row * map.columns + column;
      map.tiles[index] = 0;
      spawnEffect(column * TILE + 16, row * TILE + 16, 3, game.breakableScoreIndex);
      const values = [100, 500, 1000];
      game.score += values[Math.min(game.breakableScoreIndex, 2)];
      if (game.breakableScoreIndex < 2) game.breakableScoreIndex += 1;
      game.breakableCount += 1;
      game.breakStreakTimer = 0x10;
      game.jumpEnergy = Math.max(0, game.jumpEnergy - 4);
      playSound("get");
    }
  }

  const standRow = floorDiv(Math.trunc(game.playerY) + BOX_HALF, TILE);
  const leftColumn = boxLeftColumn;
  const rightColumn = boxRightColumn;
  let groundContact = false;
  let solidColumns = 0;
  for (let column = leftColumn; column <= rightColumn; column += 1) {
    if (solidTile(tileAt(column, standRow))) {
      groundContact = true;
      solidColumns += 1;
    } else {
      game.playerVelocityY += 0.125;
    }
  }
  if (groundContact) {
    game.solidContactFrames += 1;
    for (let column = leftColumn; column <= rightColumn; column += 1) {
      if (advanceAnimatedTile(column, standRow)) break;
    }
    game.jumpEnergy = Math.max(0, game.jumpEnergy - solidColumns);
  }
  game.grounded = groundContact;
  if (groundContact && !wasGrounded) {
    game.playerAnimTick = 0;
    game.playerSpriteIndex = 0;
    game.playerAnimFrame = 0;
    game.playerFrame = 0;
  }

  game.playerVelocityY = Math.min(6, game.playerVelocityY);
  if (game.jump && game.jumpEnergy < 0x1e) {
    game.score += 0x32;
    if (game.jumpEnergy < 0x15) {
      game.playerVelocityY = Math.trunc(game.jumpEnergy / 10) - 8;
      game.jumpEnergy += 10;
    } else {
      game.playerVelocityY = -2;
      game.jumpEnergy = 0x1e;
    }
    playSound("jump");
    if (solidColumns === 1) {
      spawnEffect(game.playerX, Math.trunc(game.playerY), 4, 0);
    }
  }
  game.jump = false;

  const predictedY = Math.trunc(game.playerY + game.playerVelocityY);
  const testRow = game.playerVelocityY < 0
    ? floorDiv(predictedY - BOX_HALF, TILE)
    : floorDiv(predictedY + BOX_HALF - 1, TILE);
  let verticalContact = false;
  for (let column = leftColumn; column <= rightColumn && !verticalContact; column += 1) {
    verticalContact = solidTile(tileAt(column, testRow));
  }
  if (verticalContact) {
    const centreY = Math.trunc(game.playerY);
    if (game.playerVelocityY < 0) {
      const snapped = floorDiv(centreY - BOX_HALF, TILE) * TILE + BOX_HALF;
      game.playerVelocityY = snapped - centreY;
    } else {
      const snapped = (floorDiv(centreY + BOX_HALF - 1, TILE) + 1) * TILE - BOX_HALF;
      game.playerVelocityY = snapped - centreY;
    }
  }
  game.playerY += game.playerVelocityY;

  const playerScreenY = Math.trunc(game.playerY) - game.cameraY;
  if (playerScreenY > 0x100) {
    game.playerDead = true;
    game.playerVelocityY = 0;
    sm.setScore(game.score);
    sm.enterMiss();
    return;
  }

  const firstGoalColumn = Math.max(0, floorDiv(game.playerX - BOX_HALF, TILE));
  const lastGoalColumn = Math.min(map.columns - 1,
    floorDiv(game.playerX + BOX_HALF - 1, TILE));
  const firstGoalRow = Math.max(0, floorDiv(Math.trunc(game.playerY) - BOX_HALF, TILE));
  const lastGoalRow = Math.min(map.rows - 1,
    floorDiv(Math.trunc(game.playerY) + BOX_HALF - 1, TILE));
  let reachedGoal = false;
  for (let row = firstGoalRow; row <= lastGoalRow && !reachedGoal; row += 1) {
    for (let column = firstGoalColumn; column <= lastGoalColumn; column += 1) {
      if (tileAt(column, row) === 3) {
        map.tiles[row * map.columns + column] = 0;
        reachedGoal = true;
        break;
      }
    }
  }
  if (reachedGoal) {
    game.cleared = true;
    sm.setScore(game.score);
    sm.setBonus(game.breakableCount * 100 * sm.difficulty + game.timeRemaining * 100);
    sm.enterClear();
    playSound("get");
  }

  if (game.breakStreakTimer < 1) {
    game.breakableScoreIndex = 0;
  } else {
    game.breakStreakTimer -= 1;
  }

  const maxCameraX = Math.max(0, map.columns * TILE - WIDTH);
  const maxCameraY = Math.max(0, map.rows * TILE - HEIGHT);
  const screenX = game.playerX - game.cameraX;
  if (screenX < 0x40) {
    game.cameraX = game.playerX - 0x40;
  } else if (screenX > 0xc0) {
    game.cameraX = game.playerX - 0xc0;
  }
  game.cameraX = clamp(game.cameraX, 0, maxCameraX);
  const screenY = Math.trunc(game.playerY) - game.cameraY;
  if (screenY < 0x40 && game.playerVelocityY < 0) {
    game.cameraY = Math.trunc(game.playerY) - 0x40;
  }
  game.cameraY = clamp(game.cameraY, 0, maxCameraY);

  if (game.backgroundReseed) {
    game.backgroundScrollX = BACKGROUND_SCROLL_SEED;
    game.backgroundScrollY = 0;
    game.backgroundReseed = false;
  } else {
    game.backgroundScrollX += ((game.cameraX - previousCameraX) * 32) >> 3;
    game.backgroundScrollY += ((game.cameraY - previousCameraY) * 32) >> 3;
  }

  sm.setScore(game.score);
}

function drawText(x, y, text, style = 0) {
  for (let i = 0; i < text.length; i += 1) {
    const code = text.charCodeAt(i) - 0x20;
    if (code >= 0 && code < 64) {
      const sx = (code % 32) * 8;
      const sy = floorDiv(code, 32) * 8 + style * 16;
      context.drawImage(assets.font, sx, sy, 8, 8, x + i * 8, y, 8, 8);
    }
  }
}

function drawTextCentered(y, text, style = 0) {
  const cells = floorDiv(40 - text.length, 2);
  drawText(cells * 8, y, text, style);
}

function drawMap() {
  context.fillStyle = "#000";
  context.fillRect(0, 0, WIDTH, HEIGHT);
  if (assets.background) {
    let offsetX = -(game.backgroundScrollX >> 5) % WIDTH;
    if (offsetX > 0) offsetX -= WIDTH;
    for (let x = offsetX; x < WIDTH; x += WIDTH) {
      context.drawImage(assets.background, x, 0, WIDTH, HEIGHT);
    }
  }

  const firstRow = Math.max(0, floorDiv(game.cameraY, TILE) - 1);
  const lastRow = Math.min(map.rows, floorDiv(game.cameraY + HEIGHT, TILE) + 2);
  const firstColumn = Math.max(0, floorDiv(game.cameraX, TILE) - 1);
  const lastColumn = Math.min(map.columns, floorDiv(game.cameraX + WIDTH, TILE) + 2);
  for (let row = firstRow; row < lastRow; row += 1) {
    for (let column = firstColumn; column < lastColumn; column += 1) {
      const value = tileAt(column, row);
      if (value === 0) continue;
      const x = column * TILE - game.cameraX;
      const y = row * TILE - game.cameraY;
      context.drawImage(
        assets.tiles,
        (value % 4) * TILE,
        floorDiv(value, 4) * TILE,
        TILE,
        TILE,
        x,
        y,
        TILE,
        TILE,
      );
    }
  }
}

function drawRanking() {
  drawTextCentered(2 * 8, "- RANKING -", 2);
  drawText(9 * 8, 5 * 8, "RANK", 2);
  drawText(0x10 * 8, 5 * 8, "NAME", 2);
  drawText(0x1a * 8, 5 * 8, "SCORE", 2);

  for (let i = 0; i < 10; i += 1) {
    const y = (i * 2 + 7) * 8;
    const suffix = i === 0 ? "ST" : i === 1 ? "ND" : i === 2 ? "RD" : "TH";
    drawText(9 * 8, y, String(i + 1).padStart(2, " ") + suffix, 0);
    let name = game.highScoreNames[i] || "";
    name = name.padEnd(4, "\0").slice(0, 4);
    drawText(0x10 * 8, y, name, 0);
    drawText(0x16 * 8, y, String(game.highScores[i]).padStart(9, " "), 0);
  }
}

function drawMenu() {
  if (sm.menuPage === 0) {
    context.drawImage(assets.title, 0, 0, WIDTH, HEIGHT);
    drawTextCentered(0x13 * 8, "CREATED BY E.HASHIMOTO", 0);
    const items = ["GAME START", "OPTION", "EXIT"];
    for (let i = 0; i < 3; i += 1) {
      drawTextCentered((0x16 + i * 2) * 8, items[i], 2);
    }
    const y = (0x16 + sm.menuIndex * 2) * 8;
    drawText(0x0e * 8, y, "<", 1);
    drawText(0x19 * 8, y, ">", 1);
    return;
  }

  context.drawImage(assets.background, 0, 0, WIDTH, HEIGHT);
  if (sm.menuPage === 2) {
    drawRanking();
    drawTextCentered(0x1c * 8, "BUTTON TO BACK", 1);
    return;
  }

  drawTextCentered(4 * 8, "- OPTION -", 2);
  drawText(5 * 8, 10 * 8, "GAME LEVEL", 2);
  drawText(5 * 8, 0x0c * 8, "JUMP BUTTON ASSIGN", 2);
  drawText(5 * 8, 0x0e * 8, "WAIT", 2);
  drawText(5 * 8, 0x10 * 8, "SOFTWARE VSYNC", 2);
  drawText(0x19 * 8, 0x12 * 8, "RANKING", 2);
  drawText(0x19 * 8, 0x14 * 8, "EXIT", 2);

  const difficulty = clamp(sm.difficulty, 1, 3);
  drawText(0x19 * 8, 10 * 8, LEVEL_NAMES[difficulty - 1], 0);
  drawText(0x19 * 8, 0x0c * 8, `BUTTON ${sm.jumpButton}`, 0);
  drawText(0x19 * 8, 0x0e * 8, sm.waitEnabled ? "ON" : "OFF", 0);
  drawText(0x19 * 8, 0x10 * 8, sm.vsyncEnabled ? "ON" : "OFF", 0);

  const y = (sm.optionIndex * 2 + 10) * 8;
  drawText(0x18 * 8, y, "<", 1);
  drawText(0x22 * 8, y, ">", 1);
}

function drawHud() {
  context.fillStyle = "#000";
  context.fillRect(256, 0, 64, HEIGHT);

  let highScore = Math.max(...game.highScores);
  if (game.score > highScore) highScore = game.score;

  drawText(0x21 * 8, 2 * 8, "HIGH", 1);
  drawText(0x22 * 8, 3 * 8, "SCORE", 1);
  drawText(0x21 * 8, 4 * 8, String(highScore).padStart(6, " "), 0);

  drawText(0x22 * 8, 8 * 8, "SCORE", 1);
  drawText(0x21 * 8, 9 * 8, String(game.score).padStart(6, " "), 0);

  drawText(0x22 * 8, 0xd * 8, "POWER", 1);
  const filled = clamp(30 - game.jumpEnergy, 0, 30);
  for (let i = 1; i <= 30; i += 1) {
    const sourceY = i <= filled ? 1 : 0;
    context.drawImage(assets.power, 0, sourceY, 8, 1, 301, 145 - i, 8, 1);
  }
  context.drawImage(assets.power, 8, 0, 24, 40, 288, 112, 24, 40);

  drawText(0x23 * 8, 0x1a * 8, "TIME", 1);
  drawText(0x24 * 8, 0x1b * 8, String(game.timeRemaining).padStart(3, " "), 0);

  if (sm.state === STATE.Gameplay || sm.state === STATE.LevelIntro) {
    game.timeAccumulator += 1;
    if (game.timeAccumulator > 0x3b) {
      game.timeAccumulator = 0;
      if (game.timeRemaining > 0) game.timeRemaining -= 1;
    }
    if (game.timeRemaining === 0) {
      sm.setScore(game.score);
      sm.enterMiss();
    }
  }
}

function drawRankingScreen() {
  context.drawImage(assets.background, 0, 0, WIDTH, HEIGHT);
  drawRanking();
  if (sm.state === STATE.Rank && sm.nameLength < 3) {
    drawText((0x10 + sm.nameLength) * 8, (sm.rankIndex * 2 + 7) * 8,
      sm.currentNameChar(), 1);
  }
}

function drawPostClear() {
  context.drawImage(assets.background, 0, 0, WIDTH, HEIGHT);
  const phaseX = [0x28, 0x50, 0x78, 0x50][floorDiv(game.bonusAnimation, 8) & 3];
  for (let index = 0; index < 5; index += 1) {
    context.drawImage(assets.player, phaseX, 0xa0, 0x28, 0x28,
      index * 0x28 + 0x1c, 0xb8, 0x28, 0x28);
  }
  game.score = sm.score;
  drawHud();

  const level = clamp(sm.difficulty, 1, 5);
  drawText(0x0b * 8, 6 * 8, `LEVEL ${LEVEL_NAMES[level - 1]}`, 2);
  drawText(0x0b * 8, 8 * 8, "- CLEAR! -", 2);
  drawText(4 * 8, 0x0d * 8, "BUNGLUWA BONUS       PTS", 2);
  drawText(4 * 8, 0x0f * 8, "TIME     BONUS       PTS", 2);
  drawText(4 * 8, 0x13 * 8, "TOTAL    BONUS       PTS", 2);

  drawText(0x12 * 8, 0x0d * 8,
    String(game.breakableCount * 100 * level).padStart(6, " "), 0);
  drawText(0x12 * 8, 0x0f * 8,
    String(game.timeRemaining * 100).padStart(6, " "), 0);
  drawText(0x12 * 8, 0x13 * 8,
    String(sm.bonus).padStart(6, " "), 0);
}

function drawEffects() {
  for (const effect of game.effects) {
    if (!effect.active || effect.type === 1) continue;
    if ((effect.cooldown & 1) !== 0) continue;
    const frame = lookupSpriteFrame(effect.spriteIndex + effect.animFrame);
    if (!frame) continue;
    const x = (effect.xFixed >> 5) - game.cameraX - floorDiv(frame.width, 2);
    const y = (effect.yFixed >> 5) - game.cameraY - floorDiv(frame.height, 2);
    if (x + frame.width <= 0 || x >= WIDTH ||
        y + frame.height <= 0 || y >= HEIGHT) {
      continue;
    }
    context.drawImage(frame.canvas, frame.sx, frame.sy, frame.width, frame.height,
      x, y, frame.width, frame.height);
  }
}

function drawFrame() {
  const state = sm.state;

  if (state === STATE.MenuInit || state === STATE.Menu) {
    drawMenu();
    return;
  }
  if (state === STATE.Rank || state === STATE.Result) {
    drawRankingScreen();
    return;
  }
  if (state === STATE.PostClear) {
    if (!game.wasPostClear) {
      game.bonusAnimation = 0;
      game.wasPostClear = true;
    }
    drawPostClear();
    game.bonusAnimation += 1;
    return;
  }
  game.wasPostClear = false;

  if (state === STATE.ExitDialog) {
    context.fillStyle = "#000";
    context.fillRect(0, 0, WIDTH, HEIGHT);
    drawTextCentered(0x0e * 8, "EXIT ESC KEY", 2);
    drawTextCentered(0x12 * 8, "PRESS ESC TO RETURN", 0);
    return;
  }
  if (state === STATE.Continue) {
    const seconds = Math.max(0, 9 - floorDiv(sm.frameCounter, 0x78));
    drawText(0x0e * 8, 0x0d * 8, "CONTINUE", 2);
    drawText(0x17 * 8, 0x0d * 8, String(seconds), 0);
    return;
  }

  drawMap();
  if (!game.playerDead) {
    context.drawImage(
      assets.player,
      (game.playerFrame % 4) * 40,
      floorDiv(game.playerFrame, 4) * 40,
      40,
      40,
      game.playerX - game.cameraX - 20,
      Math.trunc(game.playerY) - game.cameraY - 20,
      40,
      40,
    );
  }
  drawEffects();
  drawHud();

  if (state === STATE.Gameplay) {
    const level = clamp(sm.difficulty, 1, 5);
    drawText(0x0b * 8, 0x0e * 8, `LEVEL ${LEVEL_NAMES[level - 1]}`, 2);
    drawText(0x0b * 8, 0x10 * 8, "- START! -", 2);
  }
  if (state === STATE.GameOver) {
    drawText(0x0b * 8, 0x0e * 8, "GAME  OVER", 1);
  }
}

function updateMenuMusic() {
  const state = sm.state;
  if (state === STATE.MenuInit || state === STATE.Menu) {
    if (audio.music.name === "init") {
      if (audio.music.finished()) audio.music.play("rank", true);
    } else if (!audio.music.name) {
      audio.music.play("rank", true);
    }
  } else if (state === STATE.Gameplay || state === STATE.LevelIntro) {
    if (audio.music.name !== "main") audio.music.play("main", true);
  } else if (state === STATE.Rank || state === STATE.Result) {
    if (audio.music.name !== "rank" &&
        (!audio.music.name || audio.music.finished())) {
      audio.music.play("rank", true);
    }
  }
}

function tick() {
  sm.tick();
  game.playing = [STATE.Gameplay, STATE.LevelIntro, STATE.Miss, STATE.GameOver,
    STATE.Clear, STATE.PostClear].includes(sm.state);
  updatePlaying();
  updateMenuMusic();
}

function isFieldState(state) {
  return state === STATE.Gameplay || state === STATE.LevelIntro ||
    state === STATE.Miss || state === STATE.GameOver || state === STATE.Clear ||
    state === STATE.PostClear;
}

function handleEscape() {
  if (sm.state === STATE.ExitDialog) {
    sm.state = STATE.MenuInit;
    return;
  }
  if (sm.state === STATE.MenuInit || sm.state === STATE.Menu) {
    sm.menuBack();
    return;
  }
  if (isFieldState(sm.state)) {
    sm.onKeyDown(0x1b);
    game.playing = false;
    game.left = false;
    game.right = false;
  }
}

function handleKeyDown(code) {
  keys.add(code);
  audio.resume();

  if (code === "Escape") {
    handleEscape();
    return;
  }

  if (sm.state === STATE.ExitDialog) {
    if (code === "Space" || code === "KeyZ" || code === "KeyX" || code === "Enter") {
      sm.state = STATE.MenuInit;
    }
    return;
  }

  if (sm.state === STATE.MenuInit || sm.state === STATE.Menu) {
    if (code === "ArrowUp") sm.menuMove(-1);
    else if (code === "ArrowDown") sm.menuMove(1);
    else if (code === "ArrowLeft") sm.menuHorizontal(-1);
    else if (code === "ArrowRight") sm.menuHorizontal(1);
    else if (code === "Space" || code === "KeyZ" || code === "KeyX") {
      const action = sm.menuActivate();
      if (action === "start") startNewGame();
      else if (action === "exit") {
        sm.state = STATE.ExitDialog;
        game.playing = false;
      }
    }
    return;
  }

  if (sm.state === STATE.Rank || sm.state === STATE.Result) return;

  if (code === "ArrowLeft") game.left = true;
  if (code === "ArrowRight") game.right = true;
  if (code === "ControlLeft" || code === "ControlRight" || code === "Space") {
    game.jump = true;
  }
}

function handleKeyUp(code) {
  keys.delete(code);
  if (code === "ArrowLeft") game.left = false;
  if (code === "ArrowRight") game.right = false;
}

window.addEventListener("keydown", (event) => {
  const handled = ["ArrowUp", "ArrowDown", "ArrowLeft", "ArrowRight", "Space",
    "KeyZ", "KeyX", "Escape", "Enter", "ControlLeft", "ControlRight"].includes(event.code);
  if (handled) event.preventDefault();
  if (event.repeat &&
      (sm.state === STATE.Rank || sm.state === STATE.Result ||
       sm.state === STATE.ExitDialog)) {
    return;
  }
  handleKeyDown(event.code);
});

window.addEventListener("keyup", (event) => {
  handleKeyUp(event.code);
});

window.addEventListener("blur", () => {
  keys.clear();
  game.left = false;
  game.right = false;
});

window.addEventListener("pointerdown", () => audio.resume(), { passive: true });
window.addEventListener("touchstart", () => audio.resume(), { passive: true });

for (const button of document.querySelectorAll("[data-code]")) {
  const code = button.dataset.code;
  const down = (event) => {
    event.preventDefault();
    handleKeyDown(code);
  };
  const up = (event) => {
    event.preventDefault();
    handleKeyUp(code);
  };
  button.addEventListener("pointerdown", down);
  button.addEventListener("pointerup", up);
  button.addEventListener("pointercancel", up);
  button.addEventListener("pointerleave", up);
}

async function loadAssets() {
  const bitmapEntries = await Promise.all(Object.entries(BMP_FILES).map(
    async ([name, path]) => {
      const bytes = await fetchBinary(new URL(path, assetBase));
      return [name, decodeBmp(bytes)];
    },
  ));
  for (const [name, image] of bitmapEntries) {
    const keyBlack = name !== "title" && name !== "background";
    assets[name] = toCanvas(image, { keyBlack });
  }

  const mapBytes = await fetchBinary(new URL("map/000.map", assetBase));
  map = parseMap(mapBytes);
  initialTiles = map.tiles.slice();

  await loadRank();

  await Promise.all(MIDI_FILES.map(async (name) => {
    try {
      const bytes = await fetchBinary(new URL(`midi/${name}.mid`, assetBase));
      const track = parseMidi(bytes.buffer.slice(bytes.byteOffset,
        bytes.byteOffset + bytes.byteLength));
      audio.music.setTrack(name, track);
    } catch {
      // The fallback durations keep the state machine moving.
    }
  }));

  await Promise.all(WAV_FILES.map(async (name) => {
    try {
      const bytes = await fetchBinary(new URL(`wav/${name}.wav`, assetBase));
      await audio.sfx.load(name, bytes.buffer.slice(bytes.byteOffset,
        bytes.byteOffset + bytes.byteLength));
    } catch {
      // Sound effects are optional.
    }
  }));
}

async function boot() {
  try {
    await loadAssets();
  } catch (error) {
    context.fillStyle = "#000";
    context.fillRect(0, 0, WIDTH, HEIGHT);
    context.fillStyle = "#fff";
    context.font = "12px monospace";
    context.fillText("ASSET LOAD FAILED", 16, 80);
    context.font = "8px monospace";
    context.fillText(String(error.message).slice(0, 44), 16, 104);
    context.fillText("Serve the repository root, not web/ alone.", 16, 128);
    return;
  }

  sm = new GameStateMachine();
  const settings = loadSettings();
  sm.setSettings(settings.difficulty, settings.wait, settings.vsync);
  sm.setJumpButton(settings.jumpButton);

  const step = 1000 / 60;
  let last = performance.now();
  let accumulator = 0;

  function loop(now) {
    requestAnimationFrame(loop);
    if (paused) {
      last = now;
      accumulator = 0;
      return;
    }
    accumulator += Math.min(now - last, 250);
    last = now;
    let steps = 0;
    while (accumulator >= step && steps < 5) {
      accumulator -= step;
      tick();
      steps += 1;
    }
    if (steps > 0) drawFrame();
  }

  drawFrame();
  requestAnimationFrame(loop);
}

// Expose a small read-only-ish handle for automated smoke tests and for
// debugging in the browser console. The game itself never depends on it.
window.__bgw = {
  game,
  STATE,
  get sm() { return sm; },
  get map() { return map; },
  get assets() { return assets; },
  get audio() { return audio; },
  startNewGame,
  tick,
  drawFrame,
  setPaused(value) { paused = Boolean(value); },
};

boot();
