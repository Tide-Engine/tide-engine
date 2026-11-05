// What tide-crash masks before a report leaves the machine (crash/scrub.c)
#include "../crash/scrub.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void text_case(const char *in, const char *want, const scrub_names *names)
{
    char buffer[512];
    snprintf(buffer, sizeof buffer, "%s", in);
    scrub_text(buffer, strlen(buffer), names);
    if (strcmp(buffer, want) != 0) {
        fprintf(stderr, "scrub_text(\"%s\")\n  got  \"%s\"\n  want \"%s\"\n", in, buffer, want);
        failures++;
    }
}

// `text` as UTF-16LE at `out`; its byte count
static size_t wide(const char *text, unsigned char *out)
{
    size_t n = 0;
    for (const char *c = text; *c; c++) {
        out[n++] = (unsigned char)*c;
        out[n++] = 0;
    }
    return n;
}

static void put32(unsigned char *at, const uint32_t value)
{
    for (int i = 0; i < 4; i++) at[i] = (unsigned char)(value >> (i * 8));
}

static void dump_case(void)
{
    scrub_names names = {0};
    // A dump: its header, one stream (a memory list of one range), a module's
    // path outside the memory and an address and a name inside it
    unsigned char dump[512] = {0};
    memcpy(dump, "MDMP", 4);
    put32(dump + 8, 1);   // Streams
    put32(dump + 12, 32); // Their directory
    put32(dump + 32, 5);  // MemoryListStream...
    put32(dump + 36, 20); // ...20 bytes...
    put32(dump + 40, 64); // ...at 64
    put32(dump + 64, 1);  // One range
    put32(dump + 76, 64); // Its size (after the 8 bytes of its start)
    put32(dump + 80, 256); // Where its bytes are
    const size_t module = wide("C:\\Users\\Blen\\game.exe", dump + 128);
    wide("/home/blen/x", dump + 201); // At an odd offset: a dump keeps nothing of alignment
    memcpy(dump + 256, "peer 203.0.113.9 ok", 19);
    wide("10.1.2.3", dump + 280); // UTF-16, even
    unsigned char header[96];
    memcpy(header, dump, sizeof header);
    scrub_dump(dump, sizeof dump, &names);
    unsigned char want[64] = {0};
    wide("C:\\Users\\****\\game.exe", want);
    if (memcmp(dump + 128, want, module) != 0) {
        fprintf(stderr, "scrub_dump: a module's path in UTF-16 wasn't masked\n");
        failures++;
    }
    wide("/home/****/x", want);
    if (memcmp(dump + 201, want, 24) != 0) {
        fprintf(stderr, "scrub_dump: a path in UTF-16 at an odd offset wasn't masked\n");
        failures++;
    }
    if (memcmp(dump + 256, "peer ***.*.***.* ok", 19) != 0) {
        fprintf(stderr, "scrub_dump: an address in memory wasn't masked: %.19s\n", (const char *)dump + 256);
        failures++;
    }
    wide("**.*.*.*", want);
    if (memcmp(dump + 280, want, 16) != 0) {
        fprintf(stderr, "scrub_dump: a UTF-16 address in memory wasn't masked\n");
        failures++;
    }
    if (memcmp(dump, header, sizeof header) != 0) {
        fprintf(stderr, "scrub_dump: the dump's header or directory changed\n");
        failures++;
    }
}

int main(void)
{
    scrub_names names = {0};
    scrub_names_add(&names, "Blenminer");
    scrub_names_add(&names, "DESKTOP-7Q2K");
    scrub_names_add(&names, "user"); // Too common to mask but in paths
    scrub_names_add(&names, "ab");   // Too short

    // Paths: the folder after Users/ or home/, spaces and all; shared ones stay
    text_case("saved C:\\Users\\John Smith\\AppData\\LocalLow\\x.json", "saved C:\\Users\\**********\\AppData\\LocalLow\\x.json", &names);
    text_case("C:/Users/jo/Documents", "C:/Users/**/Documents", &names);
    text_case("/home/blen/.local/share/game", "/home/****/.local/share/game", &names);
    text_case("/Users/sam/Library (copy)", "/Users/***/Library (copy)", &names);
    text_case("C:\\Users\\Public\\Documents", "C:\\Users\\Public\\Documents", &names);
    text_case("\"C:\\USERS\\BLENMI~1\\x\"", "\"C:\\USERS\\********\\x\"", &names);
    // Names: whole words, any case
    text_case("signed in as blenminer on DESKTOP-7Q2K.", "signed in as ********* on ************.", &names);
    text_case("Blenminers user ab", "Blenminers user ab", &names);
    // IPv4: not the machine's own, versions, or numbers that run on
    text_case("peer 203.0.113.42:5000 joined", "peer ***.*.***.**:5000 joined", &names);
    text_case("listening on 127.0.0.1:7777 and 0.0.0.0", "listening on 127.0.0.1:7777 and 0.0.0.0", &names);
    text_case("version 1.2.3.4.5, driver 31.0.15.5222, 300.1.1.1", "version 1.2.3.4.5, driver 31.0.15.5222, 300.1.1.1", &names);
    text_case("(192.168.1.20)", "(***.***.*.**)", &names);
    // IPv6: with "::" or 8 groups; not times, names in C++, or ::1
    text_case("relay [2001:db8::1]:443", "relay [****:***::*]:443", &names);
    text_case("fe80:0:0:0:204:61ff:fe9d:f156", "****:*:*:*:***:****:****:****", &names);
    text_case("at 12:34:56, std::vector, a::b::c, ::1", "at 12:34:56, std::vector, a::b::c, ::1", &names);
    // Email
    text_case("mail jo.smith+x@example.co.uk.", "mail **********@*************.", &names);
    text_case("@everyone and x@y and a@b.c", "@everyone and x@y and a@b.c", &names);
    dump_case();
    if (failures) {
        fprintf(stderr, "%d failed\n", failures);
        return 1;
    }
    printf("scrub: all passed\n");
    return 0;
}
