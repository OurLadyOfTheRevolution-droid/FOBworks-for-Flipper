#include "../link/flipper_link_proto.h"
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
    FlipperCmd cmd;

    expect(flipper_proto_parse_cmd(
               "{\"cmd\":\"status\",\"code\":\"1234\"}", &cmd),
           "status+code parses");
    expect(cmd.kind == FlipperCmdStatus, "kind status");
    expect(cmd.has_code && strcmp(cmd.code, "1234") == 0, "code field");

    expect(flipper_proto_parse_cmd(
               "{\"cmd\":\"auth\",\"code\":\"0000\",\"new_code\":\"9999\"}", &cmd),
           "auth parses");
    expect(cmd.kind == FlipperCmdAuth, "kind auth");
    expect(cmd.has_code && strcmp(cmd.code, "0000") == 0, "auth code");
    expect(cmd.has_new_code && strcmp(cmd.new_code, "9999") == 0, "new_code");

    expect(flipper_proto_parse_cmd("{\"cmd\":\"hello\"}", &cmd), "hello parses");
    expect(!cmd.has_code, "hello has no code");

    printf("%d failures\n", fails);
    return fails ? 1 : 0;
}
