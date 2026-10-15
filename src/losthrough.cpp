#include "pch.h"
#include <cstring>
#include "hook.h"
#include "debug.h"

// FreedomStory: a locked-on ranged or magic attack is never eaten by terrain.
//
// For single-target arrows/stars (CUserLocal shoot attack, 0x9537D5) and single-target magic
// (CUserLocal magic attack, 0x95571F) the client first picks the monster to hit, then checks the
// straight line from the character to it against the map's footholds (CWvsPhysicalSpace2D, 0xA457D0:
// x, y, &targetX, &targetY, layer). If the line crosses one - e.g. the edge of the platform you stand on
// when the monster is a bit lower - it returns 0 with the crossing point in targetX/Y, and the attack
// drops its target: the projectile flies into the ground and nothing is hit.
//
// Here those two calls always report a clear line and keep the monster's position, so the attack hits
// the monster it locked on to. Which monster gets picked (range, nearest) is unchanged, and so are the
// other users of the check: Teleport (can't go through walls) and Monster Magnet.

namespace {
    typedef int(__thiscall* CheckLine_t)(void* pSpace, int x, int y, int* pnTargetX, int* pnTargetY, int nLayer);
    const CheckLine_t CheckLine = reinterpret_cast<CheckLine_t>(0x00A457D0);

    // same calling convention as CheckLine (thiscall, callee pops 5 arguments)
    int __fastcall check_line_locked_on(void* pSpace, void* /*edx*/, int x, int y, int* pnTargetX, int* pnTargetY, int nLayer) {
        int nTargetX = *pnTargetX;
        int nTargetY = *pnTargetY;
        if (CheckLine(pSpace, x, y, pnTargetX, pnTargetY, nLayer)) {
            return 1;
        }
        *pnTargetX = nTargetX;
        *pnTargetY = nTargetY;
        return 1;
    }
}

void AttachLockedOnThroughTerrain() {
    const uintptr_t CALLS[] = {
        0x0095405F, // shoot attack (arrows, stars, bullets)
        0x00956171, // magic attack
    };
    for (uintptr_t uAddr : CALLS) {
        unsigned char* p = reinterpret_cast<unsigned char*>(uAddr);
        uintptr_t uTarget = uAddr + 5 + *reinterpret_cast<int*>(uAddr + 1);
        if (p[0] != 0xE8 || uTarget != 0x00A457D0) {
            FsLog("los", "locked-on attacks through terrain: unexpected code at %08X, not patched", static_cast<unsigned>(uAddr));
            return;
        }
    }
    for (uintptr_t uAddr : CALLS) {
        PatchCall(uAddr, &check_line_locked_on);
    }
    FsLog("los", "locked-on arrows/stars/magic go through terrain");
}
