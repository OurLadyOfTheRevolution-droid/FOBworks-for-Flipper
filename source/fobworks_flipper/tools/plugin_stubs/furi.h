#pragma once
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#define furi_check(x) assert(x)
#define APP_ASSETS_PATH(x) x
#define FuriWaitForever UINT32_MAX
typedef pthread_mutex_t FuriMutex;
typedef enum { FuriMutexTypeRecursive } FuriMutexType;
static inline FuriMutex* furi_mutex_alloc(FuriMutexType type) {
    (void)type;
    FuriMutex* m = malloc(sizeof(*m));
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return m;
}
static inline void furi_mutex_acquire(FuriMutex* m, uint32_t timeout) {
    (void)timeout;
    pthread_mutex_lock(m);
}
static inline void furi_mutex_release(FuriMutex* m) { pthread_mutex_unlock(m); }
static inline void furi_mutex_free(FuriMutex* m) { pthread_mutex_destroy(m); free(m); }
#define RECORD_STORAGE "storage"
static inline void* furi_record_open(const char* name) { (void)name; return (void*)1; }
static inline void furi_record_close(const char* name) { (void)name; }
