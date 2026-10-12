#!/usr/bin/env -S deno run --allow-read --allow-write

import { Buffer } from "node:buffer";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import {
  basename,
  dirname,
  isAbsolute,
  relative,
  resolve,
  sep,
} from "node:path";
import process from "node:process";
import { inflateRawSync } from "node:zlib";

const EOCD_SIG = 0x06054b50;
const CD_SIG = 0x02014b50;
const LOCAL_SIG = 0x04034b50;
const EOCD_SIZE = 22;
const CD_FIXED = 46;
const LOCAL_FIXED = 30;
const METHOD_STORE = 0;
const METHOD_DEFLATE = 8;

interface Entry {
  name: string;
  method: number;
  crc: number;
  compressedSize: number;
  size: number;
  dataStart: number;
}

interface Bundle {
  start: number;
  end: number;
  entries: Entry[];
  name?: string;
}

const tryParse = (buf: Buffer, eocd: number): Bundle | null => {
  if (eocd + EOCD_SIZE > buf.length) return null;

  const commentLen = buf.readUInt16LE(eocd + 20);
  const end = eocd + EOCD_SIZE + commentLen;
  if (end > buf.length) return null;

  const thisDisk = buf.readUInt16LE(eocd + 4);
  const cdDisk = buf.readUInt16LE(eocd + 6);
  const diskEntries = buf.readUInt16LE(eocd + 8);
  const total = buf.readUInt16LE(eocd + 10);
  const cdSize = buf.readUInt32LE(eocd + 12);
  const cdOffset = buf.readUInt32LE(eocd + 16);

  if (thisDisk || cdDisk || diskEntries !== total) return null;
  if (total === 0 || total === 0xffff || cdOffset === 0xffffffff) return null;

  const start = eocd - cdSize - cdOffset;
  if (start < 0) return null;

  const cdStart = start + cdOffset;
  let p = cdStart;
  const entries: Entry[] = [];

  for (let i = 0; i < total; i++) {
    if (p + CD_FIXED > eocd || buf.readUInt32LE(p) !== CD_SIG) return null;
    if (buf.readUInt16LE(p + 34) !== 0) return null;

    const method = buf.readUInt16LE(p + 10);
    const crc = buf.readUInt32LE(p + 16);
    const compressedSize = buf.readUInt32LE(p + 20);
    const size = buf.readUInt32LE(p + 24);
    const nameLen = buf.readUInt16LE(p + 28);
    const extraLen = buf.readUInt16LE(p + 30);
    const commentLen2 = buf.readUInt16LE(p + 32);
    const localOffset = buf.readUInt32LE(p + 42);
    if (
      compressedSize === 0xffffffff || size === 0xffffffff ||
      localOffset === 0xffffffff
    ) return null;

    const recordEnd = p + CD_FIXED + nameLen + extraLen + commentLen2;
    if (recordEnd > eocd) return null;
    const name = buf.toString("utf8", p + CD_FIXED, p + CD_FIXED + nameLen);

    const lh = start + localOffset;
    if (lh + LOCAL_FIXED > cdStart || buf.readUInt32LE(lh) !== LOCAL_SIG) {
      return null;
    }
    const dataStart = lh + LOCAL_FIXED + buf.readUInt16LE(lh + 26) +
      buf.readUInt16LE(lh + 28);
    if (dataStart + compressedSize > cdStart) return null;

    entries.push({ name, method, crc, compressedSize, size, dataStart });
    p = recordEnd;
  }

  if (p !== eocd) return null;
  return { start, end, entries };
};

const findBundles = (buf: Buffer): Bundle[] => {
  const sig = Buffer.alloc(4);
  sig.writeUInt32LE(EOCD_SIG);

  const found: Bundle[] = [];
  for (let at = buf.indexOf(sig); at !== -1; at = buf.indexOf(sig, at + 1)) {
    const b = tryParse(buf, at);
    if (b) found.push(b);
  }

  found.sort((a, b) => a.start - b.start || b.end - a.end);
  const result: Bundle[] = [];
  for (const b of found) {
    const nested = result.some((k) => b.start >= k.start && b.end <= k.end);
    if (!nested) result.push(b);
  }
  return result;
};

type SymMap = Map<number, string>;

const BUNDLE_SYM = /^bundle_(.+)_data$/;

const cstr = (buf: Buffer, at: number, limit = buf.length): string => {
  let e = at;
  while (e < limit && buf[e] !== 0) e++;
  return buf.toString("latin1", at, e);
};

