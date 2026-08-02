#!/usr/bin/env node
/* Generates the firmware.crc32 sidecar file the modem uses to resume/skip
   its page-by-page HTTP download (see the flash-OTA plan) — one little-
   endian uint32 CRC32 per 2048-byte page of firmware.bin, computed with the
   plain zlib/PKZIP CRC32 (poly 0xEDB88320) that Flash_C::crc32OtaPage() on
   the modem also uses (Library/Flash/flash.cpp) — the two independently
   implement the same standard algorithm, so they agree without needing to
   share code.

   This CRC32 is only for verifying the HTTP download from this server to
   the modem's own local flash staging area. It has nothing to do with the
   separate rolling checksum MBC-2's CAN bootloader itself uses (PGN105
   sub2/sub3) — that one is computed by the modem at CAN-relay time, not
   here.

   Usage:
       node make_firmware_crc.js <firmware.bin>

   Pads firmware.bin in place with trailing 0xFF bytes up to a multiple of
   2048 (so every page the modem downloads is exactly 2048 bytes, no
   special-casing a short last page) and writes firmware.crc32 next to it. */

const fs = require('fs');
const path = require('path');

const PAGE_SIZE = 2048;

function crc32(buf) {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < buf.length; i++) {
    crc ^= buf[i];
    for (let bit = 0; bit < 8; bit++) {
      const mask = -(crc & 1);
      crc = (crc >>> 1) ^ (0xEDB88320 & mask);
    }
  }
  return (~crc) >>> 0;
}

function main() {
  const binPath = process.argv[2];
  if (!binPath) {
    console.error('usage: node make_firmware_crc.js <firmware.bin>');
    process.exit(1);
  }

  let data = fs.readFileSync(binPath);

  const pad = (PAGE_SIZE - (data.length % PAGE_SIZE)) % PAGE_SIZE;
  if (pad > 0) {
    data = Buffer.concat([data, Buffer.alloc(pad, 0xFF)]);
    fs.writeFileSync(binPath, data);
    console.log(`padded ${binPath} with ${pad} bytes of 0xFF (now ${data.length} bytes, ${data.length / PAGE_SIZE} pages)`);
  }

  const pageCount = data.length / PAGE_SIZE;
  const crcFile = Buffer.alloc(pageCount * 4);
  for (let page = 0; page < pageCount; page++) {
    const pageBuf = data.subarray(page * PAGE_SIZE, (page + 1) * PAGE_SIZE);
    crcFile.writeUInt32LE(crc32(pageBuf), page * 4);
  }

  const crcPath = path.join(path.dirname(binPath), 'firmware.crc32');
  fs.writeFileSync(crcPath, crcFile);
  console.log(`wrote ${crcPath} (${pageCount} page CRCs)`);
}

main();
