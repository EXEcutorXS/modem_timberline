/* Firmware filename signature. Must match host/tools/sign_firmware.py and
   Library/Crypto/verify.cpp: the signed text is
   TLFW1|<version>|<flashBase 8 lowercase hex>|<length>|<sha256 hex>. */
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const FILENAME_RE = /^([0-9]+(?:\.[0-9]+){0,3})_0x([0-9A-Fa-f]+)_([0-9A-Fa-f]{128})\.bin$/;
const publicKey = crypto.createPublicKey(
    fs.readFileSync(path.join(__dirname, 'keys', 'firmware-ed25519.pub.pem'))
);

function firmwareMessage(version, flashBaseHex, length, sha256hex) {
    const base = (parseInt(flashBaseHex, 16) >>> 0).toString(16).padStart(8, '0');
    return `TLFW1|${version}|${base}|${length}|${sha256hex}`;
}

function verifyBuffer(version, flashBaseHex, buf, sigHex) {
    if (!/^[0-9a-fA-F]{128}$/.test(sigHex)) return false;
    const hash = crypto.createHash('sha256').update(buf).digest('hex');
    const msg = Buffer.from(firmwareMessage(version, flashBaseHex, buf.length, hash), 'utf8');
    return crypto.verify(null, msg, publicKey, Buffer.from(sigHex, 'hex'));
}

/* null when the name is unsigned or the signature does not match the bytes. */
function inspectFile(filePath, fileName) {
    const m = FILENAME_RE.exec(fileName);
    if (!m) return null;
    let data;
    try {
        data = fs.readFileSync(filePath);
    } catch (e) {
        return null;
    }
    if (!verifyBuffer(m[1], m[2], data, m[3])) return null;
    return {
        fileName,
        version: m[1],
        flashBaseHex: (parseInt(m[2], 16) >>> 0).toString(16).padStart(8, '0'),
        length: data.length,
        sigHex: m[3].toLowerCase(),
    };
}

module.exports = { FILENAME_RE, firmwareMessage, verifyBuffer, inspectFile };
