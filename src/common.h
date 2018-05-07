#include <stdio.h>

#ifdef _WIN32
#define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#else
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define LOG_(fmt, ...) fprintf(stderr, "LOG: %s:%d:%s(): " fmt "\n", \
    __FILENAME__, __LINE__, __func__, ## __VA_ARGS__)

#include <chrono>
#include <thread>
#define Sleep_ms(x) std::this_thread::sleep_for(std::chrono::milliseconds(x))
