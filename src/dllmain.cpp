#include "OpenSporeMod.h"
#include <Spore\ModAPI.h>

static OpenSpore::OpenSporeMod* gMod = nullptr;

void Initialize() {
    gMod = new OpenSpore::OpenSporeMod();
    gMod->Initialize();
}

void Dispose() {
    if (gMod) {
        gMod->Dispose();
        delete gMod;
        gMod = nullptr;
    }
}

void AttachDetours() {
    // Detours will be added here as we hook more game functions.
    // For the initial version, we rely on the message system for events.
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved) {
    switch (dwReason) {
    case DLL_PROCESS_ATTACH:
        ModAPI::AddPostInitFunction(Initialize);
        ModAPI::AddDisposeFunction(Dispose);
        PrepareDetours(hModule);
        AttachDetours();
        CommitDetours();
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
