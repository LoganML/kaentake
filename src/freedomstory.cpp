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

void AttachFreedomStoryMods() {
    bool bOk = ATTACH_HOOK(CUserLocal::DoActiveSkill, CUserLocal::DoActiveSkill_hook);
    FsLog("hook", "skill aliases (DoActiveSkill): %s", bOk ? "ok" : "FAILED");
}
