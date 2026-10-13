#include "pch.h"
#include "hook.h"
#include "debug.h"
#include "constants.h"
#include <algorithm>

// FreedomStory: built-in critical strike chance for every class (physical attacks).
//
// The v83 client rolls crits only for bows/crossbows/claws/knuckles (their crit skills) or when
// Sharp Eyes is active. Sharp Eyes is stored as one value: (chance << 8) | bonus damage %.
// The physical damage code reads it through the ZtlSecure decoder at five call sites; those
// calls are redirected here, and we return the higher of the real Sharp Eyes and the built-in
// crit for the character's job tier and level. Classes with a crit skill still add it on top
// (the client adds the Sharp Eyes value to the skill's chance and damage).
//
// Built-in crit (config.ini [crit], defaults shown):
//   chance by job tier: 1st 5%, 2nd 8%, 3rd 12%, 4th 15% (beginners: none)
//   bonus damage: +20% plus 0.4% per level above 10, capped at 155% (the value is one byte;
//   on a crit the client adds this % of the hit)

namespace {
    // client functions (v83 GMS, from a memory dump of the running client)
    auto decode_secure_int = reinterpret_cast<int(__cdecl*)(void* p, int key)>(0x00416563);            // ZtlSecure<int>
    auto decode_secure_byte = reinterpret_cast<unsigned char(__cdecl*)(void* p, int key)>(0x0047465D); // ZtlSecure<unsigned char>
    auto decode_secure_short = reinterpret_cast<short(__cdecl*)(void* p, int key)>(0x004746DD);        // ZtlSecure<short>

    const uintptr_t WVSCONTEXT_INSTANCE = 0x00BE7918; // CWvsContext::ms_pInstance
    const uintptr_t WVSCONTEXT_CHARACTERDATA = 0x20B8; // CWvsContext -> CharacterData*
    const uintptr_t CD_LEVEL = 0x33;                   // ZtlSecure<unsigned char> nLevel (key at +2)
    const uintptr_t CD_JOB = 0x39;                     // ZtlSecure<short> nJob (key at +4)

    // call sites in the physical damage code that decode the Sharp Eyes value
    const uintptr_t SHARP_EYES_READS[] = {0x0078E16D, 0x0078E18F, 0x0078E1A7, 0x0078E1BF, 0x00790139};

    bool g_bEnabled = true;
    int g_aTierChance[4] = {5, 8, 12, 15};
    int g_nDamageBase = 20;        // % at level 10
    int g_nDamagePer10Levels = 4;  // +0.4% per level
    int g_nDamageCap = 155;

    bool get_job_level(int& nJob, int& nLevel) {
        uintptr_t pContext = *reinterpret_cast<uintptr_t*>(WVSCONTEXT_INSTANCE);
        if (!pContext) {
            return false;
        }
        uintptr_t pCd = *reinterpret_cast<uintptr_t*>(pContext + WVSCONTEXT_CHARACTERDATA);
        if (!pCd) {
            return false;
        }
        nLevel = decode_secure_byte(reinterpret_cast<void*>(pCd + CD_LEVEL), *reinterpret_cast<int*>(pCd + CD_LEVEL + 2));
        nJob = decode_secure_short(reinterpret_cast<void*>(pCd + CD_JOB), *reinterpret_cast<int*>(pCd + CD_JOB + 4));
        return true;
    }

    // 0 = beginner (no built-in crit), 1-4 = job advancement tier
    int job_tier(int nJob) {
        if (nJob % 1000 == 0) {
            return 0;                // Beginner, Noblesse, Legend
        }
        if (nJob % 100 == 0) {
            return 1;                // e.g. 400 Rogue, 2100 Aran
        }
        int nSub = nJob % 10;
        return nSub == 0 ? 2 : (nSub == 1 ? 3 : 4); // 420 Bandit, 421 Chief Bandit, 422 Shadower
    }

    int builtin_sharp_eyes() {
        int nJob, nLevel;
        if (!g_bEnabled || !get_job_level(nJob, nLevel)) {
            return 0;
        }
        int nTier = job_tier(nJob);
        if (nTier == 0) {
            return 0;
        }
        int nChance = std::clamp(g_aTierChance[nTier - 1], 0, 100);
        int nDamage = g_nDamageBase + (std::max(nLevel - 10, 0) * g_nDamagePer10Levels) / 10;
        nDamage = std::clamp(nDamage, 0, std::min(g_nDamageCap, 255));
        return (nChance << 8) | nDamage;
    }

    int __cdecl sharp_eyes_read_hook(void* p, int key) {
        int nReal = decode_secure_int(p, key);
        int nOwn = builtin_sharp_eyes();
        if (nOwn == 0) {
            return nReal;
        }
        // higher chance and higher bonus of the two (a real Sharp Eyes still helps if it's stronger)
        int nChance = std::max(nReal >> 8, nOwn >> 8);
        int nDamage = std::max(nReal & 0xFF, nOwn & 0xFF);
        return (nChance << 8) | nDamage;
    }
}

void AttachCritMod() {
    const char* sConfig = ".\\" CONSTANTS_CONFIG_NAME;
    g_bEnabled = GetPrivateProfileIntA("crit", "enabled", 1, sConfig) != 0;
    g_aTierChance[0] = GetPrivateProfileIntA("crit", "chance_1st", g_aTierChance[0], sConfig);
    g_aTierChance[1] = GetPrivateProfileIntA("crit", "chance_2nd", g_aTierChance[1], sConfig);
    g_aTierChance[2] = GetPrivateProfileIntA("crit", "chance_3rd", g_aTierChance[2], sConfig);
    g_aTierChance[3] = GetPrivateProfileIntA("crit", "chance_4th", g_aTierChance[3], sConfig);
    g_nDamageBase = GetPrivateProfileIntA("crit", "damage_base", g_nDamageBase, sConfig);
    g_nDamagePer10Levels = GetPrivateProfileIntA("crit", "damage_per_10_levels", g_nDamagePer10Levels, sConfig);
    g_nDamageCap = GetPrivateProfileIntA("crit", "damage_cap", g_nDamageCap, sConfig);
    if (!g_bEnabled) {
        FsLog("crit", "built-in crit disabled in config.ini");
        return;
    }
    for (uintptr_t uAddr : SHARP_EYES_READS) {
        PatchCall(uAddr, &sharp_eyes_read_hook);
    }
    FsLog("crit", "built-in crit on: chance %d/%d/%d/%d%%, bonus %d%% +%d.%d%%/level (cap %d%%)",
            g_aTierChance[0], g_aTierChance[1], g_aTierChance[2], g_aTierChance[3],
            g_nDamageBase, g_nDamagePer10Levels / 10, g_nDamagePer10Levels % 10, g_nDamageCap);
}
