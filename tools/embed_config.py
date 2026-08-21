#!/usr/bin/env python3
"""Provision c2t's reserved configuration region in an ELF, PE or Mach-O executable."""

from __future__ import annotations

import argparse
import binascii
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile


MAGIC = b"C2TCFG\x00\xa7\x31\xd5\x6c\x92\xe8\x4b\xf0\x1d"
VERSION = 1
HEADER_SIZE = 32
PAYLOAD_CAPACITY = 4096
REGION_SIZE = HEADER_SIZE + PAYLOAD_CAPACITY
SENSITIVE_KEYS = {"TELEGRAM_BOT_TOKEN", "TELEGRAM_CHAT_ID"}
ALLOWED_KEYS = {
    "C2T_VERBOSE",
    "TELEGRAM_ENABLED",
    "TELEGRAM_DEDUPLICATE",
    "TELEGRAM_SEND_FILES",
    "TELEGRAM_SEND_WINDOW_INFO",
    "TELEGRAM_MAX_FILE_BYTES",
    "C2T_QUEUE_MAX_BYTES",
    "C2T_QUEUE_MAX_ITEMS",
    "C2T_DELIVERY_ATTEMPTS",
    "C2T_RETRY_DELAY_MS",
    "TELEGRAM_BOT_TOKEN",
    "TELEGRAM_CHAT_ID",
}
FLAG_KEYS = {
    "C2T_VERBOSE",
    "TELEGRAM_ENABLED",
    "TELEGRAM_DEDUPLICATE",
    "TELEGRAM_SEND_FILES",
    "TELEGRAM_SEND_WINDOW_INFO",
}
SIZE_KEYS = ALLOWED_KEYS - FLAG_KEYS - SENSITIVE_KEYS


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
    if version != VERSION:
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
        if key == "TELEGRAM_BOT_TOKEN" and len(value.encode("utf-8")) >= 512:
            raise ProvisionError("TELEGRAM_BOT_TOKEN is too long")
        if key == "TELEGRAM_CHAT_ID" and len(value.encode("utf-8")) >= 128:
            raise ProvisionError("TELEGRAM_CHAT_ID is too long")
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
    length, expected_crc = struct.unpack_from("<II", binary, offset + 20)
    if length == 0:
        return {}
    if length > PAYLOAD_CAPACITY:
        raise ProvisionError("invalid embedded configuration length")
    payload = binary[offset + HEADER_SIZE : offset + HEADER_SIZE + length]
    if binascii.crc32(payload) != expected_crc:
        raise ProvisionError("embedded configuration checksum mismatch")
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ProvisionError("embedded configuration is not UTF-8") from error
    return parse_config(text.splitlines(keepends=True))


def patched_binary(binary: bytes, offset: int, payload: bytes) -> bytes:
    reject_signed_macho(binary)
    region = bytearray(REGION_SIZE)
    region[: len(MAGIC)] = MAGIC
    struct.pack_into("<III", region, 16, VERSION, len(payload), binascii.crc32(payload))
    region[HEADER_SIZE : HEADER_SIZE + len(payload)] = payload
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
    parser.add_argument("--output", type=Path, help="write a provisioned copy here")
    parser.add_argument("--in-place", action="store_true", help="replace the input executable")
    parser.add_argument("--force", action="store_true", help="allow replacing an existing output")
    parser.add_argument("--clear", action="store_true", help="remove embedded configuration")
    parser.add_argument("--inspect", action="store_true", help="list embedded keys; secrets are redacted")
    return parser


def main() -> int:
    parser = argument_parser()
    arguments = parser.parse_args()
    has_configuration = arguments.config is not None or bool(arguments.from_env)
    actions = sum((arguments.clear, arguments.inspect, has_configuration))
    if actions != 1:
        parser.error("choose exactly one of --config, --clear, or --inspect")
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
