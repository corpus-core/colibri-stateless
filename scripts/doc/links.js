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

    // Regex to find EITHER markdown links with empty URLs: [label]()
    // OR empty HTML anchor tags: <a href="">label</a>
    // Use named capture groups (?<mdLabel>...) and (?<htmlLabel>...) for clarity
    const regex = /\[(?<mdLabel>[^\]]+)\]\(\)|<a href="">\s*(?<htmlLabel>[^<]+)\s*<\/a>/g;

    modifiedContent = modifiedContent.replace(regex, (match, ...args) => {
        // Extract named capture groups from the end of the arguments list
        const groups = args[args.length - 1];
        const mdLabel = groups.mdLabel;   // Label from [label]() if matched
        const htmlLabel = groups.htmlLabel; // Label from <a href="">label</a> if matched

        const label = mdLabel || htmlLabel; // Get the actual label

        if (label && links[label]) {
            const targetLinkInfo = links[label]
            const targetFilePath = get_doc_path(targetLinkInfo.file)
            const currentDirPath = path.dirname(filePath)

            let relativePath = path.relative(currentDirPath, targetFilePath)
            // Ensure relative path starts with './' if needed for relative links
            if (!relativePath.startsWith('.') && !relativePath.startsWith('/')) {
                relativePath = './' + relativePath;
            }
            // Remove .md extension for GitBook style links
            relativePath = relativePath.replace(/\.md$/i, '');

            const anchor = targetLinkInfo.anchor;
            let newLink;

            // Generate the replacement link in the original format
            if (mdLabel) {
                newLink = `[${label}](${relativePath}.md#${anchor})`;
            } else { // htmlLabel must be defined
                newLink = `<a href="${relativePath}#${anchor}">${label}</a>`;
            }

            if (newLink !== match) {
                modified = true;
            }
            return newLink;
        } else {
            // If label not found in links, or label is somehow empty, remove the link syntax
            // and just return the plain label text.
            modified = true;
            return label || ''; // Return the label itself or empty string if label was empty
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