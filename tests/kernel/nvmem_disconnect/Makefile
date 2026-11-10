obj-m += nvmem_detach_holder.o nvmem_detach_provider.o
nvmem_detach_provider-y := provider.o layout_fixture.o

targets += layout_fixture.dtb layout_fixture.c
$(obj)/layout_fixture.dtb: $(src)/layout_fixture.dts FORCE
	$(objtree)/scripts/dtc/dtc -I dts -O dtb -o $@ $<
$(obj)/layout_fixture.c: $(obj)/layout_fixture.dtb
	xxd -i -n layout_fixture_dtb $< > $@
