#include "pch.h"
#include <cmath>
#include "hook.h"
#include "debug.h"
#include "constants.h"
#include <cwchar>

// FreedomStory: built-in critical strike chance for every class (physical and magic attacks).
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
    // windows.h defines min/max macros (no NOMINMAX in pch.h), so no std::min/max/clamp here
    inline int imax(int a, int b) { return a > b ? a : b; }
    inline int imin(int a, int b) { return a < b ? a : b; }
    inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

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
    // chance also climbs with level, from 5% at level 10 to chance_max at chance_max_level along a curve
    // (curve 20 = squared: slow early, fast late); the job value above is the minimum
    int g_nChanceMax = 100;
    int g_nChanceMaxLevel = 200;
    int g_nChanceCurve = 20;       // exponent x10

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

    int level_chance(int nLevel) {
        const int nStart = 10, nStartChance = 5;
        int nEnd = imax(g_nChanceMaxLevel, nStart + 1);
        if (g_nChanceMax <= nStartChance || nLevel <= nStart) {
            return 0;
        }
        double t = static_cast<double>(imin(nLevel, nEnd) - nStart) / (nEnd - nStart);
        double dCurve = imax(g_nChanceCurve, 1) / 10.0;
        return static_cast<int>(nStartChance + (g_nChanceMax - nStartChance) * std::pow(t, dCurve));
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
        int nChance = imax(g_aTierChance[nTier - 1], level_chance(nLevel));
        nChance = iclamp(nChance, 0, 100);
        int nDamage = g_nDamageBase + (imax(nLevel - 10, 0) * g_nDamagePer10Levels) / 10;
        nDamage = iclamp(nDamage, 0, imin(g_nDamageCap, 255));
        return (nChance << 8) | nDamage;
    }

    int __cdecl sharp_eyes_read_hook(void* p, int key) {
        int nReal = decode_secure_int(p, key);
        int nOwn = builtin_sharp_eyes();
        if (nOwn == 0) {
            return nReal;
        }
        // higher chance and higher bonus of the two (a real Sharp Eyes still helps if it's stronger)
        int nChance = imax(nReal >> 8, nOwn >> 8);
        int nDamage = imax(nReal & 0xFF, nOwn & 0xFF);
        return (nChance << 8) | nDamage;
    }
}

// --- magic attacks --------------------------------------------------------------------------------
// The magic damage code (MDamage) reads Sharp Eyes too (0x791792 chance, 0x7917B7 damage) but reads
// the damage byte differently: a crit multiplies the hit by (value - 100)%, so 124 means +24%.
// (Only Evans add a magic crit skill on top.) Same built-in crit, the damage byte given as 100 + bonus.
namespace {
    const uintptr_t MAGIC_SHARP_EYES_READS[] = {0x00791792, 0x007917B7};

    int __cdecl magic_sharp_eyes_read_hook(void* p, int key) {
        int nReal = decode_secure_int(p, key);
        int nOwn = builtin_sharp_eyes();
        if (nOwn == 0) {
            return nReal;
        }
        int nChance = imax(nReal >> 8, nOwn >> 8);
        int nDamage = imax(nReal & 0xFF, imin(100 + (nOwn & 0xFF), 255));
        return (nChance << 8) | nDamage;
    }
}

// --- tell the server which hits crit -------------------------------------------------------------
// The v83 attack packet carries only damage numbers, so the server had to guess crits (damage above
// its estimated max), which misses most built-in crits - other players didn't see them. The attack
// info keeps, per mob, int aDamage[15] at +0x18 and int aCritical[15] at +0x54 (0x3C after each
// damage). At the damage-encoding calls in the melee and ranged attack packets the register holding
// &aDamage[i] is known; these stubs set the top bit of the encoded damage when that hit crit.
// The server (AbstractDealDamageHandler) reads the bit, clears it, and shows the hit as a crit.
namespace {
    uintptr_t g_pEncode4 = 0x004065A6; // COutPacket::Encode4(unsigned int)

    __declspec(naked) void encode_damage_eax() { // melee: eax = &aDamage[i], [esp+4] = value
        __asm {
            cmp dword ptr [eax + 0x3C], 0
            je done
            or dword ptr [esp + 4], 0x80000000
        done:
            jmp dword ptr [g_pEncode4]
        }
    }

    __declspec(naked) void encode_damage_edi() { // ranged: edi = &aDamage[i]
        __asm {
            cmp dword ptr [edi + 0x3C], 0
            je done
            or dword ptr [esp + 4], 0x80000000
        done:
            jmp dword ptr [g_pEncode4]
        }
    }
}

