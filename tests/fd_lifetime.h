/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Storage remains allocated for safe lifetime observations. A recorded access
 * after cleanup is a source contract witness, not an actual host UAF. */
#define FMODE_WRITE 2
#define MTD_ABSENT 0
#define MTD_WRITEABLE 0x400
#define IS_ERR(p) ((intptr_t)(p)<0 && (intptr_t)(p)>=-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define pr_debug(...) ((void)0)
#define mtd_to_nand(m) container_of(m,struct nand_chip,mtd)
struct inode { int minor; };
struct file { unsigned f_mode; void *private_data; };
struct mtd_file_info { struct mtd_info *mtd; int mode; };
#define iminor(i) ((i)->minor)
static bool fd_alloc_fail;
#define kzalloc_obj(x) (fd_alloc_fail ? NULL : calloc(1,sizeof(x)))
#define kfree(p) free(p)
/* Only running chips are used in these fd schedules. Suspend/resume is an
 * explicit separate NAND-core contract, not invented sunxi PM callbacks. */
#define wait_event(q,cond) assert(cond)
static unsigned dead_lock_accesses, fd_opens, fd_closes;
static struct lock *dead_chip_lock, *dead_controller_lock;
static void fd_mutex_lock(struct lock *lock)
{
    if(lock==dead_chip_lock || lock==dead_controller_lock) dead_lock_accesses++;
    spin_lock(lock);
}
#undef mutex_lock
#define mutex_lock(l) fd_mutex_lock(l)
