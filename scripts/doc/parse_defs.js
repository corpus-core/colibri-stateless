#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const { toCamelCase, get_full_src_path, align, get_doc_path } = require('./utils');



function add_section(line, sections) {
    const regex = /\/\/ (\:+) (.*)/g;
    const m = regex.exec(line);
    let section = sections.at(-1)
    if (section && section.open) {
        if (m)
            section.open = false
        else {
            if (line.startsWith('//')) {
                section.content.push(line.substring(3))
                return true
            }
            section.open = false
            return false
        }
    }

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
            path: null,
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
        section.rpcs.push({
            method,
            data_type,
            proof_type,
            comment,
            status: section.rpc_state,
        });
        return true
    }
    return false

}

function add_function_def(sections, line, doc_comment, file, line_number) {
    let section = sections.at(-1)
    if (!section) return
    // proofer_t* c4_create_proofer_ctx(char* method, char* params, uint64_t chain_id, uint32_t flags);
    let p = line.indexOf('(')
    let fn = p >= 0 ? line.substring(0, p).trim().split(' ').at(-1) : line.trim().split(' ').at(-1).replace(';', '').trim()

    section.content.push('\n## ' + fn)
    section.content.push(`[${file.replace('../', '')}](https://github.com/corpus-core/colibri-stateless/blob/dev/src/${file}#L${line_number})`)
    section.content.push('')
    section.content.push(doc_comment.comment)
    section.content.push('')

    section.content.push('```c')
    section.content.push(line)
    section.content.push('```')

    if (doc_comment.params && Object.keys(doc_comment.params).length > 0) {
        section.content.push('**Parameters**')
        section.content.push('')
        for (let param of Object.keys(doc_comment.params)) {
            section.content.push(`- **\`${param}\`** : ${doc_comment.params[param]}`)
        }
        section.content.push('')
    }

    if (doc_comment.returns) {
        section.content.push('**Returns**')
        section.content.push('')
        section.content.push(doc_comment.returns)
        section.content.push('')
    }
}

function handle_doc_comment(line, doc_comment, sections, file, line_number) {
    if (line.trim().startsWith('*/')) {
        doc_comment.open = false
        return true
    }
    else if (line.trim().startsWith('*')) {
        line = line.trim().substring(2)
        if (line.startsWith('@param')) doc_comment.params[line.split(' ')[1]] = line.split(' ').slice(2).join(' ')
        else if (line.startsWith('@return')) doc_comment.returns = line.split(' ').slice(1).join(' ')
        else doc_comment.comment += '\n' + line
    }
    else if (doc_comment && doc_comment.open) {
        doc_comment.comment += '\n' + line
    }
    else if (line.trim().endsWith(';')) {
        doc_comment.statement += '\n' + line
        add_function_def(sections, doc_comment.statement.trim(), doc_comment, file, line_number)
        return false
    }
    else
        doc_comment.statement += '\n' + line
    return true;
}
function parse_ssz_file(file) {
    const lines = fs.readFileSync(get_full_src_path(file), 'utf8').split('\n');

    let is_markdown = file.endsWith('.md')
    let sections = []
    let def = { members: [] }
    let types = {}
    let comment = ''
    let doc_comment = null
    let line_number = 0

    for (let line of lines) {
        line_number++
        if (is_markdown) line = '// ' + line
        if (add_section(line, sections)) continue
        if (line.trim().startsWith('/**')) doc_comment = {
            open: true,
            params: {},
            returns: '',
            comment: '',
            statement: '',
        }
        else if (doc_comment) {
            if (handle_doc_comment(line, doc_comment, sections, file, line_number)) continue
            else doc_comment = null
        }
        // handle comments
        const splits = line.split('//')
        if (splits.length > 1) {
            line = splits[0]
            comment = ((comment || '') + '\n' + splits.slice(1).join('//')).trim()
        }
        if (add_rpc(line, sections, comment)) {
            comment = ''
            continue
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
    let name = section.title.replace(/[ \/]/g, '-').toLowerCase()
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
            found.rpcs = [...found.rpcs, ...section.rpcs];
        }
        else
            old_sections.push(section)
    }
}

function create_rpc_table(section, sections) {
    section.children.forEach(child => create_rpc_table(child, sections))
    if (!section.rpcs || section.rpcs.length == 0) return

    function html_link(type) {
        if (!type) return ''
        let prefix = ''
        let postfix = ''
        if (type.startsWith('List')) {
            prefix = 'List ['
            postfix = ']'
            type = type.substring(4)
        }

        return `${prefix}<a href="">${type}</a>${postfix}`
    }

    section.content.push('')
    section.content.push('<table>');
    section.content.push('  <thead>');
    section.content.push('    <tr>');
    section.content.push('      <th>Method</th>');
    section.content.push('      <th width="80" align="center">Status</th>');
    section.content.push('      <th>Data Type</th>');
    section.content.push('      <th>Proof Type</th>');
    section.content.push('    </tr>');
    section.content.push('  </thead>');
    section.content.push('  <tbody>');

    for (let rpc of section.rpcs) {
        const link = `https://docs.alchemy.com/reference/${rpc.method.replace(/_/g, '-').toLowerCase()}`
        const methodLink = `<a href="${link}" target="_blank" rel="noopener noreferrer">${rpc.method}</a>`;
        const statusIcon = rpc.status == 'proofable' ? '✅' : (rpc.status == 'local' ? '🟢' : '❌');

        section.content.push('    <tr>');
        section.content.push(`      <td>${methodLink}</td>`);
        section.content.push(`      <td style="text-align: center;">${statusIcon}</td>`);
        section.content.push(`      <td>${html_link(rpc.data_type)}</td>`);
        section.content.push(`      <td>${html_link(rpc.proof_type)}</td>`);
        section.content.push('    </tr>');

        let s = find_section_for_type(sections, rpc.proof_type)
        if (s) {
            if (!s.content.length || !s.content.at(-1).startsWith('- '))
                s.content.push('\nThis Proof is used for the following RPC-Methods:\n')
            s.content.push(`- [${rpc.method}](${link}) ${rpc.comment}`.trim())
        }
    }

    section.content.push('  </tbody>');
    section.content.push('</table>');
    section.content.push('')
}

function find_section_for_type(sections, type) {
    if (!type) return null
    if (type.startsWith('List')) type = type.substring(4)
    for (let section of sections) {
        if (section.types.find(t => t.type == type)) return section
        const found = find_section_for_type(section.children, type)
        if (found) return found
    }
    return null
}

function parse_ssz_files(files) {
    let types = {}
    let sections = []
    for (let file of files) {
        const { types: t, sections: s } = parse_ssz_file(file)
        types = { ...types, ...t }
        add_sections(sections, s)
    }

    for (let section of sections) assign_path(section, '')
    for (let type of Object.values(types)) create_type(type, types)
    sections.forEach(s => create_rpc_table(s, sections))

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

function add_references(content, members, types) {
    for (let member of members) {
        if (member.type == 'Union')
            add_references(content, types[member.type_name].members, types)
        else if (member.type == 'List' || member.type == 'Vector')
            content.push(toCamelCase(member.args[0]))
        else if (member.type == 'Container')
            content.push(member.type_name)
    }
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

    let refs = []
    add_references(refs, type.members, types)
    refs = refs.filter((_, i, arr) => arr.indexOf(_) === i).filter(_ => types[_])
    if (refs.length > 0) {
        type.content.push('')
        type.content.push('**Referenced Types**')
        type.content.push('')
        for (let ref of refs)
            type.content.push(`- [${ref}]()`)
        type.content.push('')
    }


}

module.exports = {
    parse_ssz_files
}