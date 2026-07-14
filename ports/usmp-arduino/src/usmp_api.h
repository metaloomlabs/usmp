#pragma once

/*
 * usmp_api.h — repo-local shim; NOT the header that ships.
 *
 * The Arduino wrappers include "usmp_api.h" because the packaged library renames
 * the core public header (core/include/usmp.h) to usmp_api.h at build time, to
 * dodge the USMP.h vs usmp.h collision on case-insensitive filesystems.
 *
 * In the repo this file forwards to the real core header, so there is ONE source
 * of truth and nothing to hand-sync. At package time
 * scripts/build-arduino-zip.{sh,ps1} OVERWRITES this shim with the actual
 * core/include/usmp.h, so the forward below never ships — it exists only for
 * editor indexing and standalone compilation.
 *
 * The path is explicit and relative (not a bare "usmp.h"): on a case-insensitive
 * filesystem a bare include would match the sibling USMP.h (the Arduino class
 * header) instead of core's usmp.h — the very collision the packaging step works
 * around.
 */
#include "../../../core/include/usmp.h"
