// Dependency-free MIDI player and WAV effect player for the browser port.
// The original ships a small Kbgm32 MIDI player; this module parses the same
// .mid files and schedules them through Web Audio. If Web Audio is unavailable
// (or the browser blocks it), the player still reports accurate one-shot
// durations so the state machine can advance.

const DEFAULT_DURATIONS = {
  init: 1.88,
  main: 139.61,
  miss: 3.73,
  gameover: 7.27,
  rank: 142.77,
  clear: 5.81,
};

function readVarLen(data, index) {
  let value = 0;
  let byte;
  do {
    byte = data[index];
    index += 1;
    value = (value << 7) | (byte & 0x7f);
  } while (byte & 0x80);
  return [value, index];
}

function ascii(data, offset, length) {
  let text = "";
  for (let i = 0; i < length; i += 1) {
    text += String.fromCharCode(data[offset + i] || 0);
  }
  return text;
}

export function parseMidi(buffer) {
  const data = new Uint8Array(buffer);
  const view = new DataView(buffer);
  if (ascii(data, 0, 4) !== "MThd") {
    throw new Error("not a MIDI file");
  }
  const headerLength = view.getUint32(4);
  const trackCount = view.getUint16(10);
  const division = view.getUint16(12);
  if (!division || division & 0x8000) {
    throw new Error("unsupported MIDI timing");
  }

  const notes = [];
  const tempos = [];
  let maxTick = 0;
  let offset = 8 + headerLength;

  for (let track = 0; track < trackCount; track += 1) {
    if (ascii(data, offset, 4) !== "MTrk") break;
    const length = view.getUint32(offset + 4);
    let index = offset + 8;
    const end = index + length;
    let tick = 0;
    let running = 0;
    const programs = new Array(16).fill(0);
    const active = new Map();

    while (index < end) {
      let delta;
      [delta, index] = readVarLen(data, index);
      tick += delta;
      let status = data[index];
      if (status < 0x80) {
        status = running;
      } else {
        index += 1;
        if (status < 0xf0) running = status;
      }

      if (status === 0xff) {
        const meta = data[index];
        index += 1;
        let size;
        [size, index] = readVarLen(data, index);
        if (meta === 0x51 && size >= 3) {
          const microseconds = (data[index] << 16) | (data[index + 1] << 8) | data[index + 2];
          tempos.push({ tick, microseconds });
        }
        index += size;
      } else if (status === 0xf0 || status === 0xf7) {
        let size;
        [size, index] = readVarLen(data, index);
        index += size;
      } else {
        const kind = status & 0xf0;
        const channel = status & 0x0f;
        if (kind === 0xc0) {
          programs[channel] = data[index];
          index += 1;
        } else if (kind === 0xd0) {
          index += 1;
        } else {
          const note = data[index];
          const velocity = data[index + 1];
          index += 2;
          const key = channel * 128 + note;
          if (kind === 0x90 && velocity > 0) {
            active.set(key, {
              tick,
              note,
              velocity,
              channel,
              program: programs[channel],
            });
          } else if (kind === 0x80 || (kind === 0x90 && velocity === 0)) {
            const on = active.get(key);
            if (on) {
              notes.push({ ...on, durationTicks: Math.max(1, tick - on.tick) });
              active.delete(key);
            }
          }
        }
      }
      if (tick > maxTick) maxTick = tick;
    }

    for (const on of active.values()) {
      notes.push({ ...on, durationTicks: Math.max(1, maxTick - on.tick) });
    }
    offset = end;
  }

  tempos.sort((a, b) => a.tick - b.tick);
  if (tempos.length === 0 || tempos[0].tick > 0) {
    tempos.unshift({ tick: 0, microseconds: 500000 });
  }

  const tickToSeconds = (tick) => {
    let seconds = 0;
    let lastTick = 0;
    let microseconds = tempos[0].microseconds;
    for (const tempo of tempos) {
      if (tempo.tick > tick) break;
      seconds += ((tempo.tick - lastTick) * microseconds) / 1e6 / division;
      lastTick = tempo.tick;
      microseconds = tempo.microseconds;
    }
    return seconds + ((tick - lastTick) * microseconds) / 1e6 / division;
  };

  const duration = tickToSeconds(maxTick);
  const events = notes
    .map((note) => {
      const time = tickToSeconds(note.tick);
      return {
        time,
        duration: Math.max(0.04, tickToSeconds(note.tick + note.durationTicks) - time),
        note: note.note,
        velocity: note.velocity,
        channel: note.channel,
        program: note.program,
      };
    })
    .sort((a, b) => a.time - b.time);

  return { duration, notes: events };
}

function waveformFor(program) {
  const p = program & 0x7f;
  if (p < 8) return "triangle";
  if (p < 16) return "sine";
  if (p < 24) return "square";
  if (p < 32) return "sawtooth";
  if (p < 40) return "triangle";
  if (p < 48) return "sawtooth";
  if (p < 56) return "sine";
  if (p < 64) return "square";
  if (p < 72) return "triangle";
  if (p < 80) return "sine";
  if (p < 88) return "square";
  return "sawtooth";
}

function waveformGain(waveform) {
  return waveform === "square" || waveform === "sawtooth" ? 0.09 : 0.15;
}

export class MusicPlayer {
  constructor(context, destination, durations = DEFAULT_DURATIONS) {
    this.context = context;
    this.destination = destination;
    this.durations = durations;
    this.tracks = new Map();
    this.name = null;
    this.playing = false;
    this.loop = false;
    this.fallback = false;
    this.fallbackEnd = 0;
    this.track = null;
    this.baseTime = 0;
    this.index = 0;
    this.timer = null;
    this.voices = new Set();
    this.noise = null;
    if (this.context) {
      const length = Math.floor(this.context.sampleRate * 0.5);
      this.noise = this.context.createBuffer(1, length, this.context.sampleRate);
      const channel = this.noise.getChannelData(0);
      for (let i = 0; i < length; i += 1) {
        channel[i] = Math.random() * 2 - 1;
      }
    }
  }