const addSym = (out: SymMap, raw: string, offset: number): void => {
  const m = BUNDLE_SYM.exec(raw.startsWith("_") ? raw.slice(1) : raw);
  if (m) out.set(offset, m[1]);
};

const readElf = (buf: Buffer, base: number, out: SymMap): void => {
  if (buf[base + 5] !== 1) return;
  const is64 = buf[base + 4] === 2;
  const u64 = (at: number) => Number(buf.readBigUInt64LE(at));
  const shoff = is64 ? u64(base + 0x28) : buf.readUInt32LE(base + 0x20);
  const shentsize = buf.readUInt16LE(base + (is64 ? 0x3a : 0x2e));
  const shnum = buf.readUInt16LE(base + (is64 ? 0x3c : 0x30));
  if (!shoff || !shnum) return;

  const secs = Array.from({ length: shnum }, (_, i) => {
    const h = base + shoff + i * shentsize;
    return is64
      ? {
        type: buf.readUInt32LE(h + 4),
        addr: u64(h + 16),
        off: u64(h + 24),
        size: u64(h + 32),
        link: buf.readUInt32LE(h + 40),
      }
      : {
        type: buf.readUInt32LE(h + 4),
        addr: buf.readUInt32LE(h + 12),
        off: buf.readUInt32LE(h + 16),
        size: buf.readUInt32LE(h + 20),
        link: buf.readUInt32LE(h + 24),
      };
  });

  const symSize = is64 ? 24 : 16;
  for (const s of secs) {
    if (s.type !== 2 && s.type !== 3) continue;
    const str = secs[s.link];
    if (!str) continue;
    for (let i = 0; i < Math.floor(s.size / symSize); i++) {
      const p = base + s.off + i * symSize;
      const shndx = buf.readUInt16LE(p + (is64 ? 6 : 14));
      if (shndx === 0 || shndx >= 0xff00) continue;
      const target = secs[shndx];
      if (!target || target.type === 8) continue;
      const value = is64 ? u64(p + 8) : buf.readUInt32LE(p + 4);
      const offset = base + target.off + (value - target.addr);
      addSym(out, cstr(buf, base + str.off + buf.readUInt32LE(p)), offset);
    }
  }
};

const readMachO = (buf: Buffer, base: number, out: SymMap): void => {
  const is64 = buf.readUInt32LE(base) === 0xfeedfacf;
  const u64 = (at: number) => Number(buf.readBigUInt64LE(at));
  const ncmds = buf.readUInt32LE(base + 16);
  let p = base + (is64 ? 32 : 28);

  const segs: { vmaddr: number; fileoff: number; filesize: number }[] = [];
  let symoff = 0, nsyms = 0, stroff = 0;
  for (let i = 0; i < ncmds; i++) {
    const cmd = buf.readUInt32LE(p);
    const size = buf.readUInt32LE(p + 4);
    if (cmd === 0x19) {
      segs.push({
        vmaddr: u64(p + 24),
        fileoff: u64(p + 40),
        filesize: u64(p + 48),
      });
    } else if (cmd === 0x1) {
      segs.push({
        vmaddr: buf.readUInt32LE(p + 24),
        fileoff: buf.readUInt32LE(p + 32),
        filesize: buf.readUInt32LE(p + 36),
      });
    } else if (cmd === 0x2) {
      symoff = buf.readUInt32LE(p + 8);
      nsyms = buf.readUInt32LE(p + 12);
      stroff = buf.readUInt32LE(p + 16);
    }
    p += size;
  }

  const nlistSize = is64 ? 16 : 12;
  for (let i = 0; i < nsyms; i++) {
    const q = base + symoff + i * nlistSize;
    if ((buf[q + 4] & 0x0e) !== 0x0e) continue;
    const value = is64 ? u64(q + 8) : buf.readUInt32LE(q + 8);
    const seg = segs.find((s) =>
      value >= s.vmaddr && value < s.vmaddr + s.filesize
    );
    if (!seg) continue;
    const name = cstr(buf, base + stroff + buf.readUInt32LE(q));
    addSym(out, name, base + seg.fileoff + (value - seg.vmaddr));
  }
};

