# Partition parser references and provider admission

## Source and scope

The retained-handle v5 parser used `get_mtd_device(child, -1)` to retain a
partition while parsing outside the partition mutex. That helper invokes the
master's operational `_get_device()` hook. A busy gluebi volume can reject the
open with `-EBUSY`, although a metadata-only parser needs no access to its
contents. The failure unwinds partition registration.

The local v6 correction uses `mtd_get_data()` and `mtd_put_data()` instead.
The reference still protects the parser and its post-parse registration check
against concurrent deletion. Ordinary opens continue to use provider admission.
This does not promise that a parser which actually reads the medium can ignore
an I/O error or operate without the provider's required access.

The exact `add_mtd_partitions()` bodies are pinned in
[`reference/partition-parser.json`](../reference/partition-parser.json):

| Snapshot | Kernel commit | Parser reference |
| --- | --- | --- |
| Sent lifetime v5 | `73d69252690262ee6f58f870e307c88023dd1fdf` | Operational open |
| Local lifetime v6 | `6a64bd25ab9cf14d598304d6785e42a4f94c76cc` | Metadata-only pin |

`parser_reference.py` verifies the hashes, identifies the actual get/put pair,
and checks that it brackets the unlocked parse and subsequent registration
recheck. An unknown or mismatched pair is an error, not an assumed safe policy.
The selected policy supplies the TLA+ `ParserPin` constant. These are structural
source-correspondence checks, not execution or a refinement proof of that C.

## Model

[`MtdParserReferences.tla`](../models/MtdParserReferences.tla) distinguishes:

- Metadata pins, operational handles and the registration reference.
- Child-to-parent metadata retention and operational handle retention of the
  device chain, with one or two nested partitions.
- Publication/pinning under the partition mutex, unlocked parsing, concurrent
  recursive deletion and final release of each object's storage.
- A busy provider during parsing, an explicitly rejected client open, recovery
  of the provider and a subsequent client open/close.

The provider stays busy until parsing has returned and the first client attempt
has completed. Thus successful metadata registration cannot rely on the busy
condition clearing first. Concurrent deletion is explored separately; removal
may legitimately stop child publication and prevent later opens.

The invariants require storage to survive every parser access, no child to be
published beneath a removed parent, successful metadata registration without
external removal, correct client-open admission, successful retry after recovery
when still registered, and balanced metadata/provider references. After parser
completion its pins must be gone. Positive cases also check eventual cleanup
under weak fairness of the finite modeled protocol.

There are 22 new cases: 12 metadata-pin configurations, two idle-provider
operational-pin controls, two v5 busy-provider counterexamples, and six mutations
(missing pin, early put, wrong put, missing put, bypassed client admission and
child creation beneath a removed parent). The idle-provider controls establish
that the old API retains storage when admission succeeds; the historical bug
is not a missing reference on that successful path.

The existing partition/cell-publication models remain separate. Their 11 cases
include sibling-preserving rollback, parser/delete races, NVMEM cell attributes,
notification pairing and ownership. Together the runner checks 33 expectations.

## Reproduction

```sh
python3 -m unittest discover -s tests -p test_parser_reference.py
python3 run_lifetime_publication.py --download
python3 run_lifetime_publication.py --kernel-tree ../linux \
  --revision submit/rawnand-retained-handle-lifetime-v6
python3 run_lifetime_publication.py --kernel-tree ../linux \
  --revision submit/rawnand-retained-handle-lifetime-v5 \
  --case parser-current-source
```

An optional kernel revision adds a source-policy case without changing the
kernel checkout. Its outcome is recorded explicitly: the v5 policy must produce
the named `RegistrationIndependent` counterexample; the v6 policy must pass.
The runner rejects parser/tool failures as substitutes for that counterexample.
The retained-lifetime GitHub workflow checks the pinned policies and full matrix.

## Validation on 2026-11-16

All 33 runner expectations matched: 16 positive configurations, two historical
regressions and 15 mutation counterexamples, including the existing publication
and cell tests. The optional current-source case passed for local lifetime v6
and reproduced `RegistrationIndependent` for sent v5. All positive model
actions were exercised, and the nine source-policy unit tests passed.

The complete retained-lifetime workflow also passed locally: 108 TLC
expectations, 14 unit tests and both pinned C sanitizer fixtures. The
[validation index](../VALIDATION.md) records the result directories and keeps
earlier results separate. No Linux patch or hardware configuration was changed.

## Independent evidence and limits

The existing real-kernel fixture makes a synthetic provider return `-EBUSY` from
its open hook. It requires nested metadata registration without calling that
hook, verifies an ordinary open is rejected, clears the busy condition and
opens both resulting partitions. The recorded v5 run fails; the corrected
kernel and Sunxi/lifetime-v6 integration pass. These QEMU results are separate
from the new TLA+ checks; no new physical-board result is implied.

Mutex, reference-count, device-to-data and child-to-parent retention contracts
are assumed from the inspected MTD implementations. The model abstracts final
device release as a reference contribution, rather than modeling sysfs, NVMEM,
module unloading, arbitrary parsers or actual UBI operation. Other models and
kernel fixtures cover parts of those interfaces. The historical v5 error
unwind is abstracted as unsuccessful registration; master-only fallback is
observed by the kernel fixture, not proved by this model. This is bounded
protocol verification, not proof of arbitrary C schedules or weak-memory
behavior. No NFC hardware/reset/ECC assumption is strengthened by this update.

Review used the local Sashiko general, locking, allocation, cleanup and full
false-positive guidance at `ffa8b3489407dd5cea6558fcffb6a4667131f1d3`, together
with the actual MTD acquire/release implementations. No dedicated MTD prompt
was available. This is a model/source audit, not a new whole-series review or
a Sashiko service result.
