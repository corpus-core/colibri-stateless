const fs = require('fs')
const path = require('path')
const { get_doc_path } = require('./utils')

function find_links_in_dir(dir, links) {
    if (links == undefined) links = {}
    const files = fs.readdirSync(get_doc_path(dir))
    for (let file of files) {
        const path = dir + '/' + file
        if (fs.statSync(get_doc_path(path)).isDirectory()) find_links_in_dir(path, links)
        else if (path.endsWith('.md')) find_links_in_file(path, links)
    }
    return links
}

function find_links_in_file(file, links) {
    const lines = fs.readFileSync(get_doc_path(file), 'utf8').split('\n')
    for (let line of lines) {
        if (line.startsWith('#') && line.includes('# ')) {
            let name = line.substring(line.indexOf('# ') + 2).trim()
            links[name] = {
                file,
                anchor: name.replace(/ /g, '_').toLowerCase()
            }
        }
    }
}

function replace_links_in_dir(dir, links) {
    const files = fs.readdirSync(get_doc_path(dir))
    for (let file of files) {
        const path = dir + '/' + file
        if (fs.statSync(get_doc_path(path)).isDirectory()) replace_links_in_dir(path, links)
        else if (path.endsWith('.md')) replace_links_in_file(path, links)
    }
}
function replace_links_in_file(file, links) {
    const filePath = get_doc_path(file)
    const originalContent = fs.readFileSync(filePath, 'utf8')
    let modifiedContent = originalContent
    let modified = false;

    // Regex to find markdown links with empty URLs: [label]()
    const regex = /\[([^\]]+)\]\(\)/g;

    modifiedContent = modifiedContent.replace(regex, (match, label) => {
        if (links[label]) {
            const targetLinkInfo = links[label]
            const targetFilePath = get_doc_path(targetLinkInfo.file)
            const currentDirPath = path.dirname(filePath)

            let relativePath = path.relative(currentDirPath, targetFilePath)
            // Ensure relative path starts with './' if in the same directory
            if (!relativePath.startsWith('.') && !relativePath.startsWith('/'))
                relativePath = './' + relativePath;

            if (!relativePath.endsWith('.md')) relativePath += '.md'


            // Ensure md extension if missing
            const newLink = `[${label}](${relativePath}#${targetLinkInfo.anchor})`
            if (newLink !== match) {
                modified = true;
            }
            return newLink;
        } else {
            // If label not found in links, remove the link syntax
            modified = true;
            return label;
        }
    });


    if (modified) {
        fs.writeFileSync(filePath, modifiedContent)
    }
}

module.exports = {
    find_links_in_dir,
    replace_links_in_dir
}