const readCoff = (
  buf: Buffer,
  base: number,
  hdr: number,
  out: SymMap,
): void => {
  const nsec = buf.readUInt16LE(hdr + 2);
  const symPtr = buf.readUInt32LE(hdr + 8);
  const nsyms = buf.readUInt32LE(hdr + 12);
  const optSize = buf.readUInt16LE(hdr + 16);
  if (!symPtr || !nsyms) return;

  const secTable = hdr + 20 + optSize;
  const symBase = base + symPtr;
  const strBase = symBase + nsyms * 18;
  for (let i = 0; i < nsyms;) {
    const p = symBase + i * 18;
    const secNum = buf.readInt16LE(p + 12);
    if (secNum > 0 && secNum <= nsec) {
      const name = buf.readUInt32LE(p) === 0
        ? cstr(buf, strBase + buf.readUInt32LE(p + 4))
        : cstr(buf, p, p + 8);
      const rawPtr = buf.readUInt32LE(secTable + (secNum - 1) * 40 + 20);
      addSym(out, name, base + rawPtr + buf.readUInt32LE(p + 8));
    }
    i += 1 + buf[p + 17];
  }
};

const readArchive = (
  buf: Buffer,
  base: number,
  len: number,
  out: SymMap,
): void => {
  const end = base + len;
  let p = base + 8;
  while (p + 60 <= end && buf.toString("latin1", p + 58, p + 60) === "`\n") {
    let name = buf.toString("latin1", p, p + 16).trim();
    const size = parseInt(buf.toString("latin1", p + 48, p + 58), 10);
    let data = p + 60;
    let dataLen = size;
    if (name.startsWith("#1/")) {
      const n = parseInt(name.slice(3), 10);
      data += n;
      dataLen -= n;
    }
    readObject(buf, data, dataLen, out);
    p += 60 + size + (size & 1);
  }
};

const readObject = (
  buf: Buffer,
  base: number,
  len: number,
  out: SymMap,
): void => {
  try {
    if (len < 20) return;
    const be = buf.readUInt32BE(base);
    const le = buf.readUInt32LE(base);
    if (be === 0x7f454c46) readElf(buf, base, out);
    else if (le === 0xfeedfacf || le === 0xfeedface) readMachO(buf, base, out);
    else if (be === 0xcafebabe) {
      const n = buf.readUInt32BE(base + 4);
      for (let i = 0; i < Math.min(n, 16); i++) {
        const o = base + 8 + i * 20;
        readObject(
          buf,
          base + buf.readUInt32BE(o + 8),
          buf.readUInt32BE(o + 12),
          out,
        );
      }
    } else if (buf.toString("latin1", base, base + 8) === "!<arch>\n") {
      readArchive(buf, base, len, out);
    } else if (buf[base] === 0x4d && buf[base + 1] === 0x5a) {
      const pe = base + buf.readUInt32LE(base + 0x3c);
      if (buf.readUInt32LE(pe) === 0x00004550) readCoff(buf, base, pe + 4, out);
    } else {
      const machine = buf.readUInt16LE(base);
      if (
        (machine === 0x14c || machine === 0x8664 || machine === 0xaa64) &&
        buf.readUInt16LE(base + 16) === 0
      ) {
        readCoff(buf, base, base, out);
      }
    }
  } catch {
  }
};

const readSymbols = (buf: Buffer): SymMap => {
  const out: SymMap = new Map();
  readObject(buf, 0, buf.length, out);
  return out;
};

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

const crc32 = (data: Uint8Array): number => {
  let c = 0xffffffff;
  for (let i = 0; i < data.length; i++) {
    c = CRC_TABLE[(c ^ data[i]) & 0xff] ^ (c >>> 8);
  }
  return (c ^ 0xffffffff) >>> 0;
};

const readEntry = (buf: Buffer, e: Entry): Buffer => {
  const raw = buf.subarray(e.dataStart, e.dataStart + e.compressedSize);
  let out: Buffer;
  if (e.method === METHOD_STORE) out = Buffer.from(raw);
  else if (e.method === METHOD_DEFLATE) out = inflateRawSync(raw);
  else throw new Error(`unsupported compression method ${e.method}`);

  if (out.length !== e.size) {
    throw new Error(`size mismatch (${out.length} != ${e.size})`);
  }
  if (crc32(out) !== e.crc) throw new Error("CRC-32 mismatch");
  return out;
};

