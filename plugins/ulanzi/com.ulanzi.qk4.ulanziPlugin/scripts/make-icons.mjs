// Generates the action icons. Run: node scripts/make-icons.mjs
import { writeFileSync, mkdirSync } from 'node:fs';
import { deflateSync } from 'node:zlib';

const SIZE = 196;

function crc32(buf) {
  let c;
  const table = [];
  for (let n = 0; n < 256; n++) {
    c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  let crc = 0xffffffff;
  for (const b of buf) crc = table[(crc ^ b) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const len = Buffer.alloc(4);
  len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([len, body, crc]);
}

function png(pixel) {
  const raw = Buffer.alloc((SIZE * 4 + 1) * SIZE);
  for (let y = 0; y < SIZE; y++) {
    raw[y * (SIZE * 4 + 1)] = 0;
    for (let x = 0; x < SIZE; x++) {
      const [r, g, b, a] = pixel(x - SIZE / 2 + 0.5, y - SIZE / 2 + 0.5);
      raw.set([r, g, b, a], y * (SIZE * 4 + 1) + 1 + x * 4);
    }
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(SIZE, 0);
  ihdr.writeUInt32BE(SIZE, 4);
  ihdr.set([8, 6, 0, 0, 0], 8);
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

const AMBER = [255, 176, 0, 255];
const RED = [220, 40, 40, 255];
const CLEAR = [0, 0, 0, 0];

const icons = {
  // a ring: the dial
  dial: (x, y) => {
    const r = Math.hypot(x, y);
    return r > 56 && r < 80 ? AMBER : CLEAR;
  },
  // a rounded square: a button
  button: (x, y) => (Math.max(Math.abs(x), Math.abs(y)) < 64 && Math.hypot(Math.max(Math.abs(x) - 44, 0), Math.max(Math.abs(y) - 44, 0)) < 20 ? AMBER : CLEAR),
  // a filled red disc: transmit
  ptt: (x, y) => (Math.hypot(x, y) < 72 ? RED : CLEAR),
};

mkdirSync('assets/icons', { recursive: true });
for (const [name, pixel] of Object.entries(icons)) writeFileSync(`assets/icons/${name}.png`, png(pixel));
