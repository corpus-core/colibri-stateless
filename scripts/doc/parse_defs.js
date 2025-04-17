#!/usr/bin/env node

const fs = require('fs');
const { toCamelCase, get_full_src_path, align } = require('./utils');



function add_section(line, sections) {
    let section = sections.at(-1)
    if (section && section.open) {
        if (line.startsWith('//')) {
            section.content.push(line.substring(3))
            return true
        }
        section.open = false
        return false
    }

    const regex = /\/\/ (\#+) (.*)/g;
    const m = regex.exec(line);
    if (m) {
        const section = {
            title: m[2],
            level: m[1].length,
            content: [],
            children: [],
            open: true,
            types: [],
            parent_title: null,
        }
        for (let i = sections.length - 1; i >= 0; i--) {
            if (sections[i].level < section.level) {
                section.parent_title = sections[i].title
                sections[i].children.push(section)
                break
            }
        }

        sections.push(section)
        return true
    }
    return false
}

function get_typename(type, args) {
    args = args.filter(_ => _ && _ != 'undefined')
    switch (type) {
        case "Union":
            return toCamelCase(args[0])
        case "List":
            return 'List [' + toCamelCase(args[0]) + ', ' + args[1] + ']'
        case "Vector":
            return 'Vector [' + toCamelCase(args[0]) + ', ' + args[1] + ']'
        case "ByteVector":
            return 'ByteVector [' + args[0] + ']'
        case "BitList":
            return 'BitList [' + args[0] + ']'
        case "BitVector":
            return 'BitVector [' + args[0] + ']'
        case "Container":
            return toCamelCase(args[0])
        default:
            return type + (args.length ? `[${args.join()}]` : '')
    }
}

function parse_ssz_file(file) {
    const lines = fs.readFileSync(get_full_src_path(file), 'utf8').split('\n');

    let sections = []
    let def = { members: [] }
    let types = {}
    let comment = ''
    let line_number = 0

    for (let line of lines) {
        line_number++
        if (add_section(line, sections)) continue

        // handle comments
        const splits = line.split('//')
        if (splits.length > 1) {
            line = splits[0]
            comment = ((comment || '') + '\n' + splits.slice(1).join('//')).trim()
        }

        // handle type definitions
        let match = line.match(/const\s+ssz_def_t\s+(\w+)\[.*/);
        if (match) {
            let type_name = match[1]
            let union = type_name.endsWith('_UNION')
            type_name = toCamelCase(type_name)
            def = {
                file,
                type: type_name,
                is_union: union,
                line_number,
                comment,
                members: [],
            }
            types[type_name] = def
            const section = sections.at(-1)
            if (section) {
                section.types.push(def)
            }
            comment = ''
        }
        else if (line.trim().startsWith('SSZ_') && (match = line.trim().startsWith("SSZ_NONE") ? ['', 'NONE', '', ''] : line.match(/\s+SSZ_(\w+)\(\"(\w+)\"(.*?)\).*/))) {
            const [, type_string, name, args_string] = match
            const args = ('' + args_string).split(",").map(_ => _.trim()).filter(_ => _)
            const type = toCamelCase(type_string)
            def.members.push({
                name,
                type,
                args,
                comment,
                file,
                line_number,
                type_name: get_typename(type, args)
            })
            comment = ''
        }
    }
    return { types, sections }
}

function assign_path(section, parent_dir) {
    let name = section.title.replace(/ /g, '-').toLowerCase()
    if (section.children.length > 0)
        section.path = parent_dir + '/' + name + '/README.md'
    else
        section.path = parent_dir + '/' + name + '.md'

    for (let child of section.children)
        assign_path(child, parent_dir + '/' + name)
}


function parse_ssz_files(files) {
    let types = {}
    let sections = []
    for (let file of files) {
        const { types: t, sections: s } = parse_ssz_file(file)
        types = { ...types, ...t }
        sections = [...sections, ...s.filter(s => s.parent_title == null)]
    }

    // assign paths to sections
    for (let section of sections) assign_path(section, '')
    for (let type of Object.values(types)) create_type(type, types)

    return { types, sections }
}

function add_members(content, members, types, level = '    ', is_union = false) {
    members.forEach((member, i) => {
        if (member.type == 'Union') {
            let type = types[member.type_name]
            content.push(level + member.name + ' : Union [ ' + (member.comment ? ' # ' + member.comment : ''))
            add_members(content, type.members, types, level + '    ', true)
        }
        else if (is_union)
            content.push(level + member.type_name + (i < members.length - 1 ? ',' : ']') + ' # ' + (member.comment || member.name))
        else {
            content.push(level + member.name + ' : ' + member.type_name + (member.comment ? ' # ' + member.comment : ''))
        }
    })
}


function create_type(type, types) {
    let content = []
    type.content = content
    if (type.is_union) return;
    content.push('\n## ' + type.type + '\n')
    content.push(type.comment)
    content.push('')
    content.push(`\nThe Type is defined in [src/${type.file}](https://github.com/corpus-core/colibri-stateless/blob/dev/src/${type.file}#L${type.line_number}).\n`)
    content.push('')
    content.push('```python')
    content.push('class ' + type.type + '(Container):')
    add_members(content, type.members, types)
    content.push('```')
    content.push('')

    type.content = align(content, ' : ')
    type.content = align(type.content, ' # ')


}

module.exports = {
    parse_ssz_files
}