# OTP provider backing-object lifetime

## Finding and ownership change

MTD's user and factory OTP NVMEM providers are children of the physical
device, not of the MTD device. A reference to their parent therefore does
not retain the separately allocated MTD master. The initial retained-handle
candidate covered ordinary MTD and partition-device references, but not this
ownership path. A synthetic OTP consumer reproduced a KASAN use-after-free
when reading after unregister and the driver's final put.

The corrected candidate gives each OTP provider an explicit reference to
the managed master's backing object. Optional paired NVMEM context callbacks
acquire that reference before publication and release it at final NVMEM
device release, not at unregister. The MTD core module is retained for the
callback, and OTP consumers use the backing driver's module as their owner.
The physical-device parent and existing OTP names remain unchanged.

Error-path testing also identified a separate double-unregister bug. When
factory OTP discovery fails after user OTP is published, local cleanup drops
the user provider and the outer MTD error path drops it again. A suppressed
`-EOPNOTSUPP` can instead leave the stale pointer for later device removal.
The preparatory fix clears provider pointers after unregistering them.

## Executable checks

Run the source-extracted fixture and bounded models with:

```sh
python3 run_retained_lifetime.py
python3 run_retained_model.py --download
```

The exact candidate identity and function digests are in
[`reference/retained-lifetime.json`](../reference/retained-lifetime.json).
The host fixture executes the actual MTD OTP get, put and removal helpers
with explicit NVMEM-provider shims. It checks both provider release orders,
with and without a registered MTD node, alongside the existing retained
handle tests. The expanded run passes **129 schedules** and **242 successful
file opens/closes** under ASan/UBSan.

`MtdOtpLifetime` distinguishes the driver's initial reference, the MTD node,
OTP registration, consumer references, extra NVMEM device references, and
backing-object release. Four positive instances cover one or two OTP
providers with successful MTD registration or registration unwind. Three
negative controls violate the expected storage or single-unregister
invariant. With the existing models, **23/23 expectations** match: eleven
positive instances and twelve expected counterexamples.

The real-kernel module is
[`tests/kernel/retained_lifetime/otp_lifetime_test.c`](../tests/kernel/retained_lifetime/otp_lifetime_test.c).
It uses a synthetic MTD backend and the actual MTD, NVMEM, device and module
reference implementations. It checks:

- Retained user and factory consumers across unbind, in both release orders.
- Reads before removal and `-ENODEV` after disconnection.
- Replacement registration while the old provider remains referenced.
- Consumer ownership of the provider module.
- A bare NVMEM device reference outliving unregister.
- An OTP consumer acquired from the registration notifier before factory
  discovery fails and the whole MTD registration is unwound.
- Factory discovery errors, suppressed unsupported OTP, and an MTD
  registration rejection after both OTP providers have registered.
- Rejection of an incomplete context-callback pair before acquisition and
  balanced acquisition/release on a later NVMEM registration error.

KASAN/lockdep QEMU runs with both built-in and modular MTD report nine backing
releases. The existing MTD and raw-NAND tests pass as well, with six backing
releases and four chips/two controllers respectively. The modular run unloads
all three test modules, then `nand`, `nandcore` and `mtd`, checking that the
new context references do not leave module pins behind. Expected injected
registration errors are distinct from KASAN, lockdep or test failures.

## Limits

The OTP model is an ownership abstraction, not a simulation of NVMEM cells,
sysfs internals, module unload, CPU memory ordering or manufacturer commands.
It starts with consumers/device references already held; the failed-probe
acquisition path is exercised by the kernel test rather than modeled as a
concurrent registration protocol. The host fixture does not substitute for
the actual NVMEM core; that is why the kernel module is included.

No physical flash or OTP was read or programmed. These tests do not prove all
52 converted raw-NAND drivers' hardware teardown paths, nor extend managed
backing ownership to MTD technologies that have not opted into it.
