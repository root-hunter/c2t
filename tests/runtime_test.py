#!/usr/bin/env python3
import os
import pathlib
import subprocess
import sys
import tempfile
import time


def invoke(executable, *arguments, environment):
    return subprocess.run(
        [executable, *arguments], env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)


def main():
    executable = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="c2t-runtime-") as runtime_dir, \
            tempfile.TemporaryDirectory(prefix="c2t-state-") as state_dir:
        environment = os.environ.copy()
        environment.update({
            "XDG_RUNTIME_DIR": runtime_dir,
            "XDG_STATE_HOME": state_dir,
            "DISPLAY": ":c2t-test-invalid-display",
            "TELEGRAM_ENABLED": "0",
        })
        try:
            stopped = invoke(executable, "status", environment=environment)
            assert stopped.returncode == 3, stopped
            assert "stopped" in stopped.stdout

            started = invoke(
                executable, "start", "--verbose", "--log-file", environment=environment)
            assert started.returncode == 0, started
            assert "started (PID " in started.stdout
            assert "Log:" in started.stdout

            running = invoke(executable, "status", environment=environment)
            assert running.returncode == 0, running
            assert "running (PID " in running.stdout

            duplicate = invoke(executable, "start", environment=environment)
            assert duplicate.returncode == 0, duplicate
            assert "already running" in duplicate.stdout

            restarted = invoke(
                executable, "restart", "--verbose", "--log-file", environment=environment)
            assert restarted.returncode == 0, restarted
            assert "started (PID " in restarted.stdout

            stopped = invoke(executable, "stop", environment=environment)
            assert stopped.returncode == 0, stopped
            assert "stopped" in stopped.stdout

            final = invoke(executable, "status", environment=environment)
            assert final.returncode == 3, final

            foreground = subprocess.Popen(
                [executable, "run", "--verbose", "--log-file"], env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            for _ in range(50):
                foreground_status = invoke(
                    executable, "status", environment=environment)
                if foreground_status.returncode == 0 and \
                        "running" in foreground_status.stdout:
                    break
                time.sleep(0.1)
            else:
                raise AssertionError("foreground process did not become ready")
            foreground_stop = invoke(
                executable, "stop", environment=environment)
            assert foreground_stop.returncode == 0, foreground_stop
            assert foreground.wait(timeout=20) == 0
        finally:
            invoke(executable, "stop", "--force", environment=environment)

        log = pathlib.Path(state_dir, "c2t", "c2t.log")
        assert log.is_file()
        assert "Shutdown complete" in log.read_text(encoding="utf-8")


if __name__ == "__main__":
    main()
