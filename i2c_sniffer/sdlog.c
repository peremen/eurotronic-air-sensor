#include "sdlog.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "f_util.h"
#include "ff.h"

static FATFS fs;
static FIL fil;
static volatile sdlog_state_t state = SDLOG_CLOSED;
static bool mounted, file_open;
static char path[48];
static absolute_time_t retry_at;

static void release(void) {
    if (file_open) { f_close(&fil); file_open = false; }
    if (mounted)   { f_unmount(""); mounted = false; }
}

static void fail(const char *what, FRESULT fr) {
    printf("SD: %s failed: %s (%d) - will retry in %d s\n", what, FRESULT_str(fr), (int)fr, SD_RETRY_S);
    release();
    state = SDLOG_ERROR;
    retry_at = make_timeout_time_ms(SD_RETRY_S * 1000);
}

bool sdlog_open(void) {
    release();
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) { fail("mount", fr); return false; }
    mounted = true;

    fr = f_mkdir(SD_LOG_DIR);
    if (fr != FR_OK && fr != FR_EXIST) { fail("mkdir", fr); return false; }

    int idx;
    for (idx = 1; idx <= 9999; idx++) {                       // first unused file number
        snprintf(path, sizeof path, SD_LOG_DIR "/" SD_LOG_PREFIX "%04d.csv", idx);
        if (f_stat(path, NULL) == FR_NO_FILE) break;
    }
    if (idx > 9999) { fail("find free file name", FR_DENIED); return false; }

    fr = f_open(&fil, path, FA_WRITE | FA_CREATE_NEW);
    if (fr != FR_OK) { fail("open", fr); return false; }
    file_open = true;

    UINT bw;
    fr = f_write(&fil, record_csv_header(), (UINT)strlen(record_csv_header()), &bw);
    if (fr == FR_OK) fr = f_sync(&fil);
    if (fr != FR_OK) { fail("write header", fr); return false; }

    state = SDLOG_OK;
    printf("SD: logging to %s\n", path);
    return true;
}

void sdlog_close(void) {
    release();
    state = SDLOG_CLOSED;
    printf("SD: closed, safe to remove the card\n");
}

bool sdlog_write(const sdlog_record_t *r) {
    if (state != SDLOG_OK) return false;
    char line[200];
    int n = record_format(line, sizeof line, r);
    UINT bw;
    FRESULT fr = f_write(&fil, line, (UINT)n, &bw);
    if (fr == FR_OK && bw != (UINT)n) fr = FR_DISK_ERR;
    if (fr == FR_OK) fr = f_sync(&fil);                       // data and FAT are on the card after every line
    if (fr != FR_OK) { fail("write", fr); return false; }
    return true;
}

void sdlog_task(void) {
    if (state == SDLOG_ERROR && absolute_time_diff_us(get_absolute_time(), retry_at) <= 0) {
        printf("SD: trying to remount...\n");
        sdlog_open();
    }
}

sdlog_state_t sdlog_state(void) { return state; }
const char *sdlog_filename(void) { return state == SDLOG_OK ? path : ""; }
