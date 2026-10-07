#include "../protocol/flipper_plugin.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

atomic_int test_frees;
static atomic_bool started;
static atomic_bool finished;

static void* unload(void* ctx) {
    (void)ctx;
    atomic_store(&started, true);
    flipper_plugin_unload_all();
    atomic_store(&finished, true);
    return NULL;
}

int main(void) {
    flipper_plugin_init();
    flipper_plugin_lock();
    /* Recursive API lookup must not deadlock while holding the lifetime guard. */
    assert(flipper_force_api() != NULL);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, unload, NULL) == 0);
    struct timespec pause = {0, 1000000};
    while(!atomic_load(&started)) nanosleep(&pause, NULL);
    struct timespec wait = {0, 30000000};
    nanosleep(&wait, NULL);
    assert(!atomic_load(&finished) && atomic_load(&test_frees) == 0);
    flipper_plugin_unlock();
    assert(pthread_join(thread, NULL) == 0);
    assert(atomic_load(&finished) && atomic_load(&test_frees) == 1);
    flipper_plugin_deinit();
    /* Next launch starts clean. */
    flipper_plugin_init();
    flipper_plugin_deinit();
    puts("Production plugin lifetime blocks concurrent unload: PASS");
}
