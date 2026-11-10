# Device callback code lifetime

The retained-object models did not distinguish the final statement of a
module callback from its return to the built-in caller. A callback that
drops its own last module reference can still have an epilogue to execute.
Concurrent unload can therefore invalidate its code before it returns.
Tail-call optimization in one build does not establish the required ordering.

The candidate adds an opt-in `struct device::release_owner` reference.
`device_initialize()` acquires it before publication. Built-in
`device_release()` caches the owner, invokes the selected release callback,
then drops the reference after return. The callback may free the device.
MTD supplies its owner before `device_register()` and does not drop that
reference inside `mtd_release()`.

## Model and source correspondence

`DeviceReleaseOwner.tla` separates initialization, registration outcome,
external reference handoff, delayed dispatch, callback body, epilogue,
return, core put and module unload. It permits the callback to free its
device before returning. Six positive cases cover one or two devices and
initialized-only, registered, or failed-add outcomes. They check pending
callback code, executing callback code, cached-owner access, reference
balance and eventual completion.

Three negative controls independently drop the reference inside the callback,
acquire it only after successful registration, or access the freed device
instead of caching the owner. Each must produce its specific invariant
counterexample. All modeled actions must be reached by positive cases.

`run_release_owner.py` also checks the corresponding ordering in the actual
device-core and MTD function bodies. `reference/device-release-owner.json`
pins those bodies and their hashes. This is a structural correspondence
check, not a machine-checked refinement proof of kernel C.

```sh
python3 run_release_owner.py --download
python3 run_release_owner.py --kernel-tree ../linux
```

The constructing caller keeps the callback module alive until initialization
acquires the reference. The owner remains unchanged afterwards. Forced
module removal is excluded. Failed module initialization must drain its
devices before returning: module references cannot prevent that unload.
The model treats the device-core release dispatcher as built-in, matching
the kernel, and does not claim to prove arbitrary callback bodies.

## Kernel regression fixture

The external modules in `tests/kernel/retained_lifetime` provide an owner
and a separate holder. The holder has no symbol dependency on the owner;
the owner instead depends on the holder's synchronization helpers.

The owner initializes a device with its release owner, optionally adds it
or deliberately exercises a failed `device_add()`, and hands its remaining
reference to the holder. The holder drops that reference in a worker.
Completions hold the release callback open while userspace attempts an
ordinary module unload. Removal must fail both before dispatch and during
the callback, then succeed after the callback returns. The callback checks
that its reference remains positive before and after the gate and frees
its device. The gate has a timeout so fixture failures do not hang the VM.

Build the fixtures with `CONFIG_DEVICE_RELEASE_OWNER_TEST=m`. The common
initramfs `init` runs these checks when their modules are supplied; it can
also run them alone without the MTD fixtures. Mode 0 tests initialized-only
devices, mode 1 registered devices, and mode 2 registration failure.
These are synthetic kernel tests; no flash hardware is accessed.

## Recorded validation

The pinned candidate passed all six positive model cases and all three
negative controls. The existing retained-object and concatenation models
also passed their 23 and five expectations. The extracted C harness passed
129 schedules and 242 successful file open/close pairs with ASan/UBSan.

The three kernel fixture modes passed on x86-64 QEMU with KASAN and lockdep,
both with the ORC unwinder and with frame pointers. They also passed with
frame pointers and `CONFIG_DEBUG_KOBJECT_RELEASE=y`, which exercised delayed
dispatch before attempting callback-owner unload. These results cover the
callback-lifetime contract, not every driver or every module configuration.
