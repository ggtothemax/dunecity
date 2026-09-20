/*
 *  This file is part of Dune Legacy.
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <misc/FrameYield.h>

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

void yieldFrameToBrowser(unsigned int durationMs) {
    // A non-zero Asyncify sleep unwinds the call stack to the browser event
    // loop and resumes afterwards. Zero-duration sleeps do not reliably hand
    // control back, so pending signaling and DataChannel callbacks starve.
    // Callers may pass a longer duration to pace a loop (e.g. a cutscene
    // frame budget); clamp so a caller passing 0 still yields.
    emscripten_sleep(durationMs < 1 ? 1 : durationMs);
}

#endif // __EMSCRIPTEN__
