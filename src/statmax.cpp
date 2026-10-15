#include "pch.h"
#include <cstdio>
#include <cstring>
#include "hook.h"
#include "debug.h"
#include "wvs/ctrlwnd.h"
#include "ztl/ztl.h"

// FreedomStory: "all AP" buttons in the stat window.
//
// Next to the AP up arrow of STR, DEX, INT and LUK there is a second button (red, double arrow) that puts
// ALL remaining AP into that stat, after a Yes/No confirmation. It sends the client's own auto-assign
// packet (CWvsContext::SendAPAutoAssign, 0xA23B3D) with that one stat; the server checks it like any
// auto-assign (AssignAPProcessor, USE_SERVER_AUTOASSIGNER off).
//
// The window is 14px wider to fit the buttons:
//   - background: Custom.wz UI/UIWindow.img/Stat/backgrnd (189x347, the original 175 + 14)
//   - window width 176 -> 190 (CUIStat ctor), close button, AUTO-ASSIGN and DETAIL buttons move right,
//     the detail panel opens 14px further right
//   - button art: Custom.wz UI/UIWindowEx.img/StatApMax/BtApMax (normal/mouseOver/pressed/disabled)

namespace {
    const int WIDEN = 14;

    // CUIStat (v83 GMS)
    const uintptr_t STAT_WND_WIDTH = 0x008C4AB4;      // ctor: push 0xB0 (CreateWnd width)
    const uintptr_t STAT_CLOSE_X = 0x008C485B;        // ctor: push 0x9C (close button x)
    const uintptr_t STAT_DETAIL_X[] = {0x008C4EA3, 0x008C5761, 0x008C6C73}; // add eax, 0xAA (detail panel x)
    const uintptr_t STAT_DETAIL_X_NEG = 0x008C575C;   // and eax, -0xAA (same offset, the "open" case)
    const uintptr_t STAT_AUTO_X[] = {0x008C7D64, 0x008C7E1F, 0x008C7EDE};   // push 0x61 (AUTO-ASSIGN buttons x)
    const uintptr_t STAT_DETAILBT_X = 0x008C4E21;     // push 0x7C (DETAIL button x)

    const uintptr_t STAT_BT_STR = 0x5C4;              // ZRef<CCtrlButton> of the STR up arrow (+4 = pointer)
    const int BT_X = 153 + WIDEN;                     // the up arrows are at x 153 (12px wide)
    const int BT_Y[4] = {247, 265, 283, 301};         // STR, DEX, INT, LUK (same as the up arrows)
    const unsigned int BT_ID = 2010;                  // up arrows: 2000 HP .. 2005 LUK; 2006-2009 are taken
    const int STAT_FLAG[4] = {0x40, 0x80, 0x100, 0x200}; // STR, DEX, INT, LUK (CharacterStat flags)
    const char* STAT_NAME[4] = {"STR", "DEX", "INT", "LUK"};

    const uintptr_t WVSCONTEXT_INSTANCE = 0x00BE7918;
    const uintptr_t WVSCONTEXT_CHARACTERDATA = 0x20B8;
    const uintptr_t CD_AP = 0x81;                     // ZtlSecure<short> nAP (key at +4)
    auto decode_secure_short = reinterpret_cast<short(__cdecl*)(void* p, int key)>(0x004746DD);

    ZRef<CCtrlButton> g_aBtMax[4];

    int get_ap() {
        uintptr_t pContext = *reinterpret_cast<uintptr_t*>(WVSCONTEXT_INSTANCE);
        if (!pContext) {
            return 0;
        }
        uintptr_t pCd = *reinterpret_cast<uintptr_t*>(pContext + WVSCONTEXT_CHARACTERDATA);
        if (!pCd) {
            return 0;
        }
        return decode_secure_short(reinterpret_cast<void*>(pCd + CD_AP), *reinterpret_cast<int*>(pCd + CD_AP + 4));
    }

    void set_enable(CCtrlButton* pBt, bool bEnable) {
        // IUIMsgHandler part (+4), vtable +0x1C = Enable(int) - what CUIStat does for its own buttons (0x8C58B1)
        uintptr_t pHandler = reinterpret_cast<uintptr_t>(pBt) + 4;
        void** vt = *reinterpret_cast<void***>(pHandler);
        reinterpret_cast<void(__thiscall*)(uintptr_t, int)>(vt[0x1C / 4])(pHandler, bEnable ? 1 : 0);
    }

    void release_buttons() {
        for (auto& bt : g_aBtMax) {
            bt = nullptr;
        }
    }

    void create_buttons(void* pStat) {
        for (int i = 0; i < 4; i++) {
            if (g_aBtMax[i]) {
                continue;
            }
            CCtrlButton::CREATEPARAM param;
            param.sUOL = L"UI/UIWindowEx.img/StatApMax/BtApMax";
            g_aBtMax[i] = new CCtrlButton();
            CCtrlButton* pBt = g_aBtMax[i];
            // CCtrlButton::CreateCtrl (vtable +0x20), as CUIStat creates its up arrows (0x8C7B2A)
            void** vt = *reinterpret_cast<void***>(pBt);
            reinterpret_cast<void(__thiscall*)(CCtrlButton*, void*, unsigned int, int, int, int, void*)>(vt[0x20 / 4])(
                    pBt, pStat, BT_ID + i, BT_X, BT_Y[i], 1, &param);
        }
        int nAp = get_ap();
        for (auto& bt : g_aBtMax) {
            if (bt) {
                set_enable(bt, nAp > 0);
            }
        }
    }

