/* SPDX-License-Identifier: GPL-3.0-only */
/* UMBRA's audio memory against its budget: UMBRA_MEM_TOTAL (firmware/src/umbra_mem.h) <= tests/ram_budget.txt,
 * and the size macros' arithmetic.
 *   ram_test tests/ram_budget.txt */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/umbra_mem.h"

static int fails;
static void check(const char *what, int ok)
{
    printf("ram: %-68s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "tests/ram_budget.txt", "r");
    char line[256];
    unsigned long budget = 0;
    if (!f) {
        printf("ram: no budget file\nFAIL\n");
        return 1;
    }
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "budget %lu", &budget) == 1)
            break;
    fclose(f);
    check("the budget file names a budget", budget > 0);
    check("1 s at 44.1 kHz: 88200 B HI, 44100 B LO", UMBRA_MEM_HI(UMBRA_MEM_FS, 1000u) == 88200u &&
                                                      UMBRA_MEM_LO(UMBRA_MEM_FS, 1000u) == 44100u);
    check("half rate halves it", UMBRA_MEM_LO(UMBRA_MEM_FS_LO, 1000u) == 22050u);
    printf("ram: UMBRA buffers %lu B of %lu B\n", (unsigned long)UMBRA_MEM_TOTAL, budget);
    check("UMBRA_MEM_TOTAL within tests/ram_budget.txt", (unsigned long)UMBRA_MEM_TOTAL <= budget);
    return fails ? 1 : 0;
}