// --- stat window: CRITICAL RATE / CRITICAL DMG. rows -------------------------------------------
// The detail panel of the stat window (CUIStatDetail, background UI/UIWindow.img/Stat/backgrnd2)
// draws its nine values (ATTACK .. JUMP) in one function. Custom.wz replaces the background with a
// taller one that has two more rows under JUMP; here the window is made as tall as the new art and,
// at the end of the draw function, the two values are drawn the same way the others are.
// The numbers are what an attack would use right now: the built-in crit (or a stronger Sharp Eyes)
// plus the class's own crit skill for the equipped weapon (Critical Throw, Critical Shot, ...).
namespace {
    auto get_critical_skill_level = reinterpret_cast<int(__cdecl*)(void* pCd, int nWeaponID, int nAttackType, int* pnProp, int* pnDamage)>(0x00765095);
    auto get_secure_item_id = reinterpret_cast<int(__fastcall*)(void* pSecure, void* edx)>(0x0042873D); // ZtlSecure<long>::GetData (thiscall)

    const uintptr_t STAT_DETAIL_HEIGHT = 0x008C5106;     // CUIStatDetail ctor: push 203 (window height)
    const unsigned int STAT_DETAIL_NEW_HEIGHT = 203 + 2 * 18;
    const uintptr_t STAT_DETAIL_BUTTON_Y = 0x008C2750;   // CUIStatDetail::OnCreate: push 182 (y of the "<" close button)
    // the panel sits 144px below the stat window's top so both bottoms line up; with 36px more panel it moves up
    // 36px to keep them lined up (opening it from two places, the slide animation, and following a moved window)
    const uintptr_t STAT_DETAIL_OFFSET_Y[] = {0x008C4E92, 0x008C54AA, 0x008C57C0, 0x008C6C66}; // add eax, 0x90
    const uintptr_t STAT_DETAIL_DRAW_END = 0x008C448E;   // CUIStatDetail::Draw, after the JUMP value
    uintptr_t g_pStatDetailDrawResume = 0x008C4495;
    const uintptr_t WVSCONTEXT_SECONDARYSTAT = 0x2134;  // CWvsContext -> SecondaryStat (inline)
    const uintptr_t SS_SHARP_EYES = 0x5DC;              // SecondaryStat: ZtlSecure<int> Sharp Eyes value (key at +8)
    const int VALUE_X = 77, FIRST_ROW_Y = 170, ROW_PITCH = 18; // the other values: x 77, y 8 + 18 * row

    // IWzCanvas::DrawTextA (vtable +0x98), called the way the client's own wrapper (0x4277AD) does
    typedef HRESULT(__stdcall* canvas_draw_text_t)(void* pCanvas, int nLeft, int nTop, BSTR sText, void* pFont,
            VARIANT vAlpha, VARIANT vTabOrg, int* pnRet);

    void draw_value(void* pCanvas, void* pFont, int nTop, const wchar_t* sText) {
        auto pfnDrawText = (*reinterpret_cast<canvas_draw_text_t**>(pCanvas))[0x98 / 4];
        VARIANT vMissing;
        VariantInit(&vMissing);
        V_VT(&vMissing) = VT_ERROR;
        V_ERROR(&vMissing) = DISP_E_PARAMNOTFOUND;
        BSTR bsText = SysAllocString(sText);
        int nRet = 0;
        pfnDrawText(pCanvas, VALUE_X, nTop, bsText, pFont, vMissing, vMissing, &nRet);
        SysFreeString(bsText);
    }

    // chance % and total crit damage % (100 = no bonus) the way the client's damage code adds them up
    bool current_crit(void* pWeaponSlot, int& nChance, int& nDamage) {
        uintptr_t pContext = *reinterpret_cast<uintptr_t*>(WVSCONTEXT_INSTANCE);
        if (!pContext) {
            return false;
        }
        void* pCd = *reinterpret_cast<void**>(pContext + WVSCONTEXT_CHARACTERDATA);
        if (!pCd) {
            return false;
        }
        int nSkillProp = 0, nSkillDamage = 0;
        if (pWeaponSlot) {
            int nWeaponID = get_secure_item_id(reinterpret_cast<char*>(pWeaponSlot) + 0xC, nullptr);
            get_critical_skill_level(pCd, nWeaponID, 1, &nSkillProp, &nSkillDamage);
        }
        uintptr_t pSharpEyes = pContext + WVSCONTEXT_SECONDARYSTAT + SS_SHARP_EYES;
        int nSharpEyes = sharp_eyes_read_hook(reinterpret_cast<void*>(pSharpEyes), *reinterpret_cast<int*>(pSharpEyes + 8));
        nChance = imin(nSkillProp + iclamp(nSharpEyes >> 8, 0, 100), 100);
        nDamage = (nSkillDamage > 0 ? nSkillDamage : 100) + (nSharpEyes & 0xFF);
        if (nChance <= 0 || nDamage <= 100) {
            nChance = 0;
            nDamage = 100;
        }
        return true;
    }

