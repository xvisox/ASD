// hm438596

#include <lib.h>
#include <minix/rs.h>
#include <string.h>

int get_pm_endpt(endpoint_t *pt) {
    return minix_rs_lookup("pm", pt);
}

int transfermoney(pid_t recipient, int amount) {
    endpoint_t pm_pt;
    message m;
    memset(&m, 0, sizeof(m));
    m.m_source = 2115;
    if (get_pm_endpt(&pm_pt) != 0) {
        errno = ENOSYS;
        return -1;
    }
    return (_syscall(pm_pt, PM_TRANSFER_MONEY, &m));
}