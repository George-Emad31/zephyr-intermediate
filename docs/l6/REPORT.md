# L6 – Explaining and reducing a scheduling delay

Board: `qemu_x86`, Zephyr v4.4.0.

## 1. Response-time guarantee

> Control work for event *N* must **start within 10 ms** of the timer expiry
> that created event *N*.

Latency is measured with the cycle counter:
`ready_cyc` is stored in the timer ISR when the event is queued, and the
control thread computes `k_cycle_get_32() - ready_cyc` right after
`k_msgq_get()` returns. A latency above `CONTROL_DEADLINE_US` (10000 µs) is
a deadline miss.

## 2. Instrumentation added

| What | How |
|---|---|
| Sequence-based logs | every log line carries `seq`; the control thread checks `seq == expected_seq` and logs gaps (lost events) |
| Deadline-miss counting | `misses`, `max_latency_us`, summary every 20 events |
| Rate-limited warnings | at most one `DEADLINE MISS` warning per second, with a `(+N suppressed)` count |
| Queue drops | timer ISR counts failed `k_msgq_put()` in an atomic counter |
| Trace events | `sys_trace_named_event("ctl_ready", seq, 0)` in the ISR, `("ctl_done", seq, latency_us)` in control |

## 3. Measurement – before

`control` prio 7, `maintenance` prio 4.

```
[CONTROL] DEADLINE MISS seq=0 latency=45936us deadline=10000us misses=1 (+0 suppressed)
[CONTROL] DEADLINE MISS seq=4 latency=45282us deadline=10000us misses=5 (+3 suppressed)
[SUMMARY] events=40 misses=40 (100%) max_latency=45936us drops=0
```

**Every event misses the deadline by ~35 ms.** Full log: `before_shell.log`.

### Kernel shell (`kernel thread list`)

```
 maintenance  priority: 4  state: pending  Total execution cycles: (16 %)
 control      priority: 7  state: pending  Total execution cycles: (0 %)
```

`maintenance` has a numerically lower – i.e. **more urgent** – priority than
`control`, and uses ~16 % CPU in bursts of 45 ms.

### CTF trace (Babeltrace)

`ctf_before/` decoded with `babeltrace2 ctf_before/` (excerpt in
`before_babeltrace.txt`):

```
.233648  thread_sched_ready: control          <- ISR: msgq_put readies control
.233715  thread_sched_ready: maintenance      <- ISR: sem_give readies maintenance
.233764  named_event: ctl_ready seq=2
.233800  isr_exit
.233848  thread_switched_in: maintenance      <- scheduler picks prio 4 first
.233879  thread_busy_wait_enter: 45000 us
.278895  thread_busy_wait_exit
.279023  thread_switched_in: control          <- control finally runs
.279105  named_event: ctl_done seq=2 latency=45435
```

## 4. Explanation

The timer ISR makes **both** threads ready at the same instant. Zephyr's
scheduler always runs the highest-priority ready thread, so `maintenance`
(prio 4) runs before `control` (prio 7). `maintenance` then busy-waits 45 ms
without yielding, so `control` sits in the ready queue for the whole burst.
The delay is not caused by the queue, the timer, or logging – it is a
**priority inversion of intent**: the thread with the short deadline has the
lower priority.

## 5. Correction

One change: `CONTROL_PRIORITY` 7 → **3**, so control is more urgent than
maintenance (4). This follows deadline-monotonic assignment: shorter
deadline → higher priority. Maintenance has no short deadline, so running it
right after control costs nothing.

## 6. Measurement – after

```
control prio=3  maintenance prio=4
[SUMMARY] events=100 misses=0 (0%) max_latency=355us drops=0
```

```
.233648  thread_sched_ready: control
.233715  thread_sched_ready: maintenance
.233800  isr_exit
.233847  thread_switched_in: control          <- control runs first
.233922  named_event: ctl_done seq=2 latency=258
.234041  thread_switched_in: maintenance      <- then maintenance does its 45 ms
```

| | before | after |
|---|---|---|
| Deadline misses | 100 % | **0 %** |
| Latency (CTF, all events) | 45 426 – 45 437 µs | **258 – 261 µs** |
| Max latency (log, incl. shell load) | 45 936 µs | 355 µs |
| Margin to 10 ms deadline | −35 ms | **+9.6 ms** |

## Reproducing

```sh
# Measurement + kernel shell
west build -b qemu_x86 app && west build -t run
uart:~$ kernel thread list

# CTF capture (trace goes to uart1 -> file)
west build -b qemu_x86 app -d build_trace -- -DEXTRA_CONF_FILE=tracing.conf \
    -DEXTRA_DTC_OVERLAY_FILE=tracing_qemu_x86.overlay \
    "-DQEMU_EXTRA_FLAGS=-serial;file:$PWD/ctf/channel0_0"
west build -d build_trace -t run          # Ctrl+a x after a few seconds
cp $ZEPHYR_BASE/subsys/tracing/ctf/tsdl/metadata ctf/
babeltrace2 ctf/
```
