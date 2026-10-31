/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA's golden renders (tests/umbra_golden.txt): each recipe (web/emu/umbra_recipes.c, the emulator's demo list)
 * from power-on, PLAY, 8 s of the stereo mix (FROZEN HALL: FREEZE at 4 s, stopped at 5 s); its hash (FNV-1a over the samples), peak and mean against the file.
 * Each recipe in a process of its own (fork): no state from the one before. Also checks that each sounds (peak),
 * does not clip, has no DC.
 *   build/host/umbra_golden [UPDATE=1: write the file] [UMBRA_WAV=dir: the renders as WAVs, to listen]
 * A changed hash fails: update only when the sound was meant to change (CLAUDE.md: golden hashes are tripwires). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include "../web/emu/umbra_recipes.c"
#include <sys/wait.h>
#include <unistd.h>

#define GOLD "tests/umbra_golden.txt"
#define SECS 8u

typedef struct {
    uint32_t hash;
    int32_t peak, mean, clip;
} gold_t;

static gold_t render(uint32_t n, const char *wavdir)
{
    gold_t g = {2166136261u, 0, 0, 0};
    uint32_t f, i, frames = SECS * FS;
    int64_t sum = 0;
    FILE *w = 0;
    ui_power_on();
    song.master_q12 = 2048;                     /* (main.c umbra_init; ui_power_on zeroes the song) */
    umbra_recipe(n);
    if (wavdir) {
        char nm[48], path[512];
        for (i = 0; RECIPE_NAME[n][i] && i < 40u; i++)
            nm[i] = RECIPE_NAME[n][i] == ' ' ? '_' : RECIPE_NAME[n][i];
        nm[i] = 0;
        snprintf(path, sizeof path, "%s/%s.wav", wavdir, nm);
        if ((w = fopen(path, "wb")) != 0)
            wav_hdr(w, frames);
    }
    transport_req = 1;
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        if (n == 3u && f == 4u * FS / CTL * CTL)
            usong[US_FRZ] = 1;                  /* FROZEN HALL: FREEZE at 4 s, then stopped at 5 s: the hall hangs */
        if (n == 3u && f == 5u * FS / CTL * CTL)
            transport_req = 2;
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = o[i] < 0 ? -o[i] : o[i];
            uint32_t k;
            for (k = 0; k < 4u; k++) {
                g.hash ^= ((uint32_t)o[i] >> (8u * k)) & 0xFFu;
                g.hash *= 16777619u;
            }
            g.peak = a > g.peak ? a : g.peak;
            g.clip += a >= 32767;
            sum += o[i];
        }
        if (w)
            for (i = 0; i < CTL; i++)
                wav_put(w, o[2 * i], o[2 * i + 1]);
    }
    if (w)
        fclose(w);
    g.mean = (int32_t)(sum / (int64_t)(2u * frames));
    return g;
}

int main(void)
{
    const char *upd = getenv("UPDATE"), *wavdir = getenv("UMBRA_WAV");
    gold_t got[UMBRA_NRECIPES];
    uint32_t n, bad = 0;
    FILE *f;
    for (n = 0; n < UMBRA_NRECIPES; n++) {
        int fd[2];
        pid_t pid;
        if (pipe(fd) || (pid = fork()) < 0)
            return 2;
        if (!pid) {
            gold_t g = render(n, wavdir);
            if (write(fd[1], &g, sizeof g) != (ssize_t)sizeof g)
                _exit(2);
            _exit(0);
        }
        close(fd[1]);
        if (read(fd[0], &got[n], sizeof got[n]) != (ssize_t)sizeof got[n])
            return 2;
        close(fd[0]);
        waitpid(pid, 0, 0);
    }
    if (upd && *upd == '1') {
        f = fopen(GOLD, "w");
        if (!f)
            return 2;
        fprintf(f, "# UMBRA golden renders: recipe hash peak mean (tests/umbra_golden.c; UPDATE=1 rewrites)\n");
        for (n = 0; n < UMBRA_NRECIPES; n++)
            fprintf(f, "%u %08x %d %d %s\n", n, got[n].hash, got[n].peak, got[n].mean, RECIPE_NAME[n]);
        fclose(f);
        printf("golden: %s written\n", GOLD);
    }
    f = fopen(GOLD, "r");
    for (n = 0; n < UMBRA_NRECIPES; n++) {
        char line[160];
        unsigned k = 99, h = 0;
        int ok_h = 0, ok_s;
        if (f) {
            rewind(f);
            while (fgets(line, sizeof line, f))
                if (line[0] != '#' && sscanf(line, "%u %x", &k, &h) == 2 && k == n) {
                    ok_h = h == got[n].hash;
                    break;
                }
        }
        ok_s = got[n].peak > 300 && !got[n].clip && got[n].mean > -200 && got[n].mean < 200;
        printf("golden: %-14s %08x peak %6d mean %5d clip %d: %s%s\n", RECIPE_NAME[n], got[n].hash, got[n].peak,
               got[n].mean, got[n].clip, ok_h ? "hash ok" : "HASH CHANGED", ok_s ? "" : ", SILENT / CLIPS / DC");
        bad += !ok_h + !ok_s;
    }
    if (f)
        fclose(f);
    printf(bad ? "golden: %u FAILED\n" : "golden: all ok\n", bad);
    return bad ? 1 : 0;
}
