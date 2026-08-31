# Guest-program rotation checklist

Use this when `program/src/main.rs` (or anything that changes the guest ELF) is modified.
A guest change rotates `VK_PROGRAM_HASH`. Existing Groth16 proofs become unverifiable.
The recursive chain must restart from a new trust anchor and a non-recursive baseline proof.

The `_v6` suffix on period-store files names the **SP1 circuit** (Groth16 v6, 356-byte proof),
not the guest revision. Keep that suffix. Do not add a `_v7` union variant unless the
on-wire proof size changes. SSZ union index 2 (`ZKSyncData`, 260 bytes) stays as a dead
placeholder so index 3 (`ZKSyncDataV6`) does not shift.

## 1. Rebuild and freeze the guest ELF

```bash
cd src/chains/eth/zk_proof/program
cargo prove build
cp ../target/elf-compilation/riscv64im-succinct-zkvm-elf/release/eth-sync-program \
   elf/eth_sync_program
```

Commit `elf/eth_sync_program`. `scripts/run_zk_proof.sh` uses this frozen ELF and will not
rebuild the guest if the file exists. A rebuild on another toolchain pin changes the digest.

Pin the SP1 toolchain with `sp1up -v v6.3.0` (directory name `aqFpu2ZKYP` at the time of writing).

## 2. Rotate `VK_PROGRAM_HASH` only

The C verifier binds the Groth16 proof to `VK_PROGRAM_HASH` (guest digest) plus the
circuit points (`VK_ROOT`, ALPHA, BETA, IC, …).

- Guest-only change: update **`VK_PROGRAM_HASH`** in
  `src/chains/eth/zk_verifier/zk_verifier_constants.h`.
- Leave ALPHA / BETA / GAMMA / DELTA / IC / `VK_ROOT` unchanged unless the SP1 Groth16
  circuit version itself changes.
- Do **not** import points from `~/.sp1/circuits/groth16/v5.0.0/` (that is the leftover
  v5 wrapper). The live circuit is SP1 v6.

`export_vk` can print the new digest. If it panics on `ALPHA_X` because it still points at
the v5 Solidity wrapper, take the printed `FFBn254Fr(0x…)` hash and patch `VK_PROGRAM_HASH`
by hand. Do not let it overwrite the v6 Groth16 points.

Rebuild `zk_verifier` and confirm old fixtures skip rather than fail
(`test/unittests/test_zk_proof.c`).

## 3. Pick the first proof period

`eth_proof_sync` for period `P` fetches LCU(`P-2`) and LCU(`P-1`):

| Field in `sync.ssz` | Source | Committee |
|---|---|---|
| `oldKeys` | LCU(`P-2`).`nextSyncCommittee.pubkeys` | period `P-1` |
| `newKeys` | LCU(`P-1`).`nextSyncCommittee.pubkeys` | period `P` |

The latest period that already has `sync.ssz` is the first proof you can build today.
The next period needs tomorrow's LCU.

Do **not** recurse from a proof produced by the previous guest. `run_zk_proof.sh` auto-detects
`../<P-1>/zk_proof.bin` (not `zk_proof_v6.bin`). If that file exists from an old run, remove
it or the host will feed a VK-mismatched previous proof into the guest.

## 4. Compute the trust anchor (pubkeys only)

The guest hashes **512 BLS pubkeys** (`create_root_hash`). That is *not* the Light Client
`SyncCommittee` container root (container = `hash(pubkeys_root, aggregatePubkey_root)`).

```bash
# Correct: pubkeys-only HTR of the first proof's current committee
./build/default/bin/ssz build/<chain>/<P>/sync.ssz proof oldKeys -h

# Equivalent: newKeys of the previous period
./build/default/bin/ssz build/<chain>/<P-1>/sync.ssz proof newKeys -h
```

**Wrong** (this was the 2026-08 rotation bug):

```bash
# Container HTR — includes aggregatePubkey. Must not be zk_sync_keys_root.
./build/default/bin/ssz build/<chain>/<P-2>/lcu.ssz -t lcu nextSyncCommittee -h
```

