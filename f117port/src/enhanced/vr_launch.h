#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* ENH: native Windows release setup. 1 = ready, 0 = cancelled, -1 = error.
 * The returned directory remains valid for the process lifetime. Checks never
 * open a dialog; explicit/configured paths can be validated from a terminal. */
int vr_launch_prepare(const char **directory, bool explicit_directory,
                      const char *settings_file, bool interactive, bool choose_again);
#ifdef __cplusplus
}
#endif
