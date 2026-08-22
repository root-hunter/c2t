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

            # Test start without --log-file (even with --send-logs): no Log: in stdout, no disk log
            no_disk_started = invoke(
                executable, "start", "--verbose", "--send-logs", environment=environment)
            assert no_disk_started.returncode == 0, no_disk_started
            assert "started (PID " in no_disk_started.stdout
            assert "Log:" not in no_disk_started.stdout
            stopped_no_disk = invoke(executable, "stop", environment=environment)
            assert stopped_no_disk.returncode == 0

            log_file = pathlib.Path(state_dir, "c2t", "c2t.log")
            assert not log_file.exists()

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

            # Test auto-restart feature when daemon worker is killed
            auto_restart_start = invoke(
                executable, "start", "--verbose", "--log-file", "--auto-restart",
                environment=environment)
            assert auto_restart_start.returncode == 0, auto_restart_start
            assert "started (PID " in auto_restart_start.stdout

            auto_status_1 = invoke(executable, "status", environment=environment)
            assert auto_status_1.returncode == 0 and "running" in auto_status_1.stdout
            worker_pid_1 = int(auto_status_1.stdout.split("PID ")[1].split(")")[0])

            # 1. Kill with standard SIGTERM (like 'kill <PID>')
            import signal
            os.kill(worker_pid_1, signal.SIGTERM)

            for _ in range(50):
                time.sleep(0.1)
                auto_status_2 = invoke(executable, "status", environment=environment)
                if auto_status_2.returncode == 0 and "running" in auto_status_2.stdout and "PID " in auto_status_2.stdout:
                    worker_pid_2 = int(auto_status_2.stdout.split("PID ")[1].split(")")[0])
                    if worker_pid_2 != worker_pid_1:
                        break
            else:
                raise AssertionError(f"worker was not respawned after SIGTERM: {auto_status_2.stdout}")

            # 2. Kill with SIGKILL (like 'kill -9 <PID>')
            os.kill(worker_pid_2, signal.SIGKILL)

            for _ in range(50):
                time.sleep(0.1)
                auto_status_3 = invoke(executable, "status", environment=environment)
                if auto_status_3.returncode == 0 and "running" in auto_status_3.stdout and "PID " in auto_status_3.stdout:
                    worker_pid_3 = int(auto_status_3.stdout.split("PID ")[1].split(")")[0])
                    if worker_pid_3 != worker_pid_2:
                        break
            else:
                raise AssertionError(f"worker was not respawned after SIGKILL: {auto_status_3.stdout}")

            # 3. Clean stop with 'c2t stop'
            auto_stop = invoke(executable, "stop", environment=environment)
            assert auto_stop.returncode == 0, auto_stop

            stopped_status = invoke(executable, "status", environment=environment)
            assert stopped_status.returncode == 3 and "stopped" in stopped_status.stdout
        finally:
            invoke(executable, "stop", "--force", environment=environment)

        log = pathlib.Path(state_dir, "c2t", "c2t.log")
        assert log.is_file()
        assert "Shutdown complete" in log.read_text(encoding="utf-8")


if __name__ == "__main__":
    main()
