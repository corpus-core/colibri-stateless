const fs = require('fs')
const { get_full_src_path } = require('./utils')

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
                        path: dir == '../CMakeLists.txt' ? 'general' : dir.split('/').at(-2),
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
            path: 'general',
            default: 'Release',
            description: 'Build type (Debug, Release, RelWithDebInfo, MinSizeRel)',
        },
        INCLUDE: {
            path: 'general',
            default: '',
            description: 'Path to additional CMakeLists.txt Dir, which will included into the build, allowing to extend the binaries.',
        }
    };

    ['../src', '../libs', '../CMakeLists.txt'].map(get_full_src_path).forEach(read_dir)

    const content = Object.values(options).map(o => o.path).filter((p, i, a) => a.indexOf(p) == i).map(path => {
        return '## ' + (path || 'Cmake') + ' options \n\n' +
            '| Flag | descr  | default |\n' +
            '| :--- | :----- | :----- |\n' +
            Object.keys(options).sort().filter(k => options[k].path == path).map(
                name => `| **${name}** | ${options[name].description} | ${options[name].default || ''}  |`
            )
                .join('\n') + '\n\n'

    }).join('\n\n').split('\n')

    return [{
        title: 'CMake',
        path: '/building/cmake-colibri-lib.md',
        level: 1,
        children: [],
        types: [],
        parent_title: null,
        content: content
    }]
}

module.exports = { get_cmake_options }
