/* SPDX-License-Identifier: GPL-2.0-or-later */
static bool driver_live = true;
static int query_error, query_failure_at, queries;
static int driver_layout(struct mtd_info *mtd, int section, struct mtd_oob_region *region)
{
    assert(driver_live);
    if (++queries == query_failure_at) return -EIO;
    if (query_error) return query_error;
    if (section >= 2) return -ERANGE;
    region->offset = section * 8; region->length = 4; return 0;
}
static const struct mtd_ooblayout_ops driver_ops = { .ecc = driver_layout, .free = driver_layout };
static struct device parent;
static struct device_node node;
struct controller { struct nand_controller base; struct nand_chip embedded; };

static void init_chip(struct nand_chip *chip, struct nand_controller *controller)
{
    chip->controller = controller;
    chip->mtd.parent_dev = &parent; chip->mtd.node = &node;
    chip->mtd.name = "driver-owned name";
    chip->mtd.ooblayout = &driver_ops;
}
static void verify(struct nand_chip *chip, bool supported)
{
    struct mtd_oob_region region;
    assert(chip->mtd.owner == THIS_MODULE);
    assert(!chip->mtd._get_device(&chip->mtd));
    chip->disconnected = true;
    assert(chip->mtd._get_device(&chip->mtd) == -ENODEV);
    driver_live = false;
    for (int i = 0; i < 2; i++) {
        int ret = mtd_ooblayout_ecc(&chip->mtd, i, &region);
        assert(supported ? ret == 0 && region.offset == (unsigned)i * 8 && region.length == 4 : ret == -ENOTSUPP);
        ret = mtd_ooblayout_free(&chip->mtd, i, &region);
        assert(supported ? ret == 0 && region.offset == (unsigned)i * 8 && region.length == 4 : ret == -ENOTSUPP);
    }
    assert(mtd_ooblayout_ecc(&chip->mtd, 2, &region) == (supported ? -ERANGE : -ENOTSUPP));
    assert(mtd_ooblayout_free(&chip->mtd, 2, &region) == (supported ? -ERANGE : -ENOTSUPP));
    chip->mtd.release(&chip->mtd);
    driver_live = true;
}
int main(void)
{
    /* Managed controller plus separate chips: final puts in either order. */
    for (int order = 0; order < 2; order++) {
        struct controller *ctrl = allocate(sizeof(*ctrl));
        struct nand_chip *chip = allocate(sizeof(*chip));
        assert(!devm_nand_controller_init(&parent, &ctrl->base, ctrl));
        init_chip(&ctrl->embedded, &ctrl->base); init_chip(chip, &ctrl->base);
        assert(!nand_set_lifetime(&ctrl->embedded, NULL));
        assert(!nand_set_lifetime(chip, chip));
        assert(ctrl->base.storage.refcnt.count == 3);
        devres_release(&parent);
        if (order) { verify(&ctrl->embedded, true); verify(chip, true); }
        else { verify(chip, true); verify(&ctrl->embedded, true); }
        assert(!allocations && !parent.refs && !node.refs);
    }
    /* Standalone chips and allocation failures leave caller ownership intact. */
    for (int fail = 0; fail <= 2; fail++) {
        struct nand_controller controller = {0};
        struct nand_chip *chip = allocate(sizeof(*chip));
        init_chip(chip, &controller); chip->mtd.name = NULL; chip->mtd.ooblayout = NULL;
        allocation_index = 0; allocation_fail = fail;
        int ret = nand_set_lifetime(chip, chip);
        allocation_fail = 0;
        if (fail) {
            assert(ret == -ENOMEM && !chip->lifetime && !chip->mtd.name);
            assert(!chip->mtd.release && !parent.refs && !node.refs);
            kfree(chip);
        } else {
            assert(!ret && !strcmp(chip->mtd.name, "test-controller")); verify(chip, false);
        }
        assert(!allocations);
    }
    /* Layout errors on first enumeration and either snapshot-copy pass. */
    for (int at = 1; at <= 10; at++) {
        struct nand_controller controller = {0};
        struct nand_chip *chip = allocate(sizeof(*chip)); init_chip(chip, &controller);
        queries = 0; query_failure_at = at;
        assert(nand_set_lifetime(chip, chip) == -EIO);
        query_failure_at = 0;
        assert(!chip->lifetime && chip->mtd.ooblayout == &driver_ops && !parent.refs && !node.refs);
        kfree(chip); assert(!allocations);
    }
    /* OMAP-style embedded chip whose static controller isn't retained. */
    struct controller *ctrl = allocate(sizeof(*ctrl));
    struct nand_controller shared = {0};
    assert(!devm_nand_storage_init(&parent, &ctrl->base.storage, ctrl));
    init_chip(&ctrl->embedded, &shared);
    assert(!nand_set_shared_lifetime(&ctrl->embedded, &ctrl->base.storage));
    devres_release(&parent); verify(&ctrl->embedded, true);
    assert(!allocations && !parent.refs && !node.refs);
    /* add_action_or_reset failure consumes the allocation once. */
    action_fail = true;
    ctrl = allocate(sizeof(*ctrl));
    assert(devm_nand_controller_init(&parent, &ctrl->base, ctrl) == -ENOMEM);
    assert(!allocations && !parent.action);
    puts("ALL CHECKS PASSED");
    return 0;
}
