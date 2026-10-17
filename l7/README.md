# Memory isolation exercise — Zephyr on QEMU

A Zephyr application with a network command parser that has an out-of-bounds read bug.
An attacker can use it to read the device's private key. Your task is to isolate the
parser so the bug can no longer reach the key.

It runs on an emulated Cortex-M33 board in QEMU inside Docker, so no hardware is needed.

1. [SETUP.md](SETUP.md) — install, build and run the project.
2. [HOMEWORK_TASK.md](HOMEWORK_TASK.md) — the scenario, the task and the expected result.
