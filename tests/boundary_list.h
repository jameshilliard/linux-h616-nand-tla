/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host list/lock primitives. Locks assert ownership; they do not simulate SMP. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD(n) struct list_head n = { &(n), &(n) }
static void INIT_LIST_HEAD(struct list_head *h) { h->next=h->prev=h; }
static bool list_empty(const struct list_head *h) { return h->next==h; }
static void list_add_tail(struct list_head *n, struct list_head *h)
{ n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n; }
static void list_add(struct list_head *n, struct list_head *h)
{ list_add_tail(n,h->next); }
static void list_del(struct list_head *n)
{ n->prev->next=n->next; n->next->prev=n->prev; n->next=n->prev=NULL; }
static void list_del_init(struct list_head *n) { list_del(n); INIT_LIST_HEAD(n); }
static void list_move_tail(struct list_head *n, struct list_head *h)
{ list_del(n); list_add_tail(n,h); }
static void list_splice_tail_init(struct list_head *a, struct list_head *b)
{
    if (list_empty(a)) return;
    b->prev->next=a->next; a->next->prev=b->prev;
    a->prev->next=b; b->prev=a->prev; INIT_LIST_HEAD(a);
}
static void list_splice_init(struct list_head *a, struct list_head *b)
{
    if (list_empty(a)) return;
    a->prev->next=b->next; b->next->prev=a->prev;
    b->next=a->next; a->next->prev=b; INIT_LIST_HEAD(a);
}
#define list_entry(p,t,m) container_of(p,t,m)
#define list_first_entry(h,t,m) list_entry((h)->next,t,m)
#define list_first_entry_or_null(h,t,m) (list_empty(h)?NULL:list_first_entry(h,t,m))
#define list_for_each_entry(p,h,m) \
    for (struct list_head *cursor_=(h)->next; \
         cursor_!=(h) && ((p)=list_entry(cursor_,__typeof__(*(p)),m),1); cursor_=cursor_->next)
#define list_for_each_entry_safe(p,n,h,m) \
    for (struct list_head *cursor_=(h)->next, *next_=cursor_->next; \
         cursor_!=(h) && ((p)=list_entry(cursor_,__typeof__(*(p)),m), \
         (n)=next_==(h)?NULL:list_entry(next_,__typeof__(*(n)),m),1); \
         cursor_=next_, next_=cursor_->next)
struct lock { bool held; };
typedef struct lock spinlock_t;
static void spin_lock(struct lock *l) { assert(!l->held); l->held=true; }
static void spin_unlock(struct lock *l) { assert(l->held); l->held=false; }
#define spin_lock_irq(l) spin_lock(l)
#define spin_unlock_irq(l) spin_unlock(l)
#define spin_lock_init(l) ((l)->held=false)
#define spin_lock_irqsave(l,f) do { (f)=0; spin_lock(l); } while (0)
#define spin_unlock_irqrestore(l,f) do { (void)(f); spin_unlock(l); } while (0)
#define lockdep_assert_held(l) assert((l)->held)
#define mutex_lock(l) spin_lock(l)
#define mutex_unlock(l) spin_unlock(l)
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define pr_info(...) ((void)0)
#define pr_err(...) ((void)0)
#define dev_dbg(...) ((void)0)
#define dev_vdbg(...) ((void)0)
