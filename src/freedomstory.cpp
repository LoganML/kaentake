#include "pch.h"
#include "hook.h"
#include "debug.h"

// FreedomStory client changes.
//
// Skill aliases: a skill copied into another job's skill book gets its own number (a skill number
// may only appear in one book - listing the Hermit's Flash Jump in the Chief Bandit book crashed the
// client). When the copy is used, run it as the original so the client's own logic for that skill
// (e.g. the Flash Jump movement) applies. The server grants both numbers.

struct SkillAlias {
    int nCopy;      // number in Custom.wz (client/wz/Skill.wz/...)
    int nOriginal;  // the skill whose behaviour it borrows
};

static const SkillAlias g_aSkillAlias[] = {
    {4211010, 4111006}, // Chief Bandit: Flash Jump (Hermit's)
};

class CUserLocal {
public:
    // CUserLocal::DoActiveSkill(nSkillID, nScanCode, pnConsumeCheck) - using an active skill (key press)
    MEMBER_HOOK(int, 0x00966F7A, DoActiveSkill, int nSkillID, int nScanCode, int* pnConsumeCheck)
};

int CUserLocal::DoActiveSkill_hook(int nSkillID, int nScanCode, int* pnConsumeCheck) {
    for (const auto& alias : g_aSkillAlias) {
        if (nSkillID == alias.nCopy) {
            FsDebug("skill", "alias %d -> %d", nSkillID, alias.nOriginal);
            nSkillID = alias.nOriginal;
            break;
        }
    }
    return CUserLocal::DoActiveSkill(this, nSkillID, nScanCode, pnConsumeCheck);
}

// --- diagnostics: double-clicking a Use item (why a reward box does nothing) ----------------------
// CUIItem double-click on the Use tab (0x4EFD25): two state checks, then for items with reward data
// (CItemInfo reward cache, 0x4F2ED6) the item-reward request (0xA1249F), otherwise the normal use (0xA092FB).
// Each step is logged to the client log ([item]).
namespace {
    typedef int(__fastcall* thiscall0_t)(void* self, void* edx);
    typedef int(__fastcall* thiscall1_t)(void* self, void* edx, int a);
    typedef int(__fastcall* thiscall2_t)(void* self, void* edx, int a, int b);

    int __fastcall use_check1_hook(void* self, void* edx) {
        int r = reinterpret_cast<thiscall0_t>(0x004F0D45)(self, edx);
        if (r) FsLog("item", "use blocked: character state check 1");
        return r;
    }
    int __fastcall use_check2_hook(void* self, void* edx) {
        int r = reinterpret_cast<thiscall0_t>(0x004F0D64)(self, edx);
        if (r) FsLog("item", "use blocked: character state check 2");
        return r;
    }
    int __fastcall use_excl_hook(void* self, void* edx, int delay, int force) {
        int r = reinterpret_cast<thiscall2_t>(0x00485BF7)(self, edx, delay, force);
        if (!r) FsLog("item", "use: waiting on an earlier request (excl), skipped the reward check");
        return r;
    }
    int __fastcall use_reward_lookup_hook(void* self, void* edx, int itemId) {
        int r = reinterpret_cast<thiscall1_t>(0x004F2ED6)(self, edx, itemId);
        FsLog("item", "use: item %d has reward data in the client: %s", itemId, r ? "yes" : "no");
        return r;
    }
    int __fastcall use_reward_send_hook(void* self, void* edx, int slot, int itemId) {
        FsLog("item", "use: sending item-reward request for item %d (slot %d)", itemId, slot);
        return reinterpret_cast<thiscall2_t>(0x00A1249F)(self, edx, slot, itemId);
    }
    int __fastcall use_normal_send_hook(void* self, void* edx, int slot, int itemId) {
        FsLog("item", "use: normal item use for item %d (slot %d)", itemId, slot);
        return reinterpret_cast<thiscall2_t>(0x00A092FB)(self, edx, slot, itemId);
    }
}

static void AttachItemUseDiagnostics() {
    PatchCall(0x004F030A, &use_check1_hook);
    PatchCall(0x004F031A, &use_check2_hook);
    PatchCall(0x004F0353, &use_excl_hook);
    PatchCall(0x004F0372, &use_reward_lookup_hook);
    PatchCall(0x004F038C, &use_reward_send_hook);
    PatchCall(0x004F045B, &use_normal_send_hook);
    FsLog("hook", "item use diagnostics: on");
}

void AttachFreedomStoryMods() {
    // Never block character creation. The "Create a character" button (CLogin, 0x5F6425) looks up the
    // selected world's bBlockCharCreation flag (server list entry +0x18) using the world *number* as an
    // index into the world list. The server shows its single world as Windia (world 3, display offset),
    // so the lookup reads past the one-entry list and random memory can block creation ("Due to the
    // high population..."). je -> jmp skips that notice; the server never blocks creation anyway.
    Patch1(0x005F6440, 0xEB);
    FsLog("hook", "character creation block check: off");

    bool bOk = ATTACH_HOOK(CUserLocal::DoActiveSkill, CUserLocal::DoActiveSkill_hook);
    FsLog("hook", "skill aliases (DoActiveSkill): %s", bOk ? "ok" : "FAILED");
    AttachItemUseDiagnostics();
}
