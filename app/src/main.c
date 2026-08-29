#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_A 5
#define PRIO_B 5

void thread_a_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        k_msleep(200);
    }
}

void thread_b_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        k_msleep(300);
    }
}

K_THREAD_DEFINE(thread_a, STACK_SIZE, thread_a_fn,
                NULL, NULL, NULL, PRIO_A, 0, 0);
K_THREAD_DEFINE(thread_b, STACK_SIZE, thread_b_fn,
                NULL, NULL, NULL, PRIO_B, 0, 0);

int main(void)
{
    return 0;
}

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_COOP  (-1)
#define PRIO_HIGH    3
#define PRIO_MID     5
#define PRIO_LOW     7

void coop_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[COOP] starting - will run 5 steps without yielding");

    for (int i = 0; i < 5; i++) {
        k_busy_wait(40000);   
        LOG_INF("[COOP] step %d/5 - still holding CPU  tick=%u",
                i + 1, k_uptime_get_32());
    }

    LOG_INF("[COOP] yielding now - HIGH ,MEDIUM , LOW can run");
    k_yield();

    LOG_INF("[COOP] done");
}

void t_high_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[HIGH] started");
    for (int i = 0; i < 10; i++) {
        // LOG_INF("[T_HIGH] step %d  tick=%u", i, k_uptime_get_32());
        LOG_INF("[T_HIGH] running");
        k_msleep(100);
    }

    LOG_INF("[HIGH] done");
}

void t_med_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[MID] started");

    for (int i = 0; i < 10; i++) {
        // LOG_INF("[T_MID] step %d  tick=%u", i, k_uptime_get_32());
        LOG_INF("[T_MED] running");

        k_msleep(200);
    }

    LOG_INF("[MID] done");
}

void t_low_fn(void *p1, void *p2, void *p3)
{
    LOG_INF("[LOW] started");

    for (int i = 0; i < 10; i++) {
        // LOG_INF("[T_LOW] step %d  tick=%u", i, k_uptime_get_32());
        LOG_INF("[T_LOW] running");
        k_msleep(300);
    }

    LOG_INF("[LOW] done");
}


K_THREAD_DEFINE(t_coop, STACK_SIZE, coop_fn, NULL, NULL, NULL, PRIO_COOP, 0, 0);
K_THREAD_DEFINE(t_high, STACK_SIZE, t_high_fn, NULL, NULL, NULL, PRIO_HIGH, 0, 0);
K_THREAD_DEFINE(t_low,  STACK_SIZE, t_low_fn,  NULL, NULL, NULL, PRIO_LOW,  0, 0);
K_THREAD_DEFINE(t_mid, STACK_SIZE, t_med_fn, NULL, NULL, NULL, PRIO_MID, 0, 0);


int main(void)
{
    LOG_INF("=== l1-task1 ===");
    uint32_t buildtime = k_uptime_get_32();  
    LOG_INF("Build time tick=%u", buildtime);
    LOG_INF("COOP prio=%d (cooperative)  HIGH prio=%d  MID prio=%d  LOW prio=%d",
            PRIO_COOP, PRIO_HIGH, PRIO_MID, PRIO_LOW);
    return 0;
}  