/* #908 r11: the fixture for the seqlock ORDERING guard.
 *
 * NEVER LINKED AND NEVER RUN. It is compiled to assembly so the guard can ask
 * the optimiser what it actually did with the two reads, which is the only way
 * to test a property that exists only after optimisation.
 *
 * Two readers, differing ONLY in the barrier. That pairing is deliberate: the
 * unbarriered one is the guard's KNOWN POSITIVE. A checker that merely
 * confirmed the good ordering would pass just as happily against a compiler
 * that never reorders anything, on a machine where the defect cannot occur,
 * and would then report a clean bill for a firmware that had lost its
 * barriers. Requiring the defect to REPRODUCE in `reader_unbarriered` is what
 * proves the instrument can see it at all.
 *
 * It mirrors daqifi_settings_GetCalSource's snapshot, not the whole function:
 * the generation read, the opaque scan, the NON-volatile bank read, and the
 * closing generation check. `gActiveCalBank` must stay non-volatile here
 * because that is the property under test, and `writer` must stay in this
 * translation unit -- without a writer the compiler proves the bank is always
 * its initialiser and folds the load away entirely, which looks like an even
 * more dramatic failure and is only an artifact of the fixture. (Measured
 * while writing this: the first version had no writer and the load vanished.)
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum { BANK_FACT = 0, BANK_USER } Bank;

static volatile uint32_t gCalGeneration = 0u;
static Bank gActiveCalBank = BANK_FACT;     /* non-volatile, as in the firmware */

extern bool scan_identity(void);            /* opaque: stands for CalIsIdentity */

#define CAL_BARRIER() __asm__ volatile ("" ::: "memory")

/* Keeps gActiveCalBank from being constant-folded. Mirrors the real
 * publication order: odd mark, update, even mark. */
void writer(Bank t)
{
    gCalGeneration++;
    CAL_BARRIER();
    gActiveCalBank = t;
    CAL_BARRIER();
    gCalGeneration++;
}

/* The shipped-before-r11 shape. The bank load is EXPECTED to escape the
 * window; the guard fails if it does not, because then it is not measuring. */
int reader_unbarriered(void)
{
    const uint32_t genBefore = gCalGeneration;
    if ((genBefore & 1u) != 0u) {
        return -1;
    }
    const bool ident = scan_identity();
    const Bank bank = gActiveCalBank;
    if (gCalGeneration != genBefore) {
        return -1;
    }
    return ident ? 0 : ((bank == BANK_USER) ? 2 : 1);
}

/* The shipped shape. The bank load must stay INSIDE the validated window. */
int reader_barriered(void)
{
    const uint32_t genBefore = gCalGeneration;
    CAL_BARRIER();
    if ((genBefore & 1u) != 0u) {
        return -1;
    }
    const bool ident = scan_identity();
    const Bank bank = gActiveCalBank;
    CAL_BARRIER();
    if (gCalGeneration != genBefore) {
        return -1;
    }
    return ident ? 0 : ((bank == BANK_USER) ? 2 : 1);
}
