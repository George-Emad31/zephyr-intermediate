/*
 * Lecture 5 - Task 1: Reliability under pressure
 *
 * - producer puts a sensor sample into a k_msgq every 100ms
 * - consumer takes samples from the queue and feeds a task watchdog
 * - at sample STUCK_AT_SEQ the consumer gets stuck (long k_sleep):
 *     queue fills -> producer starts dropping -> watchdog callback fires
 * - health thread monitors the queue fill level and warns at 75%
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(l5_task1, LOG_LEVEL_DBG);

#define STACK_SIZE          2048
#define SAMPLE_COUNT          30
#define PRODUCER_PERIOD_MS   100
#define CONSUMER_WORK_MS      50    /* normal processing, faster than producer */

#define QUEUE_LEN              8
#define QUEUE_WARN_PCT        75

#define WDT_TIMEOUT_MS      1000    /* consumer must feed at least this often */
#define STUCK_AT_SEQ          10    /* consumer hangs on this sample */
#define STUCK_MS            3000    /* ...for this long (> WDT_TIMEOUT_MS) */

#define HEALTH_PERIOD_MS     100

/* ================================================================== */
/*  Queue                                                             */
/* ================================================================== */

struct sensor_data {
    int32_t temperature_mc;
    uint32_t timestamp_ms;
    uint8_t seq;
};

K_MSGQ_DEFINE(sensor_q, sizeof(struct sensor_data), QUEUE_LEN, 4);

/* Statistics */
static int total_dropped;
static int total_consumed;

/* ================================================================== */
/*  Task watchdog callback                                            */
/* ================================================================== */

static void consumer_wdt_cb(int channel_id, void *user_data)
{
    const char *name = user_data;

    /*
     * Runs in timer (ISR) context: keep it short, no blocking.
     * A production system would save diagnostics and call
     * sys_reboot() here. We only log so the recovery can be observed.
     */
    LOG_ERR("[WDT] channel %d (%s) not fed for %dms! queue=%u/%d tick=%u",
            channel_id, name, WDT_TIMEOUT_MS,
            k_msgq_num_used_get(&sensor_q), QUEUE_LEN,
            k_uptime_get_32());
}

/* ================================================================== */
/*  Producer                                                          */
/* ================================================================== */

static void producer_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        struct sensor_data data = {
            .temperature_mc = 24000 + (i * 100),
            .timestamp_ms = k_uptime_get_32(),
            .seq = (uint8_t)i,
        };

        /* Never block the producer: if the queue is full, drop the sample. */
        int ret = k_msgq_put(&sensor_q, &data, K_NO_WAIT);

        if (ret != 0) {
            total_dropped++;
            LOG_WRN("[PRODUCER] queue full, dropped seq=%u  dropped=%d",
                    data.seq, total_dropped);
        } else {
            LOG_INF("[PRODUCER] put seq=%u  queue=%u/%d",
                    data.seq, k_msgq_num_used_get(&sensor_q), QUEUE_LEN);
        }

        k_msleep(PRODUCER_PERIOD_MS);
    }

    LOG_INF("[PRODUCER] done");
}

/* ================================================================== */
/*  Consumer - fed by the task watchdog                               */
/* ================================================================== */

static void consumer_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    /* No hardware watchdog on this board: software (kernel timer) only. */
    int ret = task_wdt_init(NULL);

    if (ret != 0) {
        LOG_ERR("[CONSUMER] task_wdt_init failed: %d", ret);
        return;
    }

    int wdt_id = task_wdt_add(WDT_TIMEOUT_MS, consumer_wdt_cb, "consumer");

    if (wdt_id < 0) {
        LOG_ERR("[CONSUMER] task_wdt_add failed: %d", wdt_id);
        return;
    }

    while (total_consumed + total_dropped < SAMPLE_COUNT) {
        struct sensor_data msg;

        /* Wait less than the watchdog timeout so an idle queue is not a hang. */
        ret = k_msgq_get(&sensor_q, &msg, K_MSEC(WDT_TIMEOUT_MS / 2));
        task_wdt_feed(wdt_id);

        if (ret != 0) {
            continue;
        }

        total_consumed++;
        LOG_INF("[CONSUMER] got seq=%u  latency=%ums",
                msg.seq, k_uptime_get_32() - msg.timestamp_ms);

        if (msg.seq == STUCK_AT_SEQ) {
            LOG_WRN("[CONSUMER] simulating stuck consumer for %dms  tick=%u",
                    STUCK_MS, k_uptime_get_32());
            k_sleep(K_MSEC(STUCK_MS));
            LOG_WRN("[CONSUMER] recovered  tick=%u", k_uptime_get_32());
        } else {
            k_msleep(CONSUMER_WORK_MS);
        }
    }

    task_wdt_delete(wdt_id);

    LOG_INF("[SUMMARY] produced=%d  consumed=%d  dropped=%d",
            SAMPLE_COUNT, total_consumed, total_dropped);
}

/* ================================================================== */
/*  Health check - queue fill level                                   */
/* ================================================================== */

static void health_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    const uint32_t warn_level = QUEUE_LEN * QUEUE_WARN_PCT / 100;
    bool warned = false;

    while (true) {
        k_msleep(HEALTH_PERIOD_MS);

        uint32_t used = k_msgq_num_used_get(&sensor_q);
        uint32_t pct = used * 100 / QUEUE_LEN;

        if (used >= warn_level) {
            /* Warn on every check while high, so the fill-up is visible. */
            LOG_WRN("[HEALTH] queue at %u%% (%u/%d) >= %d%%",
                    pct, used, QUEUE_LEN, QUEUE_WARN_PCT);
            warned = true;
        } else if (warned) {
            LOG_INF("[HEALTH] queue back to normal: %u%% (%u/%d)",
                    pct, used, QUEUE_LEN);
            warned = false;
        }
    }
}

/* ================================================================== */
/*  Threads                                                           */
/* ================================================================== */

/* Health check has the highest priority so it can always observe. */
K_THREAD_DEFINE(health_thread, STACK_SIZE, health_fn,
                NULL, NULL, NULL, 4, 0, 0);

K_THREAD_DEFINE(producer_thread, STACK_SIZE, producer_fn,
                NULL, NULL, NULL, 5, 0, 0);

K_THREAD_DEFINE(consumer_thread, STACK_SIZE, consumer_fn,
                NULL, NULL, NULL, 6, 0, 0);

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void)
{
    LOG_INF("=== L5 Task 1: Reliability under pressure ===");
    LOG_INF("producer every %dms, queue %d deep, warn at %d%%",
            PRODUCER_PERIOD_MS, QUEUE_LEN, QUEUE_WARN_PCT);
    LOG_INF("consumer watchdog %dms, stuck at seq=%d for %dms",
            WDT_TIMEOUT_MS, STUCK_AT_SEQ, STUCK_MS);

    return 0;
}
