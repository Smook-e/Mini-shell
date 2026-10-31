#!/usr/bin/env python3
import os
import pty
import select
import signal
import sys
import time


def main():
    shell = os.path.abspath(
        os.path.join(os.path.dirname(__file__), os.pardir, "myshell")
    )
    pid, descriptor = pty.fork()
    if pid == 0:
        os.execl(shell, "myshell")

    def read_until(expected, timeout=5):
        output = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            ready, _, _ = select.select([descriptor], [], [], 0.1)
            if ready:
                try:
                    output += os.read(descriptor, 4096)
                except OSError:
                    break
                if expected in output:
                    return output
        raise RuntimeError(f"timed out waiting for {expected!r}: {output!r}")

    try:
        read_until(b"$ ")
        os.write(descriptor, b"sleep 100\n")
        time.sleep(0.5)
        os.write(descriptor, b"\x1a")
        assert b"Stopped" in read_until(b"$ ")

        os.write(descriptor, b"jobs\n")
        assert b"Stopped" in read_until(b"$ ")

        os.write(descriptor, b"bg %1\n")
        background = read_until(b"$ ")
        assert b"job not found" not in background, background
        assert b"usage:" not in background, background
        os.write(descriptor, b"fg %1\n")
        time.sleep(0.5)
        os.write(descriptor, b"\x03")
        foreground = read_until(b"$ ")
        assert b"job not found" not in foreground, foreground
        assert b"usage:" not in foreground, foreground

        os.write(descriptor, b"sleep 100 &\n")
        assert b"[" in read_until(b"$ ")
        os.write(descriptor, b"\x03")
        time.sleep(0.2)
        os.write(descriptor, b"jobs\n")
        background_jobs = read_until(b"$ ")
        assert b"Running" in background_jobs, background_jobs

        os.write(descriptor, b"fg %2\n")
        time.sleep(0.5)
        os.write(descriptor, b"\x03")
        read_until(b"$ ")

        os.write(descriptor, b"exit\n")
        os.close(descriptor)
        os.waitpid(pid, 0)
    except Exception:
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        raise

    print("interactive job-control smoke test passed")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
