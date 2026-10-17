#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/tracing/tracing.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_INF);

#define STACK_SIZE            2048
#define CONTROL_PRIORITY         3   /* fix: more urgent than maintenance (4) */
#define MAINTENANCE_PRIORITY     4
#define EVENT_PERIOD_MS        250
#define MAINTENANCE_LOAD_US  45000

/*
 * Response-time guarantee: control work for event N must START within
 * CONTROL_DEADLINE_US of the timer expiry that created event N.
 * Latency = (control thread receives event) - (timer ISR queued event).
 */
#define CONTROL_DEADLINE_US  10000

#define WARN_INTERVAL_MS      1000   /* at most one deadline warning per second */
#define SUMMARY_EVERY           20   /* print statistics every N events */

struct control_event {
    uint32_t seq;
    uint32_t ready_ms;
    uint32_t ready_cyc;
};

K_MSGQ_DEFINE(control_queue, sizeof(struct control_event), 4, 4);
K_SEM_DEFINE(maintenance_start, 0, 1);

/* Events the timer could not queue (visible as sequence gaps) */
static atomic_t queue_drops;

/* ================================================================== */
/*  Timer expiry: creates one control event                           */
/* ================================================================== */

static void event_timer_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    static uint32_t seq;
    struct control_event event = {
        .seq = seq++,
        .ready_ms = k_uptime_get_32(),
        .ready_cyc = k_cycle_get_32(),
    };

    /* Timer expiry runs in interrupt context, so never wait here. */
    int ret = k_msgq_put(&control_queue, &event, K_NO_WAIT);

    if (ret != 0) {
        atomic_inc(&queue_drops);
        return;
    }

    /* Both threads become ready when the timer interrupt returns. */
    k_sem_give(&maintenance_start);

    sys_trace_named_event("ctl_ready", event.seq, 0);
}

K_TIMER_DEFINE(event_timer, event_timer_expiry, NULL);

/* ================================================================== */
/*  Control thread                                                   */
/* ================================================================== */

static void control_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    uint32_t expected_seq = 0;
    uint32_t processed = 0;
    uint32_t misses = 0;
    uint32_t max_latency_us = 0;
    uint32_t suppressed = 0;
    int64_t last_warn_ms = INT64_MIN / 2;

    while (true) {
        struct control_event event;
        int ret = k_msgq_get(&control_queue, &event, K_FOREVER);

        if (ret != 0) {
            LOG_ERR("[CONTROL] receive failed: %d", ret);
            continue;
        }

        uint32_t latency_us = k_cyc_to_us_floor32(k_cycle_get_32() -
                                                  event.ready_cyc);

        if (event.seq != expected_seq) {
            LOG_WRN("[CONTROL] seq gap: expected=%u got=%u (lost %u)",
                    expected_seq, event.seq, event.seq - expected_seq);
        }
        expected_seq = event.seq + 1;

        processed++;
        max_latency_us = MAX(max_latency_us, latency_us);

        bool missed = latency_us > CONTROL_DEADLINE_US;

        if (missed) {
            misses++;

            /* Rate-limited: one warning per interval, count the rest. */
            int64_t now = k_uptime_get();

            if (now - last_warn_ms >= WARN_INTERVAL_MS) {
                LOG_WRN("[CONTROL] DEADLINE MISS seq=%u latency=%uus "
                        "deadline=%uus misses=%u (+%u suppressed)",
                        event.seq, latency_us, CONTROL_DEADLINE_US,
                        misses, suppressed);
                last_warn_ms = now;
                suppressed = 0;
            } else {
                suppressed++;
            }
        }

        LOG_DBG("[CONTROL] processed seq=%u latency=%uus", event.seq, latency_us);

        sys_trace_named_event("ctl_done", event.seq, latency_us);

        if (processed % SUMMARY_EVERY == 0) {
            LOG_INF("[SUMMARY] events=%u misses=%u (%u%%) max_latency=%uus "
                    "drops=%ld",
                    processed, misses, misses * 100 / processed,
                    max_latency_us, (long)atomic_get(&queue_drops));
        }
    }
}

/* ================================================================== */
/*  Background maintenance thread                                    */
/* ================================================================== */

static void maintenance_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    while (true) {
        k_sem_take(&maintenance_start, K_FOREVER);

        /* This work is important, but it has no short deadline. */
        k_busy_wait(MAINTENANCE_LOAD_US);
    }
}

K_THREAD_DEFINE(control, STACK_SIZE, control_fn,
                NULL, NULL, NULL, CONTROL_PRIORITY, 0, 0);

K_THREAD_DEFINE(maintenance, STACK_SIZE, maintenance_fn,
                NULL, NULL, NULL, MAINTENANCE_PRIORITY, 0, 0);

int main(void)
{
    LOG_INF("=== L6 Homework: Runtime Investigation ===");
    LOG_INF("Guarantee: control work starts within %u us of event ready",
            CONTROL_DEADLINE_US);
    LOG_INF("control prio=%d  maintenance prio=%d (lower = more urgent)",
            CONTROL_PRIORITY, MAINTENANCE_PRIORITY);

    k_timer_start(&event_timer, K_MSEC(500), K_MSEC(EVENT_PERIOD_MS));

    return 0;
}
