#include "fs.h"
#include "vnode.h"
#include "file.h"
#include "path.h"
#include <fcntl.h>
#include <stdbool.h>
// FIXME: remove this include
#include <stdio.h>

int check_exclusive(ino_t inode_nr, endpoint_t fs_e) {
    for (int i = 0; i < NR_EXCLUSIVE; i++) {
        struct exclusive *e = &exclusive_files[i];
        if (e->e_fs_e == fs_e && e->e_inode_nr == inode_nr) {
            if (e->e_uid != fp->fp_realuid) {
                return (EACCES);
            } else {
                return (OK);
            }
        }
    }
    return (OK);
}

int remove_exclusive(ino_t inode_nr, endpoint_t fs_e, int fd) {
    for (int i = 0; i < NR_EXCLUSIVE; i++) {
        struct exclusive *e = &exclusive_files[i];
        if (e->e_fs_e == fs_e && e->e_inode_nr == inode_nr && e->e_fd == fd) {
            e->e_inode_nr = e->e_fs_e = e->e_fd = e->e_uid = 0;
            return (OK);
        }
    }
    return (OK);
}

int do_lock(ino_t inode_nr, endpoint_t fs_e, int fd, bool no_others) {
    // Check if the file is opened by another user.
    for (int i = 0; i < NR_PROCS && no_others; i++) {
        struct fproc *proc = &fproc[i];
        if (proc->fp_pid == PID_FREE || proc->fp_realuid == fp->fp_realuid) continue;

        for (int j = 0; j < OPEN_MAX; j++) {
            struct filp *f = proc->fp_filp[j];
            if (f == NULL) continue;

            if (f->filp_vno->v_inode_nr == inode_nr && f->filp_vno->v_fs_e == fs_e) {
                return (EAGAIN);
            }
        }
    }
    // Check if the file is already locked.
    for (int i = 0; i < NR_EXCLUSIVE; i++) {
        struct exclusive *e = &exclusive_files[i];
        if (e->e_inode_nr == inode_nr && e->e_fs_e == fs_e) {
            return (EALREADY);
        }
    }
    // Find a free slot.
    for (int i = 0; i < NR_EXCLUSIVE; i++) {
        struct exclusive *e = &exclusive_files[i];
        if (e->e_inode_nr == 0) {
            e->e_inode_nr = inode_nr;
            e->e_fs_e = fs_e;
            e->e_fd = fd;
            e->e_uid = fp->fp_realuid;
            return (OK);
        }
    }
    return (ENOLCK);
}

int do_unlock(ino_t inode_nr, endpoint_t fs_e, uid_t owner_uid, bool force) {
    uid_t caller_uid = fp->fp_realuid;
    // Find the lock.
    for (int i = 0; i < NR_EXCLUSIVE; i++) {
        struct exclusive *e = &exclusive_files[i];
        if (e->e_inode_nr == inode_nr && e->e_fs_e == fs_e) {
            if (e->e_uid == caller_uid || (force && (SU_UID == caller_uid || owner_uid == caller_uid))) {
                e->e_inode_nr = e->e_fs_e = e->e_fd = e->e_uid = 0;
                return (OK);
            } else {
                return (EPERM);
            }
        }
    }
    return (EINVAL);
}

int do_fexclusive(void) {
    int fd = job_m_in.m_lc_vfs_exclusive.fd;
    int flags = job_m_in.m_lc_vfs_exclusive.flags;

    struct filp *f = get_filp(fd, VNODE_NONE);
    if (f == NULL || (f->filp_mode != R_BIT && f->filp_mode != W_BIT)) {
        return (EBADF);
    }
    struct vnode *v = f->filp_vno;
    if (!S_ISREG(v->v_mode)) {
        return (EFTYPE);
    }

    switch (flags) {
        case EXCL_LOCK:
            return do_lock(v->v_inode_nr, v->v_fs_e, fd, false);
        case EXCL_LOCK_NO_OTHERS:
            return do_lock(v->v_inode_nr, v->v_fs_e, fd, true);
        case EXCL_UNLOCK:
            return do_unlock(v->v_inode_nr, v->v_fs_e, v->v_uid, false);
        case EXCL_UNLOCK_FORCE:
            return do_unlock(v->v_inode_nr, v->v_fs_e, v->v_uid, true);
    }

    return (EINVAL);
}

int do_exclusive(void) {
    vir_bytes name = job_m_in.m_lc_vfs_exclusive.name;
    size_t len = job_m_in.m_lc_vfs_exclusive.len;
    int flags = job_m_in.m_lc_vfs_exclusive.flags;
    int fd = -1;

    char fullpath[PATH_MAX];
    struct lookup resolve;
    struct vmnt *vmp;
    struct vnode *v;

    lookup_init(&resolve, fullpath, 0, &vmp, &v);
    resolve.l_vmnt_lock = VMNT_READ;
    resolve.l_vnode_lock = VNODE_READ;
    if (fetch_name(name, len, fullpath) != OK) return (err_code);
    if ((v = eat_path(&resolve, fp)) == NULL) return (err_code);

    unlock_vnode(v);
    if (vmp) unlock_vmnt(vmp);
    put_vnode(v);

    if (forbidden(fp, v, R_BIT) == EACCES &&
        forbidden(fp, v, W_BIT) == EACCES) {
        return (EACCES);
    }
    if (!S_ISREG(v->v_mode)) {
        return (EFTYPE);
    }

    switch (flags) {
        case EXCL_LOCK:
            return do_lock(v->v_inode_nr, v->v_fs_e, fd, false);
        case EXCL_LOCK_NO_OTHERS:
            return do_lock(v->v_inode_nr, v->v_fs_e, fd, true);
        case EXCL_UNLOCK:
            return do_unlock(v->v_inode_nr, v->v_fs_e, v->v_uid, false);
        case EXCL_UNLOCK_FORCE:
            return do_unlock(v->v_inode_nr, v->v_fs_e, v->v_uid, true);
    }

    return (EINVAL);
}
