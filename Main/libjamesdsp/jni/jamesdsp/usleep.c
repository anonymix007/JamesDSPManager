#include <qurt_thread.h>

int usleep(unsigned usec) {
    qurt_sleep(usec);
    return 0;
}
