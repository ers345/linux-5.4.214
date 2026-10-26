#ifndef __CALCLOCK_H
#define __CALCLOCK_H

#include <linux/ktime.h>

#define BILLION 1000000000LL

static inline void calclock(struct timespec *myclock,
                            unsigned long long *total_time, unsigned long long *total_count) {
    long long timedelay = (myclock[1].tv_sec - myclock[0].tv_sec) * BILLION
                        + (myclock[1].tv_nsec - myclock[0].tv_nsec);

    __sync_fetch_and_add(total_time, timedelay);
    __sync_fetch_and_add(total_count, 1);
}

#endif
