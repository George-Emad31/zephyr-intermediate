# L8 – temp_alarm coverage: Task 1 vs Task 2

Command (run on `qemu_x86`, because `native_sim` cannot build on macOS):

```sh
west twister -T tests/temp_alarm -p qemu_x86 --force-platform \
    --coverage --coverage-platform qemu_x86 --coverage-tool gcovr \
    --gcov-tool $ZEPHYR_SDK_INSTALL_DIR/gnu/x86_64-zephyr-elf/bin/x86_64-zephyr-elf-gcov \
    --coverage-basedir app/modules/temp_alarm
```

On Linux the homework's command is the same with `-p native_sim --gcov-tool gcov`.

## Results for `temp_alarm.c`

| | at `l8-task1` | at `l8-task2` |
|---|---|---|
| Lines | 79.6 % (43/54) | **100 % (54/54)** |
| Functions | 100 % (5/5) | 100 % (5/5) |
| Branches | 55.6 % (10/18) | **100 % (18/18)** |

Both match the expected numbers in `HOMEWORK.md`. The report from the second
run is in `twister-out/coverage/index.html`.

## What Task 2 reached that Task 1 could not

The 11 lines Task 1 missed are all error returns. Each `if` around them was
only ever taken one way (partial branch), so 8 of 18 branches were missing.

| Line(s) | Code | Reached by (Task 2 test) |
|---|---|---|
| 31–33 | `!device_is_ready()` → `-ENODEV` | `test_init_without_device` |
| 36–37 | init threshold out of range → `-EINVAL` | `test_init_threshold_out_of_range` |
| 53–54 | set_threshold out of range → `-EINVAL` | `test_set_threshold_out_of_range` |
| 70–73 | fetch failed → store `last_error`, return | `test_fetch_error_propagates`, `test_fail_then_recover` |
| 77–80 | channel_get failed → store `last_error`, return | `test_channel_get_error_propagates` |
| 109–110 | `status == NULL` → `-EINVAL` | `test_get_status_null` |

Function coverage was 100 % both times: every function was called by the
happy-path tests, but only their success paths ran. Line and branch coverage
are what show the untested error handling.