const safeJoin = (root: string, name: string): string => {
  const norm = name.replaceAll("\\", "/");
  if (norm.startsWith("/") || /^[A-Za-z]:/.test(norm)) {
    throw new Error(`absolute path in archive: ${name}`);
  }
  const target = resolve(root, norm);
  const rel = relative(root, target);
  if (rel === ".." || rel.startsWith(".." + sep) || isAbsolute(rel)) {
    throw new Error(`path escapes output directory: ${name}`);
  }
  return target;
};

const fmtSize = (n: number): string => {
  if (n < 1024) return `${n} B`;
  if (n < 1024 ** 2) return `${(n / 1024).toFixed(1)} KiB`;
  return `${(n / 1024 ** 2).toFixed(1)} MiB`;
};

const usage = (code = 0) => {
  console.log(`Usage: extract-bundle.ts <binary|.pak> [options]

  -o, --out <dir>    output directory (default: ./<input>.bundles)
  -l, --list         list entries only
  -n, --name <name>  only the bundle created by bundle_add(<name> ...)
  -b, --bundle <n>   only bundle n (as shown by --list)
  -h, --help         show this help`);
  process.exit(code);
};

const main = (): void => {
  const args = process.argv.slice(2);
  let input: string | undefined;
  let outDir: string | undefined;
  let listOnly = false;
  let only: number | undefined;
  let wantName: string | undefined;

  const value = (i: number): string => {
    if (i >= args.length) usage(1);
    return args[i];
  };
  for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === "-h" || a === "--help") usage(0);
    else if (a === "-l" || a === "--list") listOnly = true;
    else if (a === "-o" || a === "--out") outDir = value(++i);
    else if (a === "-n" || a === "--name") wantName = value(++i);
    else if (a === "-b" || a === "--bundle") only = Number(value(++i));
    else if (a.startsWith("-")) usage(1);
    else input = a;
  }
  if (!input) usage(1);
  if (only !== undefined && !Number.isInteger(only)) usage(1);

  const buf = readFileSync(input);
  const bundles = findBundles(buf);
  if (bundles.length === 0) {
    console.error(`No bundle.cmake archives found in ${input}`);
    process.exit(2);
  }

  const names = readSymbols(buf);
  for (const b of bundles) b.name = names.get(b.start);

  const known = bundles.flatMap((b) => (b.name ? [b.name] : []));
  if (wantName !== undefined && !known.includes(wantName)) {
    if (known.length) {
      console.error(
        `No bundle named '${wantName}'. Available: ${known.join(", ")}`,
      );
    } else {
      console.error(
        `No bundle named '${wantName}': couldn't read any bundle names from ${input}.\n` +
          `Names come from bundle_<name>_data symbols, so they're missing in stripped binaries\n` +
          `and MSVC builds (which embed a C byte array). Use -b <n> instead (see --list).`,
      );
    }
    process.exit(2);
  }

  const root = resolve(outDir ?? `${basename(input)}.bundles`);
  const usedDirs = new Set<string>();
  let failures = 0;

  bundles.forEach((b, n) => {
    if (only !== undefined && only !== n) return;
    if (wantName !== undefined && b.name !== wantName) return;

    let dir = b.name ?? `bundle-${n}`;
    if (usedDirs.has(dir)) dir = `${dir}-${n}`;
    usedDirs.add(dir);

    const total = b.entries.reduce((s, e) => s + e.size, 0);
    console.log(
      `bundle-${n}${
        b.name ? ` (${b.name})` : ""
      }: ${b.entries.length} entries, ${fmtSize(total)} ` +
        `(archive at 0x${b.start.toString(16)}, ${fmtSize(b.end - b.start)})`,
    );

    for (const e of b.entries) {
      const isDir = e.name.endsWith("/");
      if (listOnly) {
        const m = e.method === METHOD_STORE ? "store  " : "deflate";
        console.log(`  ${m} ${fmtSize(e.size).padStart(10)}  ${e.name}`);
        continue;
      }
      try {
        const dest = safeJoin(resolve(root, dir), e.name);
        if (isDir) {
          mkdirSync(dest, { recursive: true });
          continue;
        }
        mkdirSync(dirname(dest), { recursive: true });
        writeFileSync(dest, readEntry(buf, e));
        console.log(`  extracted ${e.name}`);
      } catch (err) {
        failures++;
        console.error(`  FAILED ${e.name}: ${(err as Error).message}`);
      }
    }
  });

  if (!listOnly) console.log(`\nOutput: ${root}`);
  if (failures) process.exit(1);
};

main();
