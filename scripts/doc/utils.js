const fs = require('fs')

function toCamelCase(str) {
    str = str.trim().replace('ssz_', '').replace('SSZ_', '')
    return str[0] + str.replace('_CONTAINER', '').replace('_UNION', '').substr(1).toLowerCase().replace(/_([a-z])/g, function (match, letter) {
        return letter.toUpperCase();
    })
}

function get_full_src_path(path_from_src) {
    return __dirname + '/../../src/' + path_from_src
}

function get_doc_path(path) {
    let dir = process.env.DOCS_DIR || __dirname + '/../../../colibri-stateless-doc'
    return dir + '/specification/' + path
}

function create_summary_entries(section, level, root_path, entries) {
    if (entries == undefined) entries = []
    section.full_path = root_path + section.path.substr(1)
    let line = ''.padStart(level + (section.level - 1) * 2, ' ') + '* [' + section.title + '](' + section.full_path + ')'
    entries.push(line)
    section.children.forEach(child => create_summary_entries(child, level, root_path, entries))
    return entries
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

function get_main_path_in_summary(lines, pos) {
    for (let i = pos; i >= 0; i--) {
        if (lines[i].trim().startsWith('#')) {
            let title = lines[i].substring(lines[i].indexOf(' ') + 1).trim()
            return title.replace(/ /g, '-').toLowerCase() + '/'
        }
    }
    return ''
}

function write_section(section) {
    if (section.content.length > 0) {
        let path = get_doc_path(section.full_path)
        let dir = path.substring(0, path.lastIndexOf('/'))
        fs.mkdirSync(dir, { recursive: true })
        fs.writeFileSync(path,
            '# ' + section.title + '\n\n'
            + section.content.join('\n') + "\n\n"
            + section.types.map(type => type.content.join('\n')).join('\n\n')
        )
    }
    for (let child of section.children)
        write_section(child)
}

function read_summary() {
    const path = get_doc_path('SUMMARY.md')
    let lines = fs.readFileSync(path, 'utf8').split('\n')
    let sections_to_write = []
    return {
        lines,
        write: () => {
            fs.writeFileSync(path, lines.join('\n'))
            for (let section of sections_to_write)
                write_section(section)
        },
        set_sections: function (sections) {
            let last_pos = 0
            let pos_end = 0
            sections.forEach(section => {
                sections_to_write.push(section)
                let pos_start = lines.findIndex(line => line.trim().startsWith('* [' + section.title + ']'))
                if (pos_start == -1) {
                    pos_start = last_pos
                    pos_end = pos_start
                }
                else {
                    let level = lines[pos_start].indexOf('*')
                    pos_end = lines.length - 1
                    for (let i = pos_start + 1; i < lines.length; i++) {
                        let l = lines[i].indexOf('*')
                        if (l < 0 || l <= level) {
                            pos_end = i
                            break
                        }
                    }
                }

                let level = Math.max(0, lines[pos_start].indexOf('*'))
                let entries = create_summary_entries(section, level, get_main_path_in_summary(lines, pos_start))
                lines.splice(pos_start, pos_end - pos_start, ...entries)
                last_pos = pos_start + (pos_end - pos_start + entries.length)

            })
        }
    }
}

module.exports = {
    toCamelCase,
    get_full_src_path,
    get_doc_path,
    read_summary,
    align
}