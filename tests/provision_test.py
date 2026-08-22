#!/usr/bin/env python3

# Copyright (C) 2026 Antonio Ricciardi
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


import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(command, **kwargs):
    return subprocess.run(command, check=True, text=True, capture_output=True, **kwargs)


def main() -> int:
    tool = Path(sys.argv[1])
    executable = Path(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="c2t-provision-test-") as directory:
        directory = Path(directory)
        config = directory / "c2t.env"
        provisioned = directory / executable.name
        config.write_text(
            "# Non-secret settings can be kept in this file.\n"
            "C2T_AUTO_RESTART=1\n",
            encoding="utf-8",
        )
        provision_environment = os.environ.copy()
        provision_environment["TELEGRAM_BOT_TOKEN"] = "987654:embedded-test-token"
        provision_environment["TELEGRAM_CHAT_ID"] = "-987654"
        provision_environment["C2T_PROXY"] = "socks5://127.0.0.1:9050"
        if sys.platform == "darwin":
            shutil.copy2(executable, provisioned)
            (directory / ".c2t.env").write_text(
                "TELEGRAM_BOT_TOKEN=987654:embedded-test-token\n"
                "TELEGRAM_CHAT_ID=-987654\n"
                "C2T_PROXY=socks5://127.0.0.1:9050\n",
                encoding="utf-8",
            )
            environment = os.environ.copy()
            environment["C2T_EXPECT_EMBEDDED"] = "1"
            environment.pop("TELEGRAM_BOT_TOKEN", None)
            environment.pop("TELEGRAM_CHAT_ID", None)
            environment.pop("C2T_PROXY", None)
            run([str(provisioned)], env=environment)
            return 0
        run(
            [
                sys.executable,
                str(tool),
                str(executable),
                "--config",
                str(config),
                "--from-env",
                "TELEGRAM_BOT_TOKEN",
                "--from-env",
                "TELEGRAM_CHAT_ID",
                "--from-env",
                "C2T_PROXY",
                "--output",
                str(provisioned),
            ],
            env=provision_environment,
        )
        inspected = run(
            [sys.executable, str(tool), str(provisioned), "--inspect"]
        ).stdout
        if "TELEGRAM_BOT_TOKEN=<redacted>" not in inspected:
            raise RuntimeError("inspect did not redact the bot token")
        if "C2T_PROXY=<redacted>" not in inspected:
            raise RuntimeError("inspect did not redact C2T_PROXY")

        # Verify that secrets are encrypted in the embedded configuration region on disk
        binary_bytes = provisioned.read_bytes()
        magic = b"C2TCFG\x00\xa7\x31\xd5\x6c\x92\xe8\x4b\xf0\x1d"
        offset = binary_bytes.find(magic)
        if offset < 0:
            raise RuntimeError("embedded region not found in provisioned binary")
        raw_embedded_payload = binary_bytes[offset + 32 : offset + 32 + 4096]
        if b"embedded-test-token" in raw_embedded_payload or b"TELEGRAM_BOT_TOKEN=" in raw_embedded_payload:
            raise RuntimeError("secrets found in plaintext in the embedded configuration region")

        environment = os.environ.copy()
        environment["C2T_EXPECT_EMBEDDED"] = "1"
        environment.pop("TELEGRAM_BOT_TOKEN", None)
        environment.pop("TELEGRAM_CHAT_ID", None)
        run([str(provisioned)], env=environment)

        # Test --randomize produces different hashes
        rand1 = directory / "c2t-rand1"
        rand2 = directory / "c2t-rand2"
        run([sys.executable, str(tool), str(executable), "--randomize", "--output", str(rand1)])
        run([sys.executable, str(tool), str(executable), "--randomize", "--output", str(rand2)])
        if rand1.read_bytes() == rand2.read_bytes():
            raise RuntimeError("--randomize produced identical binaries")
        inspected_rand = run([sys.executable, str(tool), str(rand1), "--inspect"]).stdout
        if "C2T_NONCE=" not in inspected_rand:
            raise RuntimeError("C2T_NONCE not found in --randomize inspected binary")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