  setTrack(name, track) {
    this.tracks.set(name, track);
  }

  play(name, loop) {
    this.stop();
    this.name = name;
    this.loop = loop;
    this.playing = true;
    this.track = this.tracks.get(name) || null;
    if (!this.context || !this.track) {
      this.fallback = true;
      const seconds = this.track
        ? this.track.duration
        : (this.durations[name] || 2.0);
      this.fallbackEnd = performance.now() + seconds * 1000;
      return;
    }
    this.fallback = false;
    this.baseTime = this.context.currentTime + 0.08;
    this.index = 0;
    this.schedule();
    this.timer = setInterval(() => this.schedule(), 30);
  }

  schedule() {
    if (!this.playing || this.fallback || !this.track) return;
    const now = this.context.currentTime;
    const horizon = now + 0.35;
    let guard = 0;
    while (guard < 4000) {
      guard += 1;
      if (this.index >= this.track.notes.length) {
        if (this.loop && this.baseTime + this.track.duration <= horizon) {
          this.baseTime += this.track.duration;
          this.index = 0;
          continue;
        }
        break;
      }
      const note = this.track.notes[this.index];
      const at = this.baseTime + note.time;
      if (at > horizon) break;
      this.scheduleNote(note, Math.max(at, now + 0.005));
      this.index += 1;
    }
  }

  scheduleNote(note, at) {
    const context = this.context;
    const end = at + note.duration;
    const gain = context.createGain();
    gain.connect(this.destination);

    if (note.channel === 9) {
      if (!this.noise) return;
      const source = context.createBufferSource();
      source.buffer = this.noise;
      const filter = context.createBiquadFilter();
      filter.type = "bandpass";
      filter.frequency.value = note.note < 40 ? 120 : note.note < 50 ? 900 : 5000;
      filter.Q.value = 0.8;
      source.connect(filter).connect(gain);
      const level = (note.velocity / 127) * 0.10;
      gain.gain.setValueAtTime(0.0001, at);
      gain.gain.exponentialRampToValueAtTime(Math.max(0.0002, level), at + 0.002);
      gain.gain.exponentialRampToValueAtTime(0.0001, at + 0.06);
      source.start(at, Math.random() * 0.2, 0.08);
      this.voices.add(source);
      source.onended = () => {
        this.voices.delete(source);
        gain.disconnect();
      };
      return;
    }

    const oscillator = context.createOscillator();
    const waveform = waveformFor(note.program);
    oscillator.type = waveform;
    oscillator.frequency.value = 440 * 2 ** ((note.note - 69) / 12);
    const level = (note.velocity / 127) * waveformGain(waveform);
    gain.gain.setValueAtTime(0.0001, at);
    gain.gain.exponentialRampToValueAtTime(Math.max(0.0002, level), at + 0.012);
    gain.gain.exponentialRampToValueAtTime(0.0001, Math.max(at + 0.02, end));
    oscillator.connect(gain);
    oscillator.start(at);
    oscillator.stop(end + 0.03);
    this.voices.add(oscillator);
    oscillator.onended = () => {
      this.voices.delete(oscillator);
      gain.disconnect();
    };
  }

  finished() {
    if (!this.playing) return true;
    if (this.fallback) return performance.now() >= this.fallbackEnd;
    if (this.loop) return false;
    return this.context.currentTime >= this.baseTime + this.track.duration + 0.05;
  }

  stop() {
    if (this.timer !== null) {
      clearInterval(this.timer);
      this.timer = null;
    }
    for (const voice of this.voices) {
      try {
        voice.stop();
      } catch {
        // Already stopped.
      }
    }
    this.voices.clear();
    this.playing = false;
    this.name = null;
    this.track = null;
    this.fallback = false;
  }
}

export class SfxPlayer {
  constructor(context, destination) {
    this.context = context;
    this.destination = destination;
    this.buffers = new Map();
  }

  async load(name, buffer) {
    if (!this.context) return;
    try {
      const decoded = await this.context.decodeAudioData(buffer.slice(0));
      this.buffers.set(name, decoded);
    } catch {
      // Ignore undecodable effects; the game still runs.
    }
  }

  play(name) {
    if (!this.context) return;
    const buffer = this.buffers.get(name);
    if (!buffer) return;
    const source = this.context.createBufferSource();
    source.buffer = buffer;
    source.connect(this.destination);
    source.start();
  }
}

export function createAudio(durations = DEFAULT_DURATIONS) {
  let context = null;
  try {
    const AudioContext = window.AudioContext || window.webkitAudioContext;
    if (AudioContext) context = new AudioContext();
  } catch {
    context = null;
  }

  let music = null;
  let sfx = null;
  let master = null;
  if (context) {
    master = context.createGain();
    master.gain.value = 0.24;
    master.connect(context.destination);
    const sfxGain = context.createGain();
    sfxGain.gain.value = 0.55;
    sfxGain.connect(context.destination);
    music = new MusicPlayer(context, master, durations);
    sfx = new SfxPlayer(context, sfxGain);
  } else {
    music = new MusicPlayer(null, null, durations);
    sfx = new SfxPlayer(null, null);
  }

  return {
    context,
    music,
    sfx,
    resume() {
      if (context && context.state === "suspended") {
        context.resume().catch(() => {});
      }
    },
  };
}
