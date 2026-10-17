/*
 * Lecture 4 - Task 1: Event-driven system with zbus
 *
 * - One zbus channel (sensor_chan) carries sensor samples
 * - sensor thread publishes a simulated sample every 100ms
 * - display listener: fast, runs synchronously in the publisher's context
 * - logger subscriber: slow, runs in its own thread and gets a copy of
 *   every message, so it can fall behind without losing samples
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(l4_task1, LOG_LEVEL_DBG);

#define STACK_SIZE        2048
#define SENSOR_COUNT        20
#define SENSOR_PERIOD_MS   100
#define LOGGER_WORK_MS     150    /* logger is slower than the sensor */

/* ================================================================== */
/*  Channel message                                                   */
/* ================================================================== */

struct sensor_data {
    int32_t temperature_mc;
    uint32_t timestamp_ms;
    uint8_t seq;
};

/* Forward declaration required before the listener definition. */
static void display_listener_cb(const struct zbus_channel *chan);

/* ================================================================== */
/*  Observers                                                         */
/* ================================================================== */

ZBUS_LISTENER_DEFINE(display_lis, display_listener_cb);

/*
 * Logger is a message subscriber: each publish queues a copy of the
 * message, so the slow logger sees every sample, not just the latest.
 */
ZBUS_MSG_SUBSCRIBER_DEFINE(logger_sub);

/* ================================================================== */
/*  Channel                                                           */
/* ================================================================== */

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_data,
                 NULL, NULL,
                 ZBUS_OBSERVERS(display_lis, logger_sub),
                 ZBUS_MSG_INIT(.temperature_mc = 0,
                               .timestamp_ms = 0,
                               .seq = 0));

/* ================================================================== */
/*  Listener - fast display update                                    */
/* ================================================================== */

static void display_listener_cb(const struct zbus_channel *chan)
{
    const struct sensor_data *msg = zbus_chan_const_msg(chan);

    /* Runs in publisher context: keep it short, never block. */
    LOG_INF("[DISPLAY] thread=%s seq=%u temp=%d mC",
            k_thread_name_get(k_current_get()),
            msg->seq,
            msg->temperature_mc);
}

/* ================================================================== */
/*  Publisher - simulated sensor                                      */
/* ================================================================== */

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "sensor");

    for (int i = 0; i < SENSOR_COUNT; i++) {
        struct sensor_data data = {
            .temperature_mc = 24000 + (i * 350),
            .timestamp_ms = k_uptime_get_32(),
            .seq = (uint8_t)i,
        };

        LOG_INF("[SENSOR] publish seq=%u temp=%d mC tick=%u",
                data.seq, data.temperature_mc, data.timestamp_ms);

        int ret = zbus_chan_pub(&sensor_chan, &data, K_MSEC(100));
        if (ret != 0) {
            LOG_WRN("[SENSOR] publish failed ret=%d", ret);
        }

        k_msleep(SENSOR_PERIOD_MS);
    }

    LOG_INF("[SENSOR] done");
}

/* ================================================================== */
/*  Subscriber - slow logger                                          */
/* ================================================================== */

static void logger_thread_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    k_thread_name_set(k_current_get(), "logger");

    const struct zbus_channel *chan;
    int received = 0;

    while (received < SENSOR_COUNT) {
        struct sensor_data msg;

        int ret = zbus_sub_wait_msg(&logger_sub, &chan, &msg, K_MSEC(1500));
        if (ret != 0) {
            LOG_WRN("[LOGGER] timeout ret=%d", ret);
            break;
        }

        received++;

        LOG_INF("[LOGGER] thread=%s seq=%u temp=%d mC latency=%ums",
                k_thread_name_get(k_current_get()),
                msg.seq,
                msg.temperature_mc,
                k_uptime_get_32() - msg.timestamp_ms);

        /* Simulate slow work, e.g. writing to flash. */
        k_msleep(LOGGER_WORK_MS);
    }

    LOG_INF("[LOGGER] done received=%d/%d", received, SENSOR_COUNT);
}

/* ================================================================== */
/*  Threads                                                           */
/* ================================================================== */

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_thread_fn,
                NULL, NULL, NULL, 5, 0, 0);

/* Lower priority than the sensor: logging must never delay sampling. */
K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_thread_fn,
                NULL, NULL, NULL, 6, 0, 0);

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void)
{
    LOG_INF("=== L4 Task 1: Zbus sensor pipeline ===");
    LOG_INF("sensor publishes every %dms", SENSOR_PERIOD_MS);
    LOG_INF("display listener: synchronous, publisher context");
    LOG_INF("logger subscriber: async, %dms per sample", LOGGER_WORK_MS);

    return 0;
}