    // CUtilDlg::YesNo(message, 0, 1) - returns 6 (IDYES) for Yes; the AUTO-ASSIGN confirmation uses it (0x8C8839)
    auto util_dlg_yes_no = reinterpret_cast<int(__cdecl*)(ZXString<char>, int, int)>(0x00992DFD);
    // CWvsContext::SendAPAutoAssign(ZArray<pair<flag, amount>>*) (0xA23B3D)
    auto send_ap_auto_assign = reinterpret_cast<void(__thiscall*)(void*, void*)>(0x00A23B3D);

    void on_max_clicked(int nStat) {
        int nAp = get_ap();
        if (nAp <= 0) {
            return;
        }
        char sMsg[160];
        sprintf_s(sMsg, "Put all %d AP into %s?", nAp, STAT_NAME[nStat]);
        if (util_dlg_yes_no(ZXString<char>(sMsg), 0, 1) != 6) {
            return;
        }
        nAp = get_ap(); // may have changed while the dialog was open
        void* pContext = *reinterpret_cast<void**>(WVSCONTEXT_INSTANCE);
        if (nAp <= 0 || !pContext) {
            return;
        }
        // ZArray<pair<long,long>>: data pointer with the count just before it. The server reads two pairs,
        // so the second is another stat with 0 AP.
        int nOther = nStat == 0 ? 1 : 0;
        int aBuff[5] = {2, STAT_FLAG[nStat], nAp, STAT_FLAG[nOther], 0};
        void* pArray = &aBuff[1];
        FsLog("stat", "all AP: %d into %s", nAp, STAT_NAME[nStat]);
        send_ap_auto_assign(pContext, &pArray);
    }
}

class CUIStat {
public:
    MEMBER_HOOK(void, 0x008C79F5, ResetButtons)       // creates/removes the AP buttons
    MEMBER_HOOK(void, 0x008C5845, EnableButtons)      // enables the AP buttons when there is AP
    MEMBER_HOOK(void, 0x008C522F, OnButtonClicked, unsigned int nId)
    MEMBER_HOOK(void, 0x008C4B3C, Destructor)
};

void CUIStat::ResetButtons_hook() {
    CUIStat::ResetButtons(this);
    bool bHasUpArrows = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(this) + STAT_BT_STR) != 0;
    if (bHasUpArrows) {
        create_buttons(this);
    } else {
        release_buttons();
    }
}

void CUIStat::EnableButtons_hook() {
    CUIStat::EnableButtons(this);
    int nAp = get_ap();
    for (auto& bt : g_aBtMax) {
        if (bt) {
            set_enable(bt, nAp > 0);
        }
    }
}

void CUIStat::OnButtonClicked_hook(unsigned int nId) {
    if (nId >= BT_ID && nId < BT_ID + 4) {
        on_max_clicked(static_cast<int>(nId - BT_ID));
        return;
    }
    CUIStat::OnButtonClicked(this, nId);
}

void CUIStat::Destructor_hook() {
    CUIStat::Destructor(this);
    release_buttons();
}

void AttachStatMaxButtons() {
    const unsigned char WIDTH[] = {0x68, 0xB0, 0x00, 0x00, 0x00};
    const unsigned char CLOSE[] = {0x68, 0x9C, 0x00, 0x00, 0x00};
    const unsigned char ADD_AA[] = {0x05, 0xAA, 0x00, 0x00, 0x00};
    const unsigned char AND_NEG[] = {0x25, 0x56, 0xFF, 0xFF, 0xFF};
    const unsigned char PUSH_61[] = {0x6A, 0x61};
    const unsigned char PUSH_7C[] = {0x6A, 0x7C};
    bool bOk = memcmp(reinterpret_cast<void*>(STAT_WND_WIDTH - 1), WIDTH, 5) == 0 &&
               memcmp(reinterpret_cast<void*>(STAT_CLOSE_X - 1), CLOSE, 5) == 0 &&
               memcmp(reinterpret_cast<void*>(STAT_DETAIL_X_NEG - 1), AND_NEG, 5) == 0 &&
               memcmp(reinterpret_cast<void*>(STAT_DETAILBT_X - 1), PUSH_7C, 2) == 0;
    for (uintptr_t u : STAT_DETAIL_X) {
        bOk = bOk && memcmp(reinterpret_cast<void*>(u - 1), ADD_AA, 5) == 0;
    }
    for (uintptr_t u : STAT_AUTO_X) {
        bOk = bOk && memcmp(reinterpret_cast<void*>(u - 1), PUSH_61, 2) == 0;
    }
    if (!bOk) {
        FsLog("stat", "all AP buttons: unexpected client code, not patched");
        return;
    }
    Patch4(STAT_WND_WIDTH, 0xB0 + WIDEN);
    Patch4(STAT_CLOSE_X, 0x9C + WIDEN);
    for (uintptr_t u : STAT_DETAIL_X) {
        Patch4(u, 0xAA + WIDEN);
    }
    Patch4(STAT_DETAIL_X_NEG, static_cast<unsigned int>(-(0xAA + WIDEN)));
    for (uintptr_t u : STAT_AUTO_X) {
        Patch1(u, 0x61 + WIDEN);
    }
    Patch1(STAT_DETAILBT_X, 0x7C + WIDEN);

    bool bHooks = ATTACH_HOOK(CUIStat::ResetButtons, CUIStat::ResetButtons_hook) &&
                  ATTACH_HOOK(CUIStat::EnableButtons, CUIStat::EnableButtons_hook) &&
                  ATTACH_HOOK(CUIStat::OnButtonClicked, CUIStat::OnButtonClicked_hook) &&
                  ATTACH_HOOK(CUIStat::Destructor, CUIStat::Destructor_hook);
    FsLog("stat", "all AP buttons: %s", bHooks ? "ok" : "FAILED");
}
