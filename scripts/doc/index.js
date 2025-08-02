const { parse_ssz_files } = require('./parse_defs')
const { read_summary } = require('./utils')
const { get_cmake_options } = require('./cmake')
const { find_links_in_dir, replace_links_in_dir } = require('./links')

const doc_files = [
    // typedefs
    "chains/eth/verifier/eth_verify.c",
    "chains/eth/ssz/beacon_denep.c",
    "chains/eth/ssz/verify_types.c",
    "chains/eth/ssz/verify_proof_types.h",
    "chains/eth/ssz/verify_data_types.h",
    "chains/eth/threat_model.md",
    "chains/eth/benchmark.md",
    // cli
    "cli/proofer.c",
    "cli/ssz.c",
    "cli/verifier.c",
    // bindings
    "../bindings/emscripten/doc.md",
    "../bindings/kotlin/doc.md",
    "../bindings/swift/doc.md",
    "../bindings/python/doc.md",

    // api
    "verifier/verify.h",
    "proofer/proofer.h",
    "util/bytes.h",
    "../bindings/colibri.h"
]

// the folders where to replace the links
const link_dirs = [
    'developer-guide',
    'introduction',
    'specifications',
]

// a list of types to replace with external types.
const extern_types = {
    BeaconBlockHeader: 'https://ethereum.github.io/consensus-specs/specs/phase0/beacon-chain/#beaconblockheader',
    DenepExecutionPayload: 'https://ethereum.github.io/consensus-specs/specs/deneb/beacon-chain/#executionpayload',
    LightClientUpdate: 'https://ethereum.github.io/consensus-specs/specs/altair/light-client/sync-protocol/#lightclientupdate',
    DenepLightClientUpdate: 'https://ethereum.github.io/consensus-specs/specs/altair/light-client/sync-protocol/#lightclientupdate'
}


const summary = read_summary()
summary.set_sections(parse_ssz_files(doc_files))
summary.set_sections(get_cmake_options())
summary.write()

// update links
let links = {}
link_dirs.forEach(dir => find_links_in_dir(dir, links))
Object.keys(extern_types).forEach(type => links[type] = { url: extern_types[type] })
link_dirs.forEach(dir => replace_links_in_dir(dir, links))

