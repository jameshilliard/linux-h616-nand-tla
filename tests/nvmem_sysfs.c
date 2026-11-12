/* SPDX-License-Identifier: GPL-2.0-or-later */
static void reset_allocations(void)
{
    for (unsigned int i = 0; i < 64; i++)
        if (allocations[i])
            kfree(allocations[i]);
    assert(!live);
    attempts = fail_at = 0;
    fail_group = false;
}

static void init(struct nvmem_device *n, struct nvmem_cell_entry cells[2])
{
    memset(n, 0, sizeof(*n));
    n->dev.published = true;
    n->cells.next = n->cells.prev = &n->cells;
    cells[0] = (struct nvmem_cell_entry){ .refcnt = 1, .name = "provider", .bytes = 1 };
    cells[1] = (struct nvmem_cell_entry){ .refcnt = 1, .name = "layout", .bytes = 2 };
    append(&n->cells, &cells[0].node);
}

int main(void)
{
    struct nvmem_device n;
    struct nvmem_cell_entry cells[2];
    unsigned int missed = 0;
    int ret;

    init(&n, cells);
    assert(!nvmem_populate_sysfs_cells(&n));
    append(&n.cells, &cells[1].node);
    assert(!nvmem_populate_sysfs_cells(&n));
    if (!n.cells_group.bin_attrs[1])
        missed++;
    else {
        assert(!EXPECT_BUGS);
        assert(cells[0].refcnt == 2 && cells[1].refcnt == 2);
    }
#if !EXPECT_BUGS
    nvmem_sysfs_remove_cells(&n);
    assert(!live && cells[0].refcnt == 1 && cells[1].refcnt == 1);
    n.dev.published = false;
#endif
    reset_allocations();

    /* Pointer array, each bin_attribute, each name, then sysfs publication. */
    for (unsigned int fault = 1; fault <= 6; fault++) {
        init(&n, cells);
        append(&n.cells, &cells[1].node);
        fail_at = fault;
        fail_group = fault == 6;
        ret = nvmem_populate_sysfs_cells(&n);
#if EXPECT_BUGS
        {
            if (ret && live)
                missed++;
        }
#else
        {
            assert(ret == -ENOMEM);
            assert(!live && !n.dev.group && !n.cells_group.bin_attrs);
            assert(cells[0].refcnt == 1 && cells[1].refcnt == 1);
            fail_at = 0;
            fail_group = false;
            assert(!nvmem_populate_sysfs_cells(&n));
            nvmem_sysfs_remove_cells(&n);
            assert(!live);
        }
#endif
        reset_allocations();
    }
    if (EXPECT_BUGS) {
        assert(missed >= 2);
        puts("NVMEM-SYSFS: reproduced missed layout attributes and rollback leaks");
    } else {
        assert(!missed);
        puts("NVMEM-SYSFS: refresh, all allocation failures, sysfs failure and retry PASS");
    }
    return 0;
}
