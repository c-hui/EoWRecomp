// Boundary checks for the production BRX helper and reserved VOTE encodings. No game code or GPU needed.
#include <assert.h>
#include <stdio.h>
#include "../runtime/nvn/maxwell/translate.c"

int main(void) {
    uint64_t blob[34] = {0};
    // A synthesized clamp/table load/indirect branch, two forward cases, and eight bytes of compiler constants.
    blob[5] = 0x3820000000000000ull | (7ull << 39) | (1ull << 20) | (7ull << 16);
    blob[6] = 0xef90000000000000ull | (1ull << 36) | (7ull << 16) | 2;
    blob[7] = 0xe250000000000000ull | (7ull << 16) | (2ull << 8);
    uint32_t table[2] = {0, 16};
    memcpy((uint8_t *)blob + 256, table, sizeof table);
    uint8_t labels[32] = {0};
    assert(!brx_targets((uint8_t *)blob, (uint8_t *)blob, 256, 0, 7, 16, labels));
    assert(!brx_targets((uint8_t *)blob, (uint8_t *)blob, 256, 4, 7, 16, labels));
    assert(brx_targets((uint8_t *)blob, (uint8_t *)blob, 256, 8, 7, 16, labels));
    assert(labels[9] && labels[10]);
    blob[6] |= 0xfffcull << 20;
    assert(!brx_targets((uint8_t *)blob, (uint8_t *)blob, 256, 8, 7, 16, labels));
    uint64_t vote[8] = {0, 0x50db03800007000eull, 0xe30000000007000full, 0xe2400fffff87000full};
    for (int glsl = 0; glsl < 2; glsl++) {
        ShaderKey key = {.glsl = glsl};
        ShaderInfo *si = maxwell_translate((uint8_t *)vote, sizeof vote, STAGE_COMPUTE, &key);
        assert(!si->msl && strstr(si->error, "vote mode 3"));
        maxwell_free(si);
    }
    puts("PASS BRX constant bounds and reserved VOTE rejection (GLSL/MSL)");
    return 0;
}
