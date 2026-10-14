#include "pch.h"
#include "hook.h"
#include "debug.h"

// FreedomStory: free skills don't use up a skill tab.
//
// The server gives some skills for free at max level (server/custom/FreeSkills.java): Haste for a Bandit,
// Flash Jump (4211010 + the Hermit's 4111006 it runs) for a Chief Bandit.
//
// When you add a point to a skill, the client (0x8ACFC5..0x8AD231) adds up the points in all the skills you
// have from that job level and refuses with "You're lacking Level 1 (or 2) Skills." when the tab is already
// at its limit for your level (2nd job: 3 x level - 89, 3rd job: 3 x level - 209,
// 4th job: 3 x level - 357). It counts the free skills
// too, so a Chief Bandit's tab was "full" (40 points of Flash Jump) until about level 84.
//
// The three counting loops read each skill with ZMap::GetNext (0x4E87BE, thiscall(pos*, level*) -> key*);
// those calls go through free_skill_get_next, which reports level 0 for the free skills.
// Keep the list in sync with the server's FreeSkills.java.

namespace {
    const int FREE_SKILLS[] = {
        4201003, // Bandit: Haste
        4211010, // Chief Bandit: Flash Jump
        4111006, // Hermit: Flash Jump (granted with the Chief Bandit's)
    };

    uintptr_t g_pZMapGetNext = 0x004E87BE; // ZMap<long, int>::GetNext

    void __cdecl skip_free_skill(const int* pKey, int* pLevel) {
        if (!pKey || !pLevel) {
            return;
        }
        for (int nId : FREE_SKILLS) {
            if (*pKey == nId) {
                *pLevel = 0;
                return;
            }
        }
    }

    // same calling convention as ZMap::GetNext: ecx = map, [esp+4] = pos*, [esp+8] = level*, ret 8
    __declspec(naked) void free_skill_get_next() {
        __asm {
            push [esp + 8]          // level*
            push [esp + 8]          // pos*
            call [g_pZMapGetNext]   // eax = key*
            push eax
            push [esp + 0x0C]       // level* (after the push: ret, pos, level -> esp+4+8)
            push eax                // key*
            call skip_free_skill
            add esp, 8
            pop eax
            ret 8
        }
    }
}

void AttachFreeSkills() {
    const uintptr_t CALLS[] = {
        0x008AD07C, // adding to a 4th job skill: counts the 4th job skills (limit 3 x level - 357)
        0x008AD126, // adding to a 3rd job skill: counts the 3rd job skills (limit 3 x level - 209)
        0x008AD1BE, // adding to a 2nd job skill: counts the 2nd job skills (limit 3 x level - 89)
    };
    for (uintptr_t uAddr : CALLS) {
        if (*reinterpret_cast<unsigned char*>(uAddr) != 0xE8) {
            FsLog("skills", "free skills: unexpected code at %08X, not patched", static_cast<unsigned>(uAddr));
            return;
        }
    }
    for (uintptr_t uAddr : CALLS) {
        PatchCall(uAddr, &free_skill_get_next);
    }
    FsLog("skills", "free skills (Haste, Flash Jump) don't count toward the skill tab limits");
}
