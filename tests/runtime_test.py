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

import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time


def invoke(executable, *arguments, environment):
    return subprocess.run(
        [executable, *arguments], env=environment, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=40)


def get_parent_pid(pid: int) -> int:
    if sys.platform.startswith("linux"):
        try:
            with open(f"/proc/{pid}/status", "r", encoding="utf-8") as f:
                for line in f:
                    if line.startswith("PPid:"):
                        return int(line.split(":")[1].strip())
        except (FileNotFoundError, ValueError, IndexError):
            pass
    try:
        out = subprocess.check_output(["ps", "-o", "ppid=", "-p", str(pid)], text=True).strip()
        if out:
            return int(out)
    except (subprocess.SubprocessError, ValueError, FileNotFoundError):
        pass
    return 0


def get_child_pids(parent_pid: int) -> list:
    children = []
    if sys.platform.startswith("linux"):
        for proc in pathlib.Path("/proc").glob("[0-9]*"):
            try:
                status_path = proc / "status"
                if status_path.is_file():
                    with open(status_path, "r", encoding="utf-8") as f:
                        for line in f:
                            if line.startswith("PPid:"):
                                ppid = int(line.split(":")[1].strip())
                                if ppid == parent_pid:
                                    children.append(int(proc.name))
                                break
            except (FileNotFoundError, ValueError, PermissionError):
                pass
        if children:
            return children
    try:
        out = subprocess.check_output(["pgrep", "-P", str(parent_pid)], text=True)
        for line in out.splitlines():
            line = line.strip()
            if line.isdigit():
                children.append(int(line))
    except (subprocess.SubprocessError, ValueError, FileNotFoundError):
        pass
    return children


