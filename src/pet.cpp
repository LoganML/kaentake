#include "pch.h"
#include "hook.h"
#include "debug.h"
#include "constants.h"

// FreedomStory: smarter pet looting (pet AI in the v83 client).
//
// How the client pet loots (from the client code):
//   * CDropPool::TryPickUpDropByPet (0x50483A): a pet grabs a drop that is within +-25 px of it
//     (and 50 px above / 10 px below).
//   * CDropPool pet sweep (0x508268), pets with Wing Boots (sweepForDrop): finds the nearest drop on the
//     pet's level within +-125 px of the owner (+150 more with Binocular / longRange) and stores a
//     walk direction in the pet's movement controller (+0x25C).
//   * CVecCtrlPet update (0x9C4122): the pet only walks to that drop when the owner hasn't moved for
//     100 ms - otherwise it keeps following. It teleports back to the owner when the owner is more
//     than 300 px (left/right) or 200 px (up/down) away.
//
// Changes (config.ini [pet], defaults shown):
//   sweep_range=300        search range around you with Wing Boots (was 125)
//   binocular_extra=200    added with Binocular (was 150): 500 in total
//   grab=40                how close the pet has to get to a drop to take it (was 25)
//   sweep_while_moving=1   the pet goes for drops while you move, not just when you stand still
//   leash=500              how far the pet may get from you before it teleports back (was 300)
// The server (config.yaml PET_*) checks the same ranges and also collects for pets with Wing Boots.

namespace {
    int g_nSweepRange = 300;

    // replaces (15 bytes at 0x5083DB) the sweep window around the owner's x ([ebp-0x44]):
    //   left = x - 125 -> [ebp-0x5C], right = x + 125 -> [ebp-0x54]
    __declspec(naked) void sweep_window() {
        __asm {
            mov ecx, [ebp - 0x44]
            mov eax, ecx
            sub eax, [g_nSweepRange]
            mov [ebp - 0x5C], eax
            mov eax, ecx
            add eax, [g_nSweepRange]
            mov [ebp - 0x54], eax
            ret
        }
    }

    int clamp_int(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}

void AttachPetMod() {
    const char* sConfig = ".\\" CONSTANTS_CONFIG_NAME;
    if (!GetPrivateProfileIntA("pet", "enabled", 1, sConfig)) {
        FsLog("pet", "pet looting changes off (config.ini [pet] enabled=0)");
        return;
    }
    g_nSweepRange = clamp_int(GetPrivateProfileIntA("pet", "sweep_range", 300, sConfig), 50, 1000);
    int nBinocularExtra = clamp_int(GetPrivateProfileIntA("pet", "binocular_extra", 200, sConfig), 0, 1000);
    int nGrab = clamp_int(GetPrivateProfileIntA("pet", "grab", 40, sConfig), 10, 120);
    bool bWhileMoving = GetPrivateProfileIntA("pet", "sweep_while_moving", 1, sConfig) != 0;
    int nLeash = clamp_int(GetPrivateProfileIntA("pet", "leash", 500, sConfig), 100, 1500);

    // sweep search window around the owner
    PatchCall(0x005083DB, &sweep_window, 15);
    Patch4(0x005083FD, static_cast<unsigned int>(nBinocularExtra)); // mov eax, 150 -> extra with longRange

    // grab distance (left/right) in TryPickUpDropByPet: lea edx,[eax-0x19] / add eax,0x19
    Patch1(0x00504855, static_cast<unsigned char>(-nGrab));
    Patch1(0x00504865, static_cast<unsigned char>(nGrab));

    // go for drops while the owner moves: skip "owner moved in the last 100 ms" (jbe -> nop nop)
    if (bWhileMoving) {
        Patch1(0x009C4C1D, 0x90);
        Patch1(0x009C4C1E, 0x90);
    }

    // leash: rect around the pet the owner must be inside, else the pet teleports to the owner
    Patch4(0x009C4441, static_cast<unsigned int>(-nLeash)); // left  (-300)
    Patch4(0x009C444F, static_cast<unsigned int>(nLeash));  // right (+300)

    FsLog("pet", "pet looting: range %d (+%d with Binocular), grab %d, while moving %s, leash %d",
            g_nSweepRange, nBinocularExtra, nGrab, bWhileMoving ? "on" : "off", nLeash);
}
