#!/usr/bin/env node

const fs = require('fs');
const path = require('path');


const type_defs = [
    "verifier/types_beacon.c",
    "verifier/types_verify.c",
    "verifier/sync_committee.c"
]

let types = {}

type_defs.forEach(f => handle(f, fs.readFileSync('../src/' + f, 'utf8').split('\n')))

function toCamelCase(str) {
    str = str.trim().replace('ssz_', '').replace('SSZ_', '')
    return str[0] + str.replace('_CONTAINER', '').replace('_UNION', '').substr(1).toLowerCase().replace(/_([a-z])/g, function (match, letter) {
        return letter.toUpperCase();
    })

}

function handle(file, lines) {
    console.log(file, lines.length)
    let defs = []
    let union = false
    let comment = ''
    let line_number = 0
    for (let line of lines) {
        line_number++
        const splits = line.split('//')
        if (splits.length > 1) {
            line = splits[0]
            comment = ((comment || '') + '\n' + splits.slice(1).join('//')).trim()
        }
        // https://github.com/corpus-core/c4/blob/main/src/verifier/beacon_types.c#L1
        let match = line.match(/const\s+ssz_def_t\s+(\w+)\[.*/);
        if (match) {
            defs = []
            let type_name = match[1]
            union = type_name.endsWith('_UNION')

            type_name = toCamelCase(type_name)
            types[type_name] = defs

            defs.push('### ' + (union ? 'Union ' : '') + type_name)
            defs.push('');
            if (comment) defs.push(comment + '\n')
            defs.push(`\n The Type is defined in [${file}](https://github.com/corpus-core/c4/blob/main/src/${file}#L${line_number}).\n`)
            defs.push('```python')
            defs.push(
                union
                    ? (type_name + ' = Union []')
                    : ('class ' + type_name + '(Container):'))
            defs.push('```')
            comment = ''
        }
        else if (line.trim().startsWith('SSZ_') && (match = line.trim().startsWith("SSZ_NONE") ? ['', 'NONE', ''] : line.match(/\s+SSZ_(\w+)\(\"(\w+)\"(.*?)\).*/))) {
            const [, type, name, args] = match
            const [t, link] = getTypename(type, ('' + args).split(",").map(_ => _.trim()).filter(_ => _))
            if (union) {
                const s = defs[defs.length - 2]
                defs[defs.length - 2] = s.substring(0, s.length - 1)
                    + (s.substr(-2) == '[]' ? ' ' : ' , ')
                    + t + ']'
            }
            else {
                let cm = link ? '# ' + link : ''
                if (comment) cm = cm ? cm + ' ' + comment : '# ' + comment

                const last = defs.pop()
                defs.push(`    ${name}: ${t}   ${cm || ''}`.trimEnd())
                defs.push(last)

            }
            comment = ''
        }
    }
}

function linked(name, prefix) {
    return ''
    //    let href = '#' + (prefix || '') + name.toLowerCase().replace(/ /g, '-')
    //    return `[${name}](${href})`
}

function getTypename(type, args) {
    switch (type) {
        case "UNION":
            return [toCamelCase(args[0], 'union-') + ' <union> ', linked(toCamelCase(args[0]), 'union-')]
        case "LIST":
            return ['List [' + toCamelCase(args[0]) + ', ' + args[1] + ']', linked(toCamelCase(args[0]))]
        case "VECTOR":
            return ['Vector [' + toCamelCase(args[0]) + ', ' + args[1] + ']', linked(toCamelCase(args[0]))]
        case "BIT_LIST":
            return ['BitList [' + args[0] + ']', '']
        case "BIT_VECTOR":
            return ['BitVector [' + args[0] + ']', '']
        case "CONTAINER":
            return [toCamelCase(args[0]), linked(toCamelCase(args[0]))]
        default:
            return [toCamelCase(type), '']
    }
}

function align_comments(lines) {

    const pos = lines.reduce((max, line) => Math.max(line.lastIndexOf(' # '), max), 0)
    if (pos == 0) return lines
    return lines.map(line => {
        const splits = line.split(' # ')
        if (splits.length > 1)
            return splits[0].padEnd(pos) + ' # ' + splits[1]
        return line
    })
}


const keys = Object.keys(types).sort()
const table = keys.map(k => `- [${k}](#${types[k][0].substr(4).toLowerCase().replace(/ /g, '-')})`).join('\n')


let readme = fs.readFileSync('../README.md', 'utf8').split('\n')
let start = readme.indexOf('## SSZ Types')
let end = readme.findIndex((_, i) => _.startsWith('## ') && i > start)
readme.splice(start + 2, end - start - 2,
    table + '\n',
    keys.map(k => align_comments(types[k]).join('\n')).join('\n\n')
)
fs.writeFileSync('../README.md', readme.join('\n'))
