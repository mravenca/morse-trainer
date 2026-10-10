#include "keysource.h"

#include <time.h>

int64_t monotonicUs()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1'000'000 + ts.tv_nsec / 1000;
}
