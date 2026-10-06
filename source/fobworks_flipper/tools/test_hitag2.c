#include "../protocol/flipper_hitag2.h"
#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char* msg) {
    if(!cond) {
        printf("FAIL: %s\n", msg);
        fails++;
    }
}

int main(void) {
    /* Known-key table is non-empty and indexable. */
    int n = hitag2_known_key_count();
    expect(n > 0, "known key count > 0");
    expect(hitag2_get_known_key(0) != NULL, "key 0 resolves");
    expect(hitag2_get_known_key(n) == NULL, "OOB key → NULL");

    /* Authenticate is deterministic for a fixed key/uid/challenge. */
    const Hitag2KnownKey* k = hitag2_get_known_key(0);
    expect(k != NULL, "have a key");
    if(k) {
        uint32_t a = hitag2_authenticate(k->key, 0x12345678u, 0xA5A5A5A5u);
        uint32_t b = hitag2_authenticate(k->key, 0x12345678u, 0xA5A5A5A5u);
        expect(a == b, "authenticate is stable");
        uint32_t c = hitag2_authenticate(k->key, 0x12345678u, 0x5A5A5A5Au);
        expect(a != c, "different challenge → different response");
    }

    printf("%d failures\n", fails);
    return fails ? 1 : 0;
}