    void __stdcall stat_detail_draw_crit(uintptr_t uEbp) {
        void* pCanvas = *reinterpret_cast<void**>(uEbp - 0x24); // IWzCanvas* the values are drawn on
        void* pFont = *reinterpret_cast<void**>(uEbp - 0x20);   // IWzFont* of the plain (unbuffed) values
        void* pWeaponSlot = *reinterpret_cast<void**>(uEbp - 0x64);
        int nChance, nDamage;
        if (!pCanvas || !pFont || !current_crit(pWeaponSlot, nChance, nDamage)) {
            return;
        }
        wchar_t sText[32];
        swprintf_s(sText, L"%d%%", nChance);
        draw_value(pCanvas, pFont, FIRST_ROW_Y, sText);
        swprintf_s(sText, L"+%d%%", nDamage - 100);
        draw_value(pCanvas, pFont, FIRST_ROW_Y + ROW_PITCH, sText);
    }

    __declspec(naked) void stat_detail_draw_end_hook() {
        __asm {
            pushad
            push ebp
            call stat_detail_draw_crit
            popad
            cmp dword ptr [ebp - 0x64], esi // the two instructions the jump replaced
            mov byte ptr [ebp - 0x4], 6
            jmp dword ptr [g_pStatDetailDrawResume]
        }
    }
}

void AttachStatWindowCrit() {
    Patch4(STAT_DETAIL_HEIGHT, STAT_DETAIL_NEW_HEIGHT);
    Patch4(STAT_DETAIL_BUTTON_Y, 182 + 2 * 18); // keep the button in the bottom strip
    for (uintptr_t uAddr : STAT_DETAIL_OFFSET_Y) {
        Patch4(uAddr, 0x90 - 2 * 18);
    }
    PatchJmp(STAT_DETAIL_DRAW_END, &stat_detail_draw_end_hook);
    PatchNop(STAT_DETAIL_DRAW_END + 5, g_pStatDetailDrawResume);
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
    g_nChanceMax = GetPrivateProfileIntA("crit", "chance_max", g_nChanceMax, sConfig);
    g_nChanceMaxLevel = GetPrivateProfileIntA("crit", "chance_max_level", g_nChanceMaxLevel, sConfig);
    g_nChanceCurve = GetPrivateProfileIntA("crit", "chance_curve", g_nChanceCurve, sConfig);
    AttachStatWindowCrit(); // the stat window rows (they still show a class's own crit skill when off)
    if (!g_bEnabled) {
        FsLog("crit", "built-in crit disabled in config.ini");
        return;
    }
    for (uintptr_t uAddr : SHARP_EYES_READS) {
        PatchCall(uAddr, &sharp_eyes_read_hook);
    }
    PatchCall(0x00952B28, &encode_damage_eax); // CLOSE_RANGE_ATTACK damage list
    PatchCall(0x00955428, &encode_damage_edi); // RANGED_ATTACK damage list
    for (uintptr_t uAddr : MAGIC_SHARP_EYES_READS) {
        PatchCall(uAddr, &magic_sharp_eyes_read_hook);
    }
    PatchCall(0x0095705E, &encode_damage_edi); // MAGIC_ATTACK damage list
    FsLog("crit", "built-in crit on: chance %d/%d/%d/%d%%, bonus %d%% +%d.%d%%/level (cap %d%%)",
            g_aTierChance[0], g_aTierChance[1], g_aTierChance[2], g_aTierChance[3],
            g_nDamageBase, g_nDamagePer10Levels / 10, g_nDamagePer10Levels % 10, g_nDamageCap);
    FsLog("crit", "crit chance climbs to %d%% at level %d (curve %d.%d)",
            g_nChanceMax, g_nChanceMaxLevel, g_nChanceCurve / 10, g_nChanceCurve % 10);
}
