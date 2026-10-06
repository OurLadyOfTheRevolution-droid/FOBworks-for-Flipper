#include "../protocol/flipper_keyvault.h"
#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

static void expect(int ok, const char* label) {
    checks++;
    if(!ok) {
        printf("  FAIL: %s\n", label);
        failures++;
    }
}

static void test_upsert_across_hole(void) {
    FlipperVaultKey keys[FLIPPER_KEYVAULT_MAX];
    memset(keys, 0, sizeof(keys));

    expect(flipper_keyvault_upsert(keys, "alpha", 1), "insert alpha");
    expect(flipper_keyvault_upsert(keys, "beta", 2), "insert beta");
    expect(flipper_keyvault_delete(keys, "alpha"), "delete alpha leaves a hole");
    expect(flipper_keyvault_count(keys) == 1, "one key after delete");
    expect(flipper_keyvault_upsert(keys, "beta", 99), "replace beta through the hole");
    expect(flipper_keyvault_count(keys) == 1, "replace does not grow");
    const FlipperVaultKey* b = flipper_keyvault_find(keys, "beta");
    expect(b && b->key == 99, "beta value replaced");
    expect(flipper_keyvault_find(keys, "alpha") == NULL, "alpha stays gone");
}

static void test_from_text_replace(void) {
    FlipperVaultKey keys[FLIPPER_KEYVAULT_MAX];
    memset(keys, 0, sizeof(keys));
    expect(flipper_keyvault_upsert(keys, "stale", 5), "seed stale");

    int count = 0;
    expect(flipper_keyvault_from_text(
               "beta 0000000000000063\nalpha 0000000000000001\n",
               keys, FLIPPER_KEYVAULT_MAX, &count),
           "parse two keys");
    expect(count == 2, "two unique keys");
    expect(flipper_keyvault_find(keys, "stale") == NULL, "reload replaces vault");
    const FlipperVaultKey* a = flipper_keyvault_find(keys, "alpha");
    const FlipperVaultKey* b = flipper_keyvault_find(keys, "beta");
    expect(a && a->key == 1, "alpha loaded");
    expect(b && b->key == 0x63, "beta loaded");
}

int main(void) {
    test_upsert_across_hole();
    test_from_text_replace();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
