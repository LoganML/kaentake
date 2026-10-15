#include "pch.h"
#include <cstring>
#include "hook.h"
#include "debug.h"

// FreedomStory: Shadow Partner for a Chief Bandit's dagger attacks.
//
// The server gives a Chief Bandit the Hermit's Shadow Partner (server/custom/FreeSkills.java). In v83 the
// shadow's extra hits only exist for throwing-star attacks: the star attack (CUserLocal, 0x9537D5) checks the
// Shadow Partner buff, doubles the hits per monster and tells the damage calculation (0x78DF87) so. The damage
// calculation already handles it for any attack: the second half of the hits copy the first half, including
// crits, at Shadow Partner's y% for a skill (50% at level 30) or x% for a plain attack (80% at level 30).
// The melee attack (CUserLocal, 0x950921) never does either, so dagger attacks got no shadow hits.
//
// This adds it to the melee attack for the skills below, only while Shadow Partner is on:
//   0x951C08  after the hits per monster are set ([ebp-0x58], from the skill's attackCount, max 15):
//             double them (max 15) and remember the attack has shadow hits. The hit count also goes
//             into the attack packet and the damage numbers shown.
//   0x951E42  the call to the damage calculation: pass "shadow partner" = 1 (argument 12, normally 0).
//   0x790209  in the damage calculation, the shadow hits of these attacks use x% (80%) instead of y% (50%).
// The server checks the extra hits (AbstractDealDamageHandler: twice the lines, shadow hits at x%).
// Meso Explosion is left out on purpose.

namespace {
    const int SHADOW_SKILLS[] = {
        4201005, // Savage Blow
        4211002, // Assaulter
        4211004, // Band of Thieves
        4221001, // Assassinate
        4221007, // Boomerang Step
    };

    typedef int(__cdecl* SecureFuse_t)(const void* p, unsigned int key); // decodes a protected int
    const SecureFuse_t SecureFuse = reinterpret_cast<SecureFuse_t>(0x00416563);

    bool g_bShadowHits = false;
    uintptr_t g_pAfterCount = 0x00951C12;  // mobs > 0: continue
    uintptr_t g_pNoMobs = 0x00951CA2;      // original jle target
    uintptr_t g_pPDamage = 0x0078DF87;     // CalcDamage::PDamage
    uintptr_t g_pRateY = 0x00790215;       // shadow hit = first-half hit x y% (skill attacks)
    uintptr_t g_pRateX = 0x0079024D;       // shadow hit = first-half hit x x% (plain attacks)

    bool shadow_partner_on() {
        uintptr_t uContext = *reinterpret_cast<uintptr_t*>(0x00BE7918); // CWvsContext
        if (!uContext) {
            return false;
        }
        uintptr_t uStat = uContext + 0x2134; // secondary stat; Shadow Partner at +0x42C (key +0x434)
        return SecureFuse(reinterpret_cast<void*>(uStat + 0x42C), *reinterpret_cast<unsigned int*>(uStat + 0x434)) != 0;
    }

    void __cdecl shadow_adjust_hits(int nSkillID, int* pnHits) {
        g_bShadowHits = false;
        bool bListed = false;
        for (int nId : SHADOW_SKILLS) {
            if (nId == nSkillID) {
                bListed = true;
                break;
            }
        }
        if (!bListed || !pnHits || *pnHits <= 0 || !shadow_partner_on()) {
            return;
        }
        int nHits = *pnHits * 2;
        if (nHits > 15) {
            nHits = 15;
        }
        FsDebug("shadow", "skill %d: %d -> %d hits", nSkillID, *pnHits, nHits);
        *pnHits = nHits;
        g_bShadowHits = true;
    }

    // replaces "cmp dword [ebp-0x14], 0 / jle 0x951CA2" at 0x951C08
    __declspec(naked) void shadow_hits_hook() {
        __asm {
            pushad
            lea eax, [ebp - 0x58]
            push eax                  // hits per monster
            push dword ptr [ebp - 0x10] // skill id
            call shadow_adjust_hits
            add esp, 8
            popad
            cmp dword ptr [ebp - 0x14], 0 // monsters hit
            jle no_mobs
            jmp [g_pAfterCount]
        no_mobs:
            jmp [g_pNoMobs]
        }
    }

    // called instead of PDamage at 0x951E42 (thiscall, 20 stack arguments; argument 12 at [esp+0x30])
    __declspec(naked) void shadow_pdamage() {
        __asm {
            cmp byte ptr [g_bShadowHits], 0
            je go
            mov dword ptr [esp + 0x30], 1
        go:
            jmp [g_pPDamage]
        }
    }

    // replaces "cmp dword [ebp+38],0 / push [ebp-70] / mov ecx,[ebp-74] / je 79024D" at 0x790209
    // ([ebp+38] = the skill, [ebp-70]/[ebp-74] = Shadow Partner's level and data)
    __declspec(naked) void shadow_rate_hook() {
        __asm {
            push dword ptr [ebp - 0x70]
            mov ecx, [ebp - 0x74]
            cmp byte ptr [g_bShadowHits], 0
            jne rate_x
            cmp dword ptr [ebp + 0x38], 0
            je rate_x
            jmp [g_pRateY]
        rate_x:
            jmp [g_pRateX]
        }
    }
}

void AttachShadowMelee() {
    const unsigned char COUNT_CHECK[] = {0x83, 0x7D, 0xEC, 0x00, 0x0F, 0x8E, 0x90, 0x00, 0x00, 0x00}; // cmp [ebp-14],0; jle 951CA2
    const unsigned char PDAMAGE_CALL[] = {0xE8, 0x40, 0xC1, 0xE3, 0xFF};                         // call 78DF87
    const unsigned char SHADOW_ARG[] = {0x56, 0xFF, 0x75, 0xCC};                                  // push esi (arg 12); push [ebp-34]
    const unsigned char SHADOW_RATE[] = {0x83, 0x7D, 0x38, 0x00, 0xFF, 0x75, 0x90, 0x8B, 0x4D, 0x8C, 0x74, 0x38};
    if (memcmp(reinterpret_cast<void*>(0x00951C08), COUNT_CHECK, sizeof(COUNT_CHECK)) != 0 ||
        memcmp(reinterpret_cast<void*>(0x00951E42), PDAMAGE_CALL, sizeof(PDAMAGE_CALL)) != 0 ||
        memcmp(reinterpret_cast<void*>(0x00951E18), SHADOW_ARG, sizeof(SHADOW_ARG)) != 0 ||
        memcmp(reinterpret_cast<void*>(0x00790209), SHADOW_RATE, sizeof(SHADOW_RATE)) != 0) {
        FsLog("shadow", "shadow partner melee: unexpected client code, not patched");
        return;
    }
    PatchJmp(0x00951C08, &shadow_hits_hook);
    for (uintptr_t u = 0x00951C0D; u < 0x00951C12; u++) {
        Patch1(u, 0x90);
    }
    PatchCall(0x00951E42, &shadow_pdamage);
    PatchJmp(0x00790209, &shadow_rate_hook);
    for (uintptr_t u = 0x0079020E; u < 0x00790215; u++) {
        Patch1(u, 0x90);
    }
    FsLog("shadow", "shadow partner: extra hits for Chief Bandit dagger skills");
}
