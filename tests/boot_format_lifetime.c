/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <setjmp.h>

struct mtd_info {
    const char *name;
    unsigned refs;
    bool registered, boot;
};
struct nand_chip { struct mtd_info mtd; bool disconnected; };
struct sunxi_nand_boot {
    struct list_head node;
    struct mtd_info mtd;
    struct lock lock;
    struct nand_chip *nand;
    void *buf;
};
struct sunxi_nand_chip {
    struct list_head node, boot_views;
    struct nand_chip nand;
    bool registered;
};
struct sunxi_nfc { struct list_head chips; };
static struct mtd_info *nand_to_mtd(struct nand_chip *chip) { return &chip->mtd; }
#define kfree(p) free((void *)(p))
#define WARN_ON(c) assert(!(c))
static jmp_buf blocked;
static unsigned unregistered, disconnected, freed_views, freed_chips, woken;

static struct lock *acquire(struct lock *lock)
{
    if (lock->held)
        longjmp(blocked, 1);
    lock->held = true;
    return lock;
}
static void release(struct lock **lock)
{
    if (*lock) {
        assert((*lock)->held);
        (*lock)->held = false;
    }
}
#define guard(type) struct lock *held_ __attribute__((cleanup(release))) = acquire
#define scoped_guard(type, ptr) \
    for (struct lock *held_ __attribute__((cleanup(release))) = acquire(ptr); \
         held_; release(&held_), held_ = NULL)

static int mtd_device_unregister(struct mtd_info *mtd)
{
    assert(mtd->registered && mtd->refs >= 2);
    mtd->registered = false;
    mtd->refs--;
    unregistered++;
    return 0;
}

static void nand_disconnect(struct nand_chip *chip)
{
    struct sunxi_nand_chip *sn = container_of(chip, struct sunxi_nand_chip, nand);
    struct sunxi_nand_boot *boot;

    assert(!chip->mtd.registered && !chip->disconnected);
    chip->disconnected = true;
    disconnected++;
    list_for_each_entry(boot, &sn->boot_views, node) {
        if (boot->lock.held) {
            boot->lock.held = false;
            woken++;
        }
    }
}

static void sunxi_nand_boot_release(struct mtd_info *mtd);
static void mtd_put_master(struct mtd_info *mtd)
{
    assert(mtd->refs && !mtd->registered);
    if (--mtd->refs)
        return;
    if (mtd->boot) {
        struct sunxi_nand_boot *boot = container_of(mtd, struct sunxi_nand_boot, mtd);
        assert(!boot->nand && !boot->lock.held && !boot->node.next);
        freed_views++;
        sunxi_nand_boot_release(mtd);
    } else {
        struct nand_chip *nand = container_of(mtd, struct nand_chip, mtd);
        struct sunxi_nand_chip *sn = container_of(nand, struct sunxi_nand_chip, nand);
        assert(nand->disconnected && list_empty(&sn->boot_views));
        assert(!sn->node.next);
        freed_chips++;
        free(sn);
    }
}

/* @driver@ */

static bool cleanup_stalled(struct sunxi_nfc *nfc)
{
    if (setjmp(blocked))
        return true;
    sunxi_nand_chips_cleanup(nfc);
    return false;
}

static struct sunxi_nfc nfc;
static struct sunxi_nand_boot *views[4];
static struct sunxi_nand_chip *chips[2];

int main(void)
{
    unsigned cases = 0;
    for (unsigned count = 1; count <= 2; count++)
    for (unsigned nviews = 1; nviews <= 2; nviews++)
    for (unsigned registered = 0; registered <= 1; registered++)
    for (unsigned refs = 0; refs <= 2; refs++)
    for (unsigned waiting = 0; waiting <= 1; waiting++) {
        if (EXPECT_DEADLOCK && !waiting)
            continue;
        INIT_LIST_HEAD(&nfc.chips);
        unregistered = disconnected = freed_views = freed_chips = woken = 0;
        for (unsigned i = 0; i < count; i++) {
            struct sunxi_nand_chip *sn = calloc(1, sizeof(*sn));
            assert(sn);
            chips[i] = sn;
            INIT_LIST_HEAD(&sn->boot_views);
            list_add_tail(&sn->node, &nfc.chips);
            sn->registered = registered;
            sn->nand.mtd.registered = registered;
            sn->nand.mtd.refs = 1 + registered;
            for (unsigned j = 0; j < nviews; j++) {
                struct sunxi_nand_boot *boot = calloc(1, sizeof(*boot));
                assert(boot);
                views[2 * i + j] = boot;
                boot->mtd = (struct mtd_info){.name = strdup("boot"),
                    .refs = 2 + refs, .registered = true, .boot = true};
                boot->buf = malloc(2048 + 128);
                assert(boot->mtd.name && boot->buf);
                boot->nand = &sn->nand;
                boot->lock.held = waiting;
                list_add_tail(&boot->node, &sn->boot_views);
            }
        }
        if (cleanup_stalled(&nfc)) {
            assert(EXPECT_DEADLOCK && !disconnected && !freed_views && !freed_chips);
            for (unsigned i = 0; i < count; i++) {
                for (unsigned j = 0; j < nviews; j++)
                    sunxi_nand_boot_release(&views[2 * i + j]->mtd);
                free(chips[i]);
            }
            puts("BOOT FORMAT DEADLOCK REPRODUCED");
            return 0;
        }
        assert(!EXPECT_DEADLOCK);
        assert(list_empty(&nfc.chips) && disconnected == count);
        assert(unregistered == count * (registered + nviews));
        assert(woken == count * nviews * waiting);
        assert(freed_chips == count);
        assert(freed_views == (refs ? 0 : count * nviews));
        if (refs) {
            for (unsigned i = 0; i < count; i++)
                for (unsigned j = 0; j < nviews; j++) {
                    struct sunxi_nand_boot *boot = views[2 * i + j];
                    assert(!boot->nand);
                    assert(sunxi_nand_boot_isbad(&boot->mtd, 0) == -ENODEV);
                    for (unsigned r = 0; r < refs; r++)
                        mtd_put_master(&boot->mtd);
                }
        }
        assert(freed_views == count * nviews);
        cases++;
    }
    printf("%u bounded cleanup/waiter/ref schedules passed\n", cases);
    puts("BOOT FORMAT LIFETIME CHECKS PASS");
    return 0;
}
