obj-m += retained_lifetime_test.o
obj-m += raw_nand_lifetime_test.o
obj-m += otp_lifetime_test.o
obj-m += h616_nvmem_hold.o
obj-$(CONFIG_RETAINED_REVIEW_TEST) += review_lifetime_test.o
obj-$(CONFIG_RETAINED_CONCAT_TEST) += virt_concat_lifetime.o
obj-$(CONFIG_DEVICE_RELEASE_OWNER_TEST) += device_release_holder.o device_release_owner.o
virt_concat_lifetime-y := virt_concat_lifetime_test.o concat_fixture.o

targets += concat_fixture.dtb concat_fixture.c
$(obj)/concat_fixture.dtb: $(src)/concat_fixture.dts FORCE
	$(objtree)/scripts/dtc/dtc -I dts -O dtb -o $@ $<
$(obj)/concat_fixture.c: $(obj)/concat_fixture.dtb
	xxd -i -n concat_fixture_dtb $< > $@