Cross-check against an *old* recursive `zk_pub_v6.bin` if one is still around:

- Bytes `32..63` (`next_keys_root`) must equal that period's `sync.ssz` `newKeys` HTR.
- Bytes `0..31` (`current_keys_root`) on a **recursive** proof are the *old chain* anchor,
  not this period's `oldKeys`. Only a non-recursive baseline has
  `current_keys_root == oldKeys` HTR.

Write the pubkeys-only root into `chain_spec_t.zk_sync_keys_root` in
`src/chains/eth/ssz/beacon_types.c` and pin it in
`test/unittests/test_eth_chain_spec.c` (`test_zk_sync_trust_anchors`).

## 5. Remove leftover artifacts on the first-proof period

Keep `sync.ssz` and witness `sig_*`. Delete previous-guest ZK files so the period store
cannot serve or recurse on them:

```bash
rm -f build/<chain>/<P>/zk_*.bin build/<chain>/<P>/zk_*.ssz
```

Older periods may still hold old-guest `_v6` files. The automatic prover verifies before
accepting a baseline; those files fail the new `VK_PROGRAM_HASH` and are skipped. The
newest valid `zk_proof_g16_v6.bin` + `zk_pub_v6.bin` wins.

## 6. Execute, then prove (no recursion)

```bash
# Fast guest simulation — must print "Valid transition to period <P>"
./scripts/run_zk_proof.sh --period <P> --execute --output build/<chain>

# Baseline Groth16 via the SP1 network (do not pass --prev-period)
export SP1_PRIVATE_KEY_FILE=/path/to/.proverkey   # gitignored
./scripts/run_zk_proof.sh --period <P> --prove --groth16 --network --output build/<chain>
```

`--execute` / `--prove` write `zk_pub.bin` (no `_v6` suffix). After a successful prove:

```
zk_pub.bin          current = oldKeys HTR, next = newKeys HTR, period = P
zk_proof_g16.bin    356-byte Groth16
zk_proof.bin        compressed (recursion input for P+1)
zk_vk_raw.bin       compressed VK (recursion input for P+1)
```

Copy onto the names the period-store prover actually looks for:

```bash
cd build/<chain>/<P>
cp -f zk_groth16.bin    zk_groth16_v6.bin
cp -f zk_proof_g16.bin  zk_proof_g16_v6.bin
cp -f zk_proof.bin      zk_proof_v6.bin
cp -f zk_pub.bin        zk_pub_v6.bin
cp -f zk_vk.bin         zk_vk_v6.bin
cp -f zk_vk_raw.bin     zk_vk_raw_v6.bin
```

## 7. Verify with the C verifier

```bash
./build/default/bin/verify_zk_proof_cli \
  build/<chain>/<P>/zk_proof_g16_v6.bin \
  build/<chain>/<P>/zk_pub_v6.bin
```

`current_keys_root` in `zk_pub` must equal `zk_sync_keys_root` in `beacon_types.c`.
The automatic prover (`c4_period_prover_on_checkpoint`) will then recurse `P+1`
once `sync.ssz` for that period exists (needs the next LCU).

## 8. Guest gindex reminder

`c4_next_sync_committee_gindex` returns the **container** leaf in `BeaconState`
(Deneb 55 / Electra-Fulu 87 / Gloas 2946). The guest and `proof_sync.c` both do

```text
expected_gidx = ssz_add_gindex(19, next_sync_committee_gindex) * 2
```

The `* 2` steps into `SyncCommittee.pubkeys` (left child). Gloas only moves the
committee inside the ProgressiveContainer; `SyncCommittee` itself stays
`{pubkeys, aggregatePubkey}`. Proof node count is `1 + branch + 4`
(1 aggregate helper, 5/6/11 LCU branch nodes, 4 header nodes) → depth 10 / 11 / 16.

Pin this in `test_zk_sync_proof_expected_gidx_depths` (expected 1262 / 2478 / 79620).

## Clients below 3.0.0

This prover does not dual-serve the previous guest. HTTP forwards clients `< 3.0.0`
to the parallel v2 prover (`c4_try_forward_legacy_proof`).
