const { parse_ssz_files } = require('./parse_defs')
const { read_summary } = require('./utils')

const type_defs = [
    "chains/eth/ssz/beacon_denep.c",
    "chains/eth/ssz/verify_types.c",
    "chains/eth/ssz/verify_proof_types.h",
    "chains/eth/ssz/verify_data_types.h",
]

const { types, sections } = parse_ssz_files(type_defs)
const summary = read_summary()
summary.set_sections(sections)
summary.write()
//console.log(summary.lines.join('\n'))

//console.log(JSON.stringify(sections, null, 2))

