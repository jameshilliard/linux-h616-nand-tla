/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deliberately sequential: kernel locks/SRCU are exercised separately in QEMU. */
#define guard(kind) int __attribute__((unused)) held = fake_lock
#define scoped_guard(kind, ...) for (int once = 1; once; once = 0)
static int fake_lock(void *p) { return 0; }
#define lockdep_assert_held(p) ((void)(p))
#define GFP_KERNEL 0
#define sysfs_bin_attr_init(p) ((void)(p))
static int nvmem_mutex;

struct list_head { struct list_head *next, *prev; };
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define list_for_each_entry(p, head, member) \
    for (p = container_of((head)->next, __typeof__(*p), member); \
         &p->member != (head); \
         p = container_of(p->member.next, __typeof__(*p), member))
static bool list_empty(struct list_head *h) { return h->next == h; }
static unsigned int list_count_nodes(struct list_head *h)
{
    struct list_head *p;
    unsigned int count = 0;
    for (p = h->next; p != h; p = p->next)
        count++;
    return count;
}
static void append(struct list_head *h, struct list_head *p)
{
    p->prev = h->prev;
    p->next = h;
    h->prev->next = p;
    h->prev = p;
}
struct nvmem_cell_entry {
    int refcnt;
    const char *name;
    unsigned int offset, bit_offset, bytes;
    struct list_head node;
};
struct attribute { const char *name; unsigned int mode; };
struct bin_attribute {
    struct attribute attr;
    size_t size;
    int (*read)(void);
    void *private;
};
struct attribute_group {
    const char *name;
    const struct bin_attribute *const *bin_attrs;
};
struct device { bool published; const struct attribute_group *group; };
struct nvmem_device {
    struct device dev;
    int sysfs_mutex;
    bool disconnected, sysfs_cells_populated;
    struct list_head cells;
    struct attribute_group cells_group;
};
static int nvmem_cell_attr_read(void) { return 0; }
static unsigned int nvmem_bin_attr_get_umode(struct nvmem_device *n) { return 0644; }
static void kref_get(int *ref) { assert(*ref > 0); ++*ref; }
static void nvmem_cell_entry_put(struct nvmem_cell_entry *e) { assert(e->refcnt > 1); --e->refcnt; }

static void *allocations[64];
static unsigned int attempts, fail_at, live;
static bool fail_group;
static void *allocate(size_t count, size_t size)
{
    void *p;
    if (++attempts == fail_at)
        return NULL;
    p = calloc(count, size);
    assert(p);
    for (unsigned int i = 0; i < 64; i++)
        if (!allocations[i]) {
            allocations[i] = p;
            live++;
            return p;
        }
    abort();
}
static void kfree(const void *p)
{
    if (!p)
        return;
    for (unsigned int i = 0; i < 64; i++)
        if (allocations[i] == p) {
            allocations[i] = NULL;
            live--;
            free((void *)p);
            return;
        }
    abort();
}
#define kcalloc(n, s, flags) allocate(n, s)
#define kzalloc_obj(obj) allocate(1, sizeof(obj))
#define devm_kcalloc(dev, n, s, flags) allocate(n, s)
static char *kasprintf(int flags, const char *fmt, const char *name, int off, int bit)
{
    char *s = allocate(1, 128);
    if (s)
        snprintf(s, 128, fmt, name, off, bit);
    return s;
}
#define devm_kasprintf(dev, ...) kasprintf(__VA_ARGS__)
static int device_add_group(struct device *dev, const struct attribute_group *group)
{
    assert(dev->published && !dev->group);
    if (fail_group)
        return -ENOMEM; /* sysfs rolls partial publication back before returning. */
    dev->group = group;
    return 0;
}
static void device_remove_group(struct device *dev, const struct attribute_group *group)
{
    assert(dev->published && dev->group == group);
    for (unsigned int i = 0; group->bin_attrs[i]; i++) {
        const struct bin_attribute *attr = group->bin_attrs[i];
        assert(attr->attr.name && attr->size && attr->private);
    }
    dev->group = NULL; /* kernfs drains readers before it returns. */
}
