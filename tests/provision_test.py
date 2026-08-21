#!/usr/bin/env python3

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
            "# Non-secret settings can be kept in this file.\n",
            encoding="utf-8",
        )
        provision_environment = os.environ.copy()
        provision_environment["TELEGRAM_BOT_TOKEN"] = "987654:embedded-test-token"
        provision_environment["TELEGRAM_CHAT_ID"] = "-987654"
        if sys.platform == "darwin":
            shutil.copy2(executable, provisioned)
            (directory / ".c2t.env").write_text(
                "TELEGRAM_BOT_TOKEN=987654:embedded-test-token\n"
                "TELEGRAM_CHAT_ID=-987654\n",
                encoding="utf-8",
            )
            environment = os.environ.copy()
            environment["C2T_EXPECT_EMBEDDED"] = "1"
            environment.pop("TELEGRAM_BOT_TOKEN", None)
            environment.pop("TELEGRAM_CHAT_ID", None)
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
        if "embedded-test-token" in inspected:
            raise RuntimeError("inspect exposed the bot token")

        environment = os.environ.copy()
        environment["C2T_EXPECT_EMBEDDED"] = "1"
        environment.pop("TELEGRAM_BOT_TOKEN", None)
        environment.pop("TELEGRAM_CHAT_ID", None)
        run([str(provisioned)], env=environment)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
