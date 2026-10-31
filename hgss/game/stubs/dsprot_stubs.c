/* dsprot (copy-protection) stand-ins: report a genuine cart.
 *
 * The game sprinkles DSProt_* checks through field loading, the touchscreen
 * menu, the touch-save app and the Pokedex (src/overlay_124.c,
 * src/field/fieldmap.c, src/field/touchscreen_menu.c, src/touch_save_app.c,
 * src/application/pokedex/pokedex_app.c). On real hardware lib/dsprot runs
 * encrypted ROM/MAC-owner tests and the checks below pass on a genuine cart.
 * The port runs only the player's own dump, so each check reports the
 * genuine-cart result:
 *
 * - DSProt_DetectFlashcart / DSProt_DetectEmulator / DSProt_DetectDummy
 *   return 0 (not a flashcart / emulator; the dummy check always fails).
 * - DSProt_DetectNotFlashcart / DSProt_DetectNotEmulator /
 *   DSProt_DetectNotDummy return 1.
 *
 * Each takes a `void *callback` (a `void (*)(void)`, per
 * build/upstream lib/dsprot/include/dsprot_main.h) and invokes it when the
 * check passes, exactly as dsprotMain does in
 * build/upstream lib/dsprot/src/dsprot_main.c:
 * `if (callback != NULL && ret) ((CallbackFunc)callback)();`
 * The game's pass-callbacks are harmless dummies; the fail-callbacks
 * (Heap_AllocAtEnd heap-clobbering) never run.
 *
 * Signatures match build/upstream lib/include/dsprot.h. This file includes
 * no SDK headers; it needs nothing but u32.
 */

#include <stddef.h>
#include <stdint.h>

typedef uint32_t u32;

/* One check: report `pass`, calling the callback only on a pass. */
static u32 GenuineCart(void *callback, u32 pass)
{
    if (callback != NULL && pass) {
        ((void (*)(void))callback)();
    }
    return pass;
}

u32 DSProt_DetectFlashcart(void *callback)
{
    return GenuineCart(callback, 0);
}

u32 DSProt_DetectNotFlashcart(void *callback)
{
    return GenuineCart(callback, 1);
}

u32 DSProt_DetectEmulator(void *callback)
{
    return GenuineCart(callback, 0);
}

u32 DSProt_DetectNotEmulator(void *callback)
{
    return GenuineCart(callback, 1);
}

u32 DSProt_DetectDummy(void *callback)
{
    return GenuineCart(callback, 0);
}

u32 DSProt_DetectNotDummy(void *callback)
{
    return GenuineCart(callback, 1);
}
