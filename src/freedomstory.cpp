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
    {4211011, 4111002}, // Chief Bandit: Shadow Partner (Hermit's)
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

void AttachFreedomStoryMods() {
    // Never block character creation. The "Create a character" button (CLogin, 0x5F6425) looks up the
    // selected world's bBlockCharCreation flag (server list entry +0x18) using the world *number* as an
    // index into the world list. The server shows its single world as Windia (world 3, display offset),
    // so the lookup reads past the one-entry list and random memory can block creation ("Due to the
    // high population..."). je -> jmp skips that notice; the server never blocks creation anyway.
    Patch1(0x005F6440, 0xEB);
    FsLog("hook", "character creation block check: off");

    // Beginners can place AP at any level. The stat window locks the AP up buttons for Beginner jobs
    // (0, 1000, 2000, 2001) at level 10 and below ("Basic Stats": the official server auto-assigned
    // their AP; ours doesn't, USE_AUTOASSIGN_STARTERS_AP is off). Each check is
    // `cmp al, 0x0A; ja <normal>` on the level, so the level limit 10 -> 0 turns it off
    // (the same edit the HeavenMS localhost has):
    //   0x8C7A6C  CUIStat button setup: deletes the six AP up buttons
    //   0x8C61A4  CUIStat draw: Beginner layout of the AP area
    //   0x8C53F0  CUIStat tooltip: the "Basic Stats" tooltip
    const uintptr_t aBeginnerApLevel[] = {0x008C7A6C, 0x008C61A4, 0x008C53F0};
    int nBeginnerAp = 0;
    for (uintptr_t uAddr : aBeginnerApLevel) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(uAddr);
        if (p[0] == 0x3C && (p[1] == 0x0A || p[1] == 0x00)) { // cmp al, 10 (or already 0)
            Patch1(uAddr + 1, 0x00);
            nBeginnerAp++;
        } else {
            FsLog("hook", "beginner AP: unexpected bytes at 0x%08X (%02X %02X), not patched", uAddr, p[0], p[1]);
        }
    }
    FsLog("hook", "beginner AP lock: %d/3 off", nBeginnerAp);

    bool bOk = ATTACH_HOOK(CUserLocal::DoActiveSkill, CUserLocal::DoActiveSkill_hook);
    FsLog("hook", "skill aliases (DoActiveSkill): %s", bOk ? "ok" : "FAILED");
}
