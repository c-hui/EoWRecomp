// Exercise the production FFmpeg extractor against an in-image file and a physical-file fallback.
// Usage: test_movie_io <local WebM file>. Supply your own file; no media is bundled.
// Build with -ffunction-sections -fdata-sections -Wl,--gc-sections and the runtime's FFmpeg libs.
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../runtime/hle/movie.c"

const char *g_romfs_dir = "/does-not-exist";
Settings g_settings = {.movie_audio = true};
static int fixture;
static uint64_t fixture_size;
static bool image_enabled = true;
static int reads;

bool romfs_find(const char *rel, uint64_t *off, uint64_t *size) {
    if (!image_enabled || strcmp(rel, "test.webm")) return false;
    *off = 4096; *size = fixture_size;
    return true;
}

ssize_t romfs_read_image(void *dst, uint64_t size, uint64_t off) {
    assert(off >= 4096 && off - 4096 <= fixture_size);
    assert(size <= fixture_size - (off - 4096));
    reads++;
    return pread(fixture, dst, size, (off_t)(off - 4096));
}

static int packets(Extractor *e) {
    AVPacket *pkt = av_packet_alloc();
    int n = 0;
    while (av_read_frame(e->fmt, pkt) >= 0) { n++; av_packet_unref(pkt); }
    av_packet_free(&pkt);
    return n;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    fixture = open(argv[1], O_RDONLY);
    assert(fixture >= 0);
    struct stat st;
    assert(fstat(fixture, &st) == 0 && st.st_size > 0);
    fixture_size = (uint64_t)st.st_size;
    Extractor e = {0};
    uint64_t self[2] = {0, (uint64_t)(uintptr_t)&e};
    Cpu c = {0};
    c.x[0] = (uint64_t)(uintptr_t)self;
    c.x[2] = (uint64_t)(uintptr_t)"rom:/test.webm";
    ex_set_data_source(&c);
    assert(c.x[0] == 0 && e.io && e.ntracks > 0 && reads > 0);
    int count = packets(&e);
    assert(count > 0);
    e.selected = 7; e.eof = true;
    c.x[0] = (uint64_t)(uintptr_t)self;
    ex_set_data_source(&c);
    assert(c.x[0] == 0 && !e.eof && e.selected == 0 && packets(&e) == count);
    assert(rom_io_seek(&e, 0, AVSEEK_SIZE) == (int64_t)fixture_size);
    assert(rom_io_seek(&e, -1, SEEK_SET) == AVERROR(EINVAL));
    assert(rom_io_seek(&e, 0, SEEK_END | AVSEEK_FORCE) == (int64_t)fixture_size);
    uint8_t byte;
    assert(rom_io_read(&e, &byte, 1) == AVERROR_EOF);
    assert(rom_io_seek(&e, INT64_MAX, SEEK_CUR) == AVERROR(EINVAL));
    assert(rom_io_seek(&e, 0, 123) == AVERROR(EINVAL));
    assert(rom_io_seek(&e, -1, SEEK_END) == (int64_t)fixture_size - 1);
    assert(rom_io_read(&e, &byte, 1) == 1);
    ex_close(&e);
    assert(!e.fmt && !e.io && !e.pkt);
    ex_close(&e);
    image_enabled = false;
    g_romfs_dir = "/";
    c.x[0] = (uint64_t)(uintptr_t)self;
    c.x[2] = (uint64_t)(uintptr_t)argv[1];
    ex_set_data_source(&c);
    assert(c.x[0] == 0 && !e.io && packets(&e) == count);
    c.x[0] = (uint64_t)(uintptr_t)self;
    c.x[2] = (uint64_t)(uintptr_t)"missing.webm";
    ex_set_data_source(&c);
    assert(c.x[0] != 0 && !e.fmt && !e.io && !e.pkt);
    c.x[0] = (uint64_t)(uintptr_t)self;
    c.x[2] = 0;
    ex_set_data_source(&c);
    assert(c.x[0] != 0);
    ex_close(&e);
    close(fixture);
    printf("movie IO: %d packets match image and file paths; reopen, seek, EOF, failure cleanup OK\n", count);
    return 0;
}
