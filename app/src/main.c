/*
 * Lecture 3 - Homework: Polling to Workqueue
 *
 * TASK 2: polling_thread replaced by a k_work handler. sensor_sim submits
 *         the work item instead of setting a flag.
 * TASK 3: handler logs k_uptime_get_32() - it runs once per sensor event,
 *         with no wake-ups in between.
 * BONUS:  set DEBOUNCE_BONUS to 1. sensor_sim fires bursts of 5 events in
 *         ~20ms and calls k_work_reschedule() on each, so the handler runs
 *         once per burst (30ms after the last event).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define DEBOUNCE_BONUS 0      /* 0 = Task 2/3, 1 = debounce bonus */

#define STACK_SIZE    1024
#define SENSOR_MS     100    /* sensor fires every 100ms */
#define EVENT_COUNT   10     /* total sensor events (or bursts) to produce */

#define BURST_SIZE    5      /* BONUS: events per burst */
#define BURST_GAP_MS  4      /* BONUS: 5 events x 4ms = burst within 20ms */
#define DEBOUNCE_MS   30     /* BONUS: quiet time before handler runs */

/* Statistics */
static int total_events;
static int total_processed;

/* ------------------------------------------------------------------ */
/*  sensor_handler - runs on the system workqueue, once per submit    */
/* ------------------------------------------------------------------ */

static void sensor_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    total_processed++;
    LOG_INF("[HANDLER] processed event %d  tick=%u",
            total_processed, k_uptime_get_32());

    if (total_processed == EVENT_COUNT) {
        LOG_INF("[SUMMARY] sensor events=%d  handler runs=%d  wasted wakeups=0",
                total_events, total_processed);
    }
}

#if DEBOUNCE_BONUS
K_WORK_DELAYABLE_DEFINE(debounce_work, sensor_handler);
#else
K_WORK_DEFINE(sensor_work, sensor_handler);
#endif

/* ------------------------------------------------------------------ */
/*  sensor_sim - produces EVENT_COUNT events (or bursts), 100ms apart */
/* ------------------------------------------------------------------ */

static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    for (int i = 0; i < EVENT_COUNT; i++) {
        k_msleep(SENSOR_MS);

#if DEBOUNCE_BONUS
        for (int j = 0; j < BURST_SIZE; j++) {
            if (j > 0) {
                k_msleep(BURST_GAP_MS);
            }

            total_events++;

            /* Restarts the 30ms countdown on every event in the burst */
            int ret = k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_MS));

            if (ret < 0) {
                LOG_ERR("reschedule failed: %d", ret);
            }
            LOG_INF("[SENSOR] burst %d event %d  reschedule tick=%u",
                    i, j, k_uptime_get_32());
        }
#else
        total_events++;
        LOG_INF("[SENSOR] event %d  tick=%u", i, k_uptime_get_32());

        int ret = k_work_submit(&sensor_work);

        if (ret < 0) {
            LOG_ERR("submit failed: %d", ret);
        }
#endif
    }

    LOG_INF("[SENSOR] all events produced");
}

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
#if DEBOUNCE_BONUS
    LOG_INF("=== L3 Homework: Debounce with k_work_reschedule ===");
    LOG_INF("%d bursts of %d events, %dms debounce",
            EVENT_COUNT, BURST_SIZE, DEBOUNCE_MS);
#else
    LOG_INF("=== L3 Homework: Workqueue (event-driven) ===");
    LOG_INF("Sensor fires every %dms, handler runs only on submit", SENSOR_MS);
#endif

    /* Wait long enough for all events to complete */
    k_msleep((EVENT_COUNT + 2) * SENSOR_MS + 500);

    return 0;
}
