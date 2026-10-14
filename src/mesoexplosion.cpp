#include "pch.h"
#include <cstring>
#include "hook.h"
#include "debug.h"
#include "constants.h"

// FreedomStory: big meso bags for Meso Explosion.
//
// The v83 client works out Meso Explosion damage per pile (CalcDamage, 0x791FBC):
//   damage = 50 x skill x (0.5 .. 1.0 random) x size factor
//   size factor: up to 1,000 mesos (0.82 x mesos + 28) / 5300, above that mesos / (mesos + 5250)
// so the factor stops near 1 and 5k bags give the most damage per meso (~3.7 damage per meso at level 30).
// And the meso drop box only goes up to 50,000 (0x81DAC6).
//
// Changes (config.ini [meso_explosion], defaults shown):
//   power=300            above 1,000 mesos the size factor grows with the bag: mesos x power / 1,025,000.
//                        100 = as good per meso as the best normal (5k) bag, 300 = three times that.
//                        0 = the normal v83 formula.
//   max_drop=10000000    the most mesos the drop box lets you drop at once (server: MESO_DROP_MAX).
// Bags of 1,000 mesos or less work as before.

namespace {
    int g_nPower = 300;
    uintptr_t g_pRatioDone = 0x007920D1; // continues with the size factor in st(0)

    double __cdecl meso_size_factor(int nMeso) {
        if (nMeso <= 1000) {
            return (nMeso * 0.82 + 28.0) / 5300.0;
        }
        double dNormal = nMeso / (nMeso + 5250.0);
        if (g_nPower <= 0) {
            return dNormal;
        }
        double dBig = static_cast<double>(nMeso) * g_nPower / 1025000.0;
        if (dBig > 40000.0) {
            dBig = 40000.0; // 50 x 1000 x 40000 = 2,000,000,000: stays inside a 32-bit damage number
        }
        return dBig > dNormal ? dBig : dNormal;
    }

    // replaces the size factor (0x79209E..0x7920D0); [ebp+0x20] points at this pile's meso amount
    __declspec(naked) void meso_size_factor_hook() {
        __asm {
            mov eax, [ebp + 0x20]
            push [eax]
            call meso_size_factor   // st(0) = factor; esi/edi/ebx are kept
            add esp, 4
            jmp [g_pRatioDone]
        }
    }

    int clamp_int(int v, int lo, int hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}

void AttachMesoExplosionMod() {
    const char* sConfig = ".\\" CONSTANTS_CONFIG_NAME;
    g_nPower = clamp_int(GetPrivateProfileIntA("meso_explosion", "power", 300, sConfig), 0, 1000);
    int nMaxDrop = clamp_int(GetPrivateProfileIntA("meso_explosion", "max_drop", 10000000, sConfig), 50000, 2000000000);

    const unsigned char RATIO_START[] = {0x8B, 0x45, 0x20, 0x8B, 0x00, 0x3D, 0xE8, 0x03, 0x00, 0x00}; // mov eax,[ebp+20]; mov eax,[eax]; cmp eax,1000
    const unsigned char DROP_MAX[] = {0xB9, 0x50, 0xC3, 0x00, 0x00};                               // mov ecx, 50000
    if (memcmp(reinterpret_cast<void*>(0x0079209E), RATIO_START, sizeof(RATIO_START)) != 0 ||
        memcmp(reinterpret_cast<void*>(0x0081DAC6), DROP_MAX, sizeof(DROP_MAX)) != 0) {
        FsLog("meso", "meso explosion: unexpected client code, not patched");
        return;
    }
    PatchJmp(0x0079209E, &meso_size_factor_hook);
    Patch4(0x0081DAC7, nMaxDrop);
    FsLog("meso", "meso explosion: power %d, drop up to %d mesos", g_nPower, nMaxDrop);
}
