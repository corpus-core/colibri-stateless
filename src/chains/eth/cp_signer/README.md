## Externer Checkpoint-Signer (cp_signer)

Dieses Verzeichnis enthält `cp_signer`, eine Referenz-Implementierung für einen **externen Signer**, der finalized Ethereum-Beacon-Checkpoints signiert und die Signaturen an den Colibri-Server zurückliefert.

Der wichtigste Teil für andere Implementierungen ist die **API + Signatur-Spezifikation** weiter unten.

### Ziel

- Möglichst viele unabhängige Entwickler/Betreiber sollen ihre eigenen Signer in beliebiger Umgebung (Server, HSM, Wallet, Cloud-Function, Offline) bauen können.
- Der Colibri-Server stellt dafür eine einfache **Pull→Sign→Push**-Schnittstelle bereit.

### Begriffe

- **Checkpoint root**: 32-Byte Root (Hex), der signiert wird. Er ist der `hash_tree_root` des `BeaconBlockHeader` (SSZ).
- **Signer address**: Ethereum-Adresse (20 Bytes) des secp256k1 Keys, der signiert.

---

## HTTP API (Server)

### 1) Fehlende Checkpoints abfragen

`GET /signed_checkpoints?signer=0x<address>`

Antwort: JSON-Array; jeder Eintrag beschreibt einen zu signierenden Checkpoint:

```json
[
  {
    "period": 1621,
    "slot": 13292352,
    "root": "0x76fb0f621ff30c53869de9fe9ebd498eb041be04b49b15284a4cc614d37d0971"
  }
]
```

Hinweise:
- `period` wird serverseitig als Identifier verwendet (Period Store Pfad).
- `slot` ist zur **externen Validierung** des Roots gedacht.
- `root` ist der 32-Byte-Checkpoint, der signiert werden soll.

### 2) Signaturen hochladen

`POST /signed_checkpoints`

Body: JSON-Array, pro Signatur ein Objekt:

```json
[
  {
    "period": 1621,
    "signature": "0x<130-hex-chars-plus-0x-prefix>"
  }
]
```

Signaturformat:
- `signature` ist eine **65-Byte recoverable secp256k1 Signatur** (R,S,V) als Hex-String.
- Die Server-Implementation leitet die Ethereum-Adresse aus der Signatur ab und speichert die Signatur unter `sig_<address>`.

---

## Signatur-Spezifikation (EIP-191 / personal_sign)

Der Signer signiert **ausschließlich** den 32-Byte `checkpoint_root` (nicht ein JSON, nicht ein SSZ-Objekt), aber mit EIP-191 Message Prefix (wie bei `personal_sign`).

### Message Digest

```text
digest = keccak256("\x19Ethereum Signed Message:\n32" || checkpoint_root_32bytes)
```

Dann wird `digest` mit **secp256k1** signiert (recoverable, 65 Byte).

### Minimaler Ablauf für eigene Signer

1. `GET /signed_checkpoints?signer=0x...`
2. Für jeden Eintrag:
   - **Validieren**, dass `root` korrekt ist und **finalized** ist (siehe nächster Abschnitt).
   - `digest` wie oben berechnen.
   - `signature = secp256k1_sign_recoverable(digest)`
3. `POST /signed_checkpoints` mit `{period, signature}`.

---

## Empfehlung: Root & Finality prüfen (Beacon API / checkpointz)

Der Server liefert zwar `slot` und `root`, aber ein externer Signer sollte **nicht blind signieren**.

Empfohlene Checks:

### A) Root passt zum Slot (canonical root)

`GET /eth/v1/beacon/blocks/<slot>/root`

Erwartete Struktur:

```json
{ "data": { "root": "0x..." } }
```

Hinweis: Manche Dienste (z.B. `checkpointz`) liefern `root` auch **ohne** `0x` Prefix. Eine robuste Implementierung sollte beides akzeptieren.

### B) Block ist finalized

Bevorzugt (Beacon API, nicht immer von checkpointz unterstützt):

`GET /eth/v1/beacon/headers/0x<root>`

Erwartete Felder:
- `finalized == true`
- `data.canonical == true`
- `data.root` entspricht dem angefragten Root

Fallback (checkpointz-kompatibel):

`GET /eth/v2/beacon/blocks/<slot>`

Erwartete Felder:
- `finalized == true`

Wenn `finalized` nicht true ist: **nicht signieren** (später erneut versuchen).

---

## Referenz-CLI: cp_signer benutzen

Beispiel (lokaler Colibri-Server + lokaler Beacon Node):

```bash
build/default/bin/cp_signer \
  --server http://localhost:8090 \
  --key-file ./cp_key \
  --beacon-api http://localhost:5052 \
  --once
```

Optional kann man die Nodes explizit setzen (überschreibt/ergänzt die curl-config):

```bash
build/default/bin/cp_signer \
  --server http://localhost:8090 \
  --key 0x<32-byte-hex-private-key> \
  --checkpointz https://sync-mainnet.beaconcha.in \
  --beacon-api https://lodestar-mainnet.chainsafe.io \
  --once
```

---

## Curl-Quickstart (API-only)

1) Fehlende Checkpoints holen:

```bash
curl "http://localhost:8090/signed_checkpoints?signer=0x0123456789abcdef0123456789abcdef01234567"
```

2) Signatur posten:

```bash
curl -X POST "http://localhost:8090/signed_checkpoints" \
  -H "Content-Type: application/json" \
  -d '[{"period":1621,"signature":"0x..."}]'
```

