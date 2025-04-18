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
            rpcs: [],
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

function add_rpc(line, sections, comment) {
    let section = sections.at(-1)
    if (!section) return false
    if (line.includes('proofable_methods[] = ')) section.rpc_state = 'proofable';
    if (line.includes('local_methods[] = ')) section.rpc_state = 'local';
    if (line.includes('not_verifieable_yet_methods[] = ')) section.rpc_state = 'not_verifieable';
    if (line.includes('RPC_METHOD')) {
        const args = line.split('(')[1].split(')')[0].split(',').map(arg => arg.trim());
        const method = args[0].replace(/["']/g, '');
        const data_type = args[1] == 'Void' ? '' : args[1];
        const proof_type = args[2] == 'Void' ? '' : args[2];
        section.rpcs.push({ method, data_type, proof_type, comment, status: section.rpc_state });
        return true
    }
    return false

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
        if (add_rpc(line, sections, comment)) {
            comment = ''
            continue
        }

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
    return { types, sections: sections.filter(s => s.parent_title == null) }
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

function add_sections(old_sections, new_sections) {
    for (let section of new_sections) {
        let found = old_sections.find(s => s.title == section.title)
        if (found) {
            add_sections(found.children, section.children)
            found.content = [...found.content, ...section.content]
            found.types = [...found.types, ...section.types]
        }
        else
            old_sections.push(section)
    }
}

function create_rpc_table(section) {
    section.children.forEach(child => create_rpc_table(child))
    if (!section.rpcs || section.rpcs.length == 0) return
    section.content.push('')
    section.content.push('| Method | Status | Data Type | Proof Type |');
    section.content.push('| :----- | :----- | :-------- | :--------- |');
    for (let rpc of section.rpcs)
        section.content.push(`| [${rpc.method}](https://docs.alchemy.com/reference/${rpc.method.replace(/_/g, '-').toLowerCase()}) | ${rpc.status == 'proofable' ? '✅' : (rpc.status == 'local' ? '🟢' : '❌')} | ${rpc.data_type ? '[' + rpc.data_type + ']()' : ''} | ${rpc.proof_type ? '[' + rpc.proof_type + ']()' : ''} |`);
    section.content.push('')

}

function parse_ssz_files(files) {
    let types = {}
    let sections = []
    for (let file of files) {
        const { types: t, sections: s } = parse_ssz_file(file)
        types = { ...types, ...t }
        add_sections(sections, s)
    }


    // assign paths to sections
    for (let section of sections) assign_path(section, '')
    for (let type of Object.values(types)) create_type(type, types)
    sections.forEach(create_rpc_table)

    return sections
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