def safe_kill(pid: int, sig: int):
    if pid <= 1:
        raise ValueError(f"Refusing to kill PID {pid}")
    if sys.platform.startswith("linux"):
        try:
            with open(f"/proc/{pid}/comm", "r", encoding="utf-8") as f:
                comm = f.read().strip()
                if comm in ("systemd", "init", "wsl-init", "systemd-exec", "gnome-session", "bash", "zsh"):
                    raise ValueError(f"Refusing to kill system process '{comm}' (PID {pid})")
        except FileNotFoundError:
            pass
    elif sys.platform == "darwin":
        try:
            out = subprocess.check_output(["ps", "-o", "comm=", "-p", str(pid)], text=True).strip()
            if any(sys_proc in out for sys_proc in ("launchd", "zsh", "bash", "Finder", "Dock")):
                raise ValueError(f"Refusing to kill system process '{out}' (PID {pid})")
        except (subprocess.SubprocessError, FileNotFoundError):
            pass
    os.kill(pid, sig)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <c2t-executable>", file=sys.stderr)
        sys.exit(1)

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

            # Verify foreground execution
            foreground = subprocess.Popen(
                [executable, "run", "--verbose", "--log-file"], env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                for _ in range(150):
                    foreground_status = invoke(
                        executable, "status", environment=environment)
                    if foreground_status.returncode == 0 and \
                            "running" in foreground_status.stdout:
                        break
                    time.sleep(0.1)
                else:
                    raise AssertionError("foreground process did not become ready")
            finally:
                invoke(executable, "stop", environment=environment)
                foreground.wait(timeout=20)

            # Test auto-restart feature when daemon worker is killed
            auto_restart_start = invoke(
                executable, "start", "--verbose", "--log-file", "--auto-restart",
                environment=environment)
            assert auto_restart_start.returncode == 0, auto_restart_start
            assert "started (PID " in auto_restart_start.stdout

            auto_status_1 = invoke(executable, "status", environment=environment)
            assert auto_status_1.returncode == 0 and "running" in auto_status_1.stdout
            worker_pid_1 = int(auto_status_1.stdout.split("PID ")[1].split(")")[0])

            # 1. Kill worker with standard SIGTERM
            safe_kill(worker_pid_1, signal.SIGTERM)

            for _ in range(150):
                time.sleep(0.1)
                auto_status_2 = invoke(executable, "status", environment=environment)
                if auto_status_2.returncode == 0 and "running" in auto_status_2.stdout and "PID " in auto_status_2.stdout:
                    worker_pid_2 = int(auto_status_2.stdout.split("PID ")[1].split(")")[0])
                    if worker_pid_2 != worker_pid_1:
                        break
            else:
                raise AssertionError(f"worker was not respawned after SIGTERM: {auto_status_2.stdout}")

            # 2. Kill worker with SIGKILL (kill -9)
            safe_kill(worker_pid_2, signal.SIGKILL)

            for _ in range(150):
                time.sleep(0.1)
                auto_status_3 = invoke(executable, "status", environment=environment)
                if auto_status_3.returncode == 0 and "running" in auto_status_3.stdout and "PID " in auto_status_3.stdout:
                    worker_pid_3 = int(auto_status_3.stdout.split("PID ")[1].split(")")[0])
                    if worker_pid_3 != worker_pid_2:
                        break
            else:
                raise AssertionError(f"worker was not respawned after SIGKILL: {auto_status_3.stdout}")

            # 3. Test mutual resilience: Kill supervisor process with SIGKILL and verify worker daemon restores supervisor
            sup_pid_1 = get_parent_pid(worker_pid_3)
            assert sup_pid_1 > 1, f"invalid supervisor PID: {sup_pid_1}"

            # Verify supervisor is alive
            safe_kill(sup_pid_1, 0)

            # Kill supervisor with SIGKILL
            safe_kill(sup_pid_1, signal.SIGKILL)

            # Verify supervisor process was killed
            for _ in range(50):
                time.sleep(0.1)
                try:
                    safe_kill(sup_pid_1, 0)
                except ProcessLookupError:
                    break
            else:
                raise AssertionError(f"supervisor (PID {sup_pid_1}) did not terminate after SIGKILL")

            # Wait for worker daemon to restore supervisor
            sup_pid_2 = 0
            for _ in range(200):
                time.sleep(0.1)
                auto_status_4 = invoke(executable, "status", environment=environment)
                if auto_status_4.returncode == 0 and "running" in auto_status_4.stdout:
                    children = get_child_pids(worker_pid_3)
                    for sup_candidate in children:
                        if sup_candidate > 1 and sup_candidate != sup_pid_1:
                            try:
                                safe_kill(sup_candidate, 0)
                                sup_pid_2 = sup_candidate
                                break
                            except ProcessLookupError:
                                pass
                    if sup_pid_2:
                        break
            else:
                raise AssertionError("supervisor process was not restored by worker daemon after SIGKILL")

            assert sup_pid_2 > 1 and sup_pid_2 != sup_pid_1

            # Give restored supervisor a brief moment to settle and write state
            time.sleep(0.3)

            # 4. Test SECOND supervisor kill cycle to verify permanent mutual resilience
            safe_kill(sup_pid_2, signal.SIGKILL)

            sup_pid_3 = 0
            for i in range(200):
                time.sleep(0.1)
                auto_status_5 = invoke(executable, "status", environment=environment)
                if auto_status_5.returncode == 0 and "running" in auto_status_5.stdout:
                    children = get_child_pids(worker_pid_3)
                    for sup_candidate in children:
                        if sup_candidate > 1 and sup_candidate != sup_pid_2:
                            try:
                                safe_kill(sup_candidate, 0)
                                sup_pid_3 = sup_candidate
                                break
                            except ProcessLookupError:
                                pass
                    if sup_pid_3:
                        break
            else:
                raise AssertionError("supervisor process was not restored on second SIGKILL cycle")

            assert sup_pid_3 > 1 and sup_pid_3 != sup_pid_2

            # Give restored supervisor a brief moment to settle and write state
            time.sleep(0.3)

            # Verify worker daemon worker_pid_3 remained alive throughout both supervisor kills
            safe_kill(worker_pid_3, 0)

            # 5. Clean stop with 'c2t stop'
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
