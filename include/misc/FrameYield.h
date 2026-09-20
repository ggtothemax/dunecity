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

#ifndef FRAMEYIELD_H
#define FRAMEYIELD_H

/**
    Single seam for letting the browser event loop run while the game sits in
    a nested blocking loop (menu, game, cutscene). See platform/web/README.md
    for the recorded main-loop decision: DuneCity's menus nest several blocking
    showMenu() loops deep, so instead of rewriting them all into an
    emscripten_set_main_loop state machine, the Emscripten build compiles with
    -sASYNCIFY and each frame boundary calls through here. yieldFrameToBrowser()
    unwinds the whole call stack to the JavaScript event loop (where WebSocket
    signaling and WebRTC DataChannel callbacks live) via a non-zero Asyncify
    sleep and resumes on the next tick. Native builds keep their blocking loops
    and pay nothing.
*/
#ifdef __EMSCRIPTEN__
void yieldFrameToBrowser(unsigned int durationMs = 1);
#else
inline void yieldFrameToBrowser(unsigned int = 1) { }
#endif

#endif // FRAMEYIELD_H
