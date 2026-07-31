// dlopen smoke test: load the built plugin, confirm getName() and the menu
// (getFuncsArray) without needing the Nextpad++ host.
//
// Usage: loader <path to CSVLint.so>

#include "NppPluginInterfaceLinux.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <so>\n", argv[0]); return 2; }
    // RTLD_LAZY | RTLD_LOCAL — exactly what the host's load_plugin() uses. The
    // plugin's scintilla_view_send_message is deliberately undefined until the
    // host's libscintilla.so provides it, so RTLD_NOW would fail here.
    void *h = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
    if (!h) { printf("dlopen FAIL: %s\n", dlerror()); return 1; }

    auto pGetName  = (const char *(*)())dlsym(h, "getName");
    auto pGetFuncs = (FuncItem * (*)(int *)) dlsym(h, "getFuncsArray");
    auto pSetInfo  = (void (*)(LinuxHostNppData))dlsym(h, "setInfo");
    if (!pGetName || !pGetFuncs || !pSetInfo) { printf("dlsym FAIL\n"); return 1; }

    // The host's load_plugin() requires ALL FIVE exports and silently skips
    // the plugin when any is missing — isUnicode is the one macOS never asks
    // for, so check it explicitly here.
    if (!dlsym(h, "beNotified") || !dlsym(h, "messageProc") || !dlsym(h, "isUnicode")) {
        printf("dlsym FAIL: missing beNotified/messageProc/isUnicode "
               "(host would silently skip this plugin)\n");
        return 1;
    }

    // setInfo() reads the persisted settings, so it sends host messages — the
    // real host always installs a callback, so supply a stub rather than a
    // zeroed struct (a NULL hostMsg would be dereferenced by the bridge).
    LinuxHostNppData nd{};
    nd.hostMsg = [](unsigned int, unsigned long, long) -> long { return 0; };
    pSetInfo(nd);   // fills the item names

    printf("getName: '%s'\n", pGetName());
    int n = 0;
    FuncItem *f = pGetFuncs(&n);
    printf("nbFunc: %d\n", n);
    for (int i = 0; i < n; ++i)
        printf("  [%2d] '%s'%s\n", i, f[i]._itemName, f[i]._pFunc ? "" : "   <separator>");

    printf("sizeof(FuncItem): %zu (host expects 80)\n", sizeof(FuncItem));

    // 1:1 with the Windows plugin's menu (Main.cs SetCommand table); macOS
    // uses EMPTY names for the three separators; this host SKIPS empty-named
    // items entirely and wants exactly "-".
    bool ok = (n == 11) && (sizeof(FuncItem) == 80) &&
              !strcmp(pGetName(), "CSV Lint") &&
              !strcmp(f[0]._itemName, "CSV Lint window") &&
              !strcmp(f[1]._itemName, "-") && f[1]._pFunc == nullptr &&
              !strcmp(f[2]._itemName, "Analyse data report") &&
              !strcmp(f[3]._itemName, "Select columns") &&
              !strcmp(f[4]._itemName, "-") && f[4]._pFunc == nullptr &&
              !strcmp(f[5]._itemName, "Convert data") &&
              !strcmp(f[6]._itemName, "Generate metadata") &&
              !strcmp(f[7]._itemName, "-") && f[7]._pFunc == nullptr &&
              !strcmp(f[8]._itemName, "Settings") &&
              !strcmp(f[9]._itemName, "Documentation") &&
              !strcmp(f[10]._itemName, "About");
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
