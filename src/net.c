#include "net.h"

#include <string.h>
#include <pspkernel.h>
#include <psputility.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <pspnet_resolver.h>

int net_connect(int profile, int timeout_ms, char *ip_out, int ip_len) {
    int ret;

    if ((ret = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON)) < 0) return ret;
    if ((ret = sceUtilityLoadNetModule(PSP_NET_MODULE_INET)) < 0) return ret;

    if ((ret = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024)) < 0) return ret;
    if ((ret = sceNetInetInit()) < 0) return ret;
    if ((ret = sceNetResolverInit()) < 0) return ret;
    if ((ret = sceNetApctlInit(0x8000, 48)) < 0) return ret;

    if ((ret = sceNetApctlConnect(profile)) < 0) return ret;

    int last_state = -1;
    for (int waited = 0; waited < timeout_ms; waited += 50) {
        int state;
        if ((ret = sceNetApctlGetState(&state)) < 0) return ret;
        if (state == PSP_NET_APCTL_STATE_GOT_IP) {
            union SceNetApctlInfo info;
            if (ip_out && sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &info) == 0) {
                strncpy(ip_out, info.ip, ip_len - 1);
                ip_out[ip_len - 1] = '\0';
            }
            return 0;
        }
        // Spadek z powrotem do "rozłączony" po rozpoczęciu łączenia = błąd
        if (last_state > PSP_NET_APCTL_STATE_DISCONNECTED &&
            state == PSP_NET_APCTL_STATE_DISCONNECTED) {
            return -2;
        }
        last_state = state;
        sceKernelDelayThread(50 * 1000);
    }
    return -1;  // timeout
}

void net_shutdown(void) {
    sceNetApctlDisconnect();
    sceNetApctlTerm();
    sceNetResolverTerm();
    sceNetInetTerm();
    sceNetTerm();
    sceUtilityUnloadNetModule(PSP_NET_MODULE_INET);
    sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
}
