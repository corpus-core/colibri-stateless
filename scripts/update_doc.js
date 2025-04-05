#!/usr/bin/env node

const fs = require('fs');

const type_defs = [
    "chains/eth/ssz/beacon_denep.c",
    "chains/eth/ssz/verify_types.c",
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
                defs[defs.length - 2] = s.substring(0, s.length - 2)
                    + (s.substr(-2) == '[]' ? '[\n    ' : ',\n    ')
                    + t + (comment ? ' # ' + comment : '') + '\n]'
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
    args = args.filter(_ => _ && _ != 'undefined')
    switch (type) {
        case "UNION":
            return [toCamelCase(args[0], 'union-') + ' <union> ', linked(toCamelCase(args[0]), 'union-')]
        case "LIST":
            return ['List [' + toCamelCase(args[0]) + ', ' + args[1] + ']', linked(toCamelCase(args[0]))]
        case "VECTOR":
            return ['Vector [' + toCamelCase(args[0]) + ', ' + args[1] + ']', linked(toCamelCase(args[0]))]
        case "BYTE_VECTOR":
            return ['ByteVector [' + args[0] + ']', '']
        case "BIT_LIST":
            return ['BitList [' + args[0] + ']', '']
        case "BIT_VECTOR":
            return ['BitVector [' + args[0] + ']', '']
        case "CONTAINER":
            return [toCamelCase(args[0]), linked(toCamelCase(args[0]))]
        default:
            return [toCamelCase(type) + (args.length ? `[${args.join()}]` : ''), '']
    }
}

function align(lines, divider = ' # ') {
    lines = lines.join('\n').split('\n')
    let p = lines.findIndex(line => line.startsWith('```python'))

    const pos = lines.slice(p + 1).reduce((max, line) => Math.max(line.lastIndexOf(divider), max), 0)
    if (pos == 0) return lines
    return lines.map((line, i) => {
        if (i < p) return line
        const splits = line.split(divider)
        if (splits.length > 1)
            return splits[0].padEnd(pos) + divider + splits[1]
        return line
    })
}

const inlineUnions = true
if (inlineUnions) {
    function getUnionContent(name) {
        const start = types[name].indexOf('```python')
        const end = types[name].indexOf('```', start)
        let content = types[name].slice(start + 1, end).join('\n').split('\n')

        if (content[0].indexOf(' = ') > 0)
            content[0] = content[0].split(' = ')[1]
        return content
    }
    const unionTypes = Object.keys(types).filter(k => types[k][0].startsWith('### Union'))
    Object.entries(types).forEach(([k, v]) => {
        if (!unionTypes.includes(k)) {
            for (let i = 0; i < v.length; i++) {
                if (v[i].indexOf('<union>') > 0) {
                    const splits = v[i].split('<union>')
                    const unionName = splits[0].split(':')[1].trim()
                    const unionContent = getUnionContent(unionName)
                    v[i] = splits[0].replace(unionName, '').trimEnd() + ' Union[ ' + splits[1] + '\n' + unionContent.slice(1).map(_ => '    ' + _).join('\n')
                }
            }
        }
    })
    unionTypes.forEach(k => delete types[k])
}


function replace_section(readme, section, content) {
    let level = '#'.repeat(section.indexOf(' ')) + ' '
    let start = readme.indexOf(section)
    let end = readme.findIndex((_, i) => (_.startsWith(level) || _.startsWith(level.substring(1))) && i > start)
    readme.splice(start + 2, end - start - 2, content)
}

function createToC(readme) {
    let toc = []
    let level = 0
    for (let line of readme.join('\n').split('\n')) {
        if (line.startsWith('##')) {
            level = line.indexOf(' ')
            let name = line.substring(level + 1).trim()
            toc.push(
                ' '.repeat(level * 4 - 8)
                + '- [' + name + '](#'
                + name.toLowerCase().replace(/ /g, '-') + ')')
        }
    }
    return toc.join('\n') + '\n\n'
}

function get_cmake_options() {

    function cmake_tokens(line) {
        let split = line.trim().split(/( +|\")/g)
        let tokens = ['']
        let quote = false
        for (let i = 0; i < split.length; i++) {
            if (i % 2 == 0) {
                if (quote)
                    tokens[tokens.length - 1] += split[i]
                else
                    tokens.push(split[i])
            }
            else if (split[i] == '"') quote = !quote
            else if (quote)
                tokens[tokens.length - 1] += split[i]
        }
        return tokens.filter(_ => _)
    }


    function read_dir(dir) {
        if (dir.endsWith('CMakeLists.txt')) {
            let content = fs.readFileSync(dir, 'utf8').split('\n')
            for (let line of content) {
                if (line.trim().startsWith('option(')) {
                    let [name, description, val] = cmake_tokens(line.split('(')[1].split(')')[0].trim())
                    options[name] = {
                        default: val,
                        description: description
                    }
                }
            }
        }
        else if (fs.statSync(dir).isDirectory())
            fs.readdirSync(dir).forEach(file => read_dir(dir + '/' + file))
    }

    let options = {
        CMAKE_BUILD_TYPE: {
            default: 'Release',
            description: 'Build type',
            options: ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'],
        }
    };

    ['../src', '../libs', '../CMakeLists.txt'].forEach(read_dir)

    return '| Flag | descr  | default |\n' +
        '| :--- | :----- | :----- |\n' +
        Object.keys(options).sort().map(
            name => `| **${name}** | ${options[name].description} | ${options[name].default || ''}  |`
        )
            .join('\n') + '\n\n'



}


const keys = Object.keys(types).sort()
const table = keys.map(k => `- [${k}](#${types[k][0].substr(4).toLowerCase().replace(/ /g, '-')})`).join('\n')


// update readme

let readme = fs.readFileSync('../README.md', 'utf8').split('\n')

replace_section(readme, '## SSZ Types', keys.map(k => align(align(types[k], ': '), ' # ').join('\n')).join('\n\n'))
replace_section(readme, '### CMake Options', get_cmake_options())
replace_section(readme, '## Index', createToC(readme))
fs.writeFileSync('../README.md', readme.join('\n'))
