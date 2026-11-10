obj-m += mtd_detach_holder.o mtd_detach_provider.o raw_nand_detach_test.o
obj-m += virt_concat_detach.o
obj-m += mtd_pm_detach_test.o
virt_concat_detach-y := virt_concat_detach_test.o concat_fixture.o

targets += concat_fixture.dtb concat_fixture.c
$(obj)/concat_fixture.dtb: $(src)/concat_fixture.dts FORCE
	$(objtree)/scripts/dtc/dtc -I dts -O dtb -o $@ $<
$(obj)/concat_fixture.c: $(obj)/concat_fixture.dtb
	xxd -i -n concat_fixture_dtb $< > $@
