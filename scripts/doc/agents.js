#!/usr/bin/env node
/**
 * Auto-generates sections in AGENTS.md files from source code.
 *
 * Scans header files for public functions, CMakeLists.txt for options,
 * and the directory tree for module overviews. Inserts content between
 * <!-- AUTO:<id>:START --> and <!-- AUTO:<id>:END --> markers.
 *
 * Usage: node scripts/doc/agents.js
 */

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '../..');
const SRC = path.join(ROOT, 'src');

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

function readFile(p) {
  return fs.readFileSync(p, 'utf8');
}

function listDir(dir, filter) {
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir)
    .filter(f => !filter || filter(f))
    .sort();
}

function isDir(p) {
  try { return fs.statSync(p).isDirectory(); } catch { return false; }
}

const COUNT_SKIP = new Set([
  '.git', '.github', '.cursor', 'node_modules', '.cxx', 'venv',
  '__pycache__', '.gradle', '.idea', 'xcframework', 'dist',
  'rn-web-test',
]);

function countFiles(dir, ext) {
  let count = 0;
  if (!fs.existsSync(dir)) return 0;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.name.startsWith('build') || COUNT_SKIP.has(entry.name) || entry.name.startsWith('.')) continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) count += countFiles(full, ext);
    else if (!ext || entry.name.endsWith(ext)) count++;
  }
  return count;
}

function relPath(full) {
  return path.relative(ROOT, full);
}

// ---------------------------------------------------------------------------
// Parse header files for public function signatures and doc comments
// ---------------------------------------------------------------------------

function parseHeaderFunctions(headerPath) {
  if (!fs.existsSync(headerPath)) return [];
  const lines = readFile(headerPath).split('\n');
  const functions = [];
  let docComment = null;
  let multiLineDecl = '';
  let collectingDecl = false;

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const trimmed = line.trim();

    // Start of doc comment
    if (trimmed.startsWith('/**')) {
      docComment = { comment: '', params: {}, returns: '' };
      if (trimmed.endsWith('*/') && trimmed.length > 5) {
        docComment.comment = trimmed.slice(3, -2).trim();
        continue;
      }
      continue;
    }

    // Inside doc comment
    if (docComment && docComment.comment !== undefined && !trimmed.startsWith('*/') && !collectingDecl) {
      if (trimmed.startsWith('*')) {
        const content = trimmed.substring(1).trim();
        if (content.startsWith('@param')) {
          const parts = content.split(/\s+/);
          docComment.params[parts[1]] = parts.slice(2).join(' ');
        } else if (content.startsWith('@return')) {
          docComment.returns = content.split(/\s+/).slice(1).join(' ');
        } else {
          docComment.comment += (docComment.comment ? ' ' : '') + content;
        }
      }
      continue;
    }

    // End of doc comment
    if (docComment && trimmed.startsWith('*/')) {
      continue;
    }

    // After doc comment, look for function declaration
    if (docComment) {
      // Skip typedefs, macros, enums
      if (trimmed.startsWith('typedef') || trimmed.startsWith('#') || trimmed.startsWith('enum')) {
        // Still capture typedef info
        if (trimmed.startsWith('typedef') && trimmed.includes('(') === false) {
          const name = trimmed.replace(/;.*/, '').split(/\s+/).pop();
          if (name && docComment.comment) {
            functions.push({
              name: name,
              signature: trimmed.replace(';', ''),
              description: docComment.comment.split('.')[0].trim(),
              line: i + 1,
            });
          }
        }
        docComment = null;
        continue;
      }

      // Function declaration might span multiple lines
      if (trimmed.includes('(')) {
        collectingDecl = true;
        multiLineDecl = trimmed;
        if (trimmed.includes(';')) {
          collectingDecl = false;
          const decl = multiLineDecl;
          const funcName = extractFuncName(decl);
          if (funcName && !funcName.startsWith('_')) {
            functions.push({
              name: funcName,
              signature: cleanSignature(decl),
              description: docComment.comment.split('.')[0].trim(),
              line: i + 1,
            });
          }
          docComment = null;
          multiLineDecl = '';
        }
        continue;
      }
      docComment = null;
    }

    // Continue collecting multi-line declaration
    if (collectingDecl) {
      multiLineDecl += ' ' + trimmed;
      if (trimmed.includes(';')) {
        collectingDecl = false;
        const decl = multiLineDecl;
        const funcName = extractFuncName(decl);
        if (funcName && !funcName.startsWith('_') && docComment) {
          functions.push({
            name: funcName,
            signature: cleanSignature(decl),
            description: docComment.comment.split('.')[0].trim(),
            line: i + 1,
          });
        }
        docComment = null;
        multiLineDecl = '';
      }
    }
  }
  return functions;
}

function extractFuncName(decl) {
  // Remove annotations like NONNULL_FOR(...), M_RET, M_TAKE(...), RETURNS_NONNULL
  let clean = decl
    .replace(/NONNULL_FOR\([^)]*\)/g, '')
    .replace(/NONNULL/g, '')
    .replace(/RETURNS_NONNULL/g, '')
    .replace(/M_RET/g, '')
    .replace(/M_TAKE\([^)]*\)/g, '')
    .replace(/COUNTED_BY\([^)]*\)/g, '')
    .trim();

  const parenIdx = clean.indexOf('(');
  if (parenIdx < 0) return null;
  const beforeParen = clean.substring(0, parenIdx).trim();
  const parts = beforeParen.split(/[\s*]+/);
  return parts[parts.length - 1].replace(/^\*+/, '');
}

function cleanSignature(decl) {
  return decl
    .replace(/\s+/g, ' ')
    .replace(/;.*$/, '')
    .trim();
}

// ---------------------------------------------------------------------------
// Parse CMake options from CMakeLists.txt files
// ---------------------------------------------------------------------------

function parseCmakeOptions(dir) {
  const options = [];
  const seen = new Set();

  function scan(d) {
    if (!fs.existsSync(d)) return;
    const stat = fs.statSync(d);
    const rel = path.relative(ROOT, d);

    // Skip build directories, _deps, and dot-dirs
    if (rel.startsWith('build') || rel.includes('_deps') || rel.includes('.cxx') ||
        rel.includes('node_modules') || rel.includes('venv')) return;

    if (stat.isFile() && d.endsWith('CMakeLists.txt')) {
      const lines = readFile(d).split('\n');
      for (const line of lines) {
        if (line.trim().startsWith('option(')) {
          const inner = line.trim().match(/option\(([^)]+)\)/);
          if (inner) {
            const tokens = parseCmakeTokens(inner[1]);
            if (tokens.length >= 2) {
              const key = tokens[0];
              if (seen.has(key)) continue;
              seen.add(key);
              options.push({
                name: key,
                description: tokens[1],
                default: tokens[2] || 'OFF',
                file: relPath(d),
              });
            }
          }
        }
      }
    } else if (stat.isDirectory()) {
      for (const entry of fs.readdirSync(d)) {
        if (entry.startsWith('.') || entry.startsWith('build')) continue;
        scan(path.join(d, entry));
      }
    }
  }

  function parseCmakeTokens(str) {
    const tokens = [];
    let current = '';
    let inQuote = false;
    for (const ch of str) {
      if (ch === '"') { inQuote = !inQuote; continue; }
      if (ch === ' ' && !inQuote) {
        if (current) tokens.push(current);
        current = '';
        continue;
      }
      current += ch;
    }
    if (current) tokens.push(current);
    return tokens;
  }

  scan(dir);
  return options;
}

// ---------------------------------------------------------------------------
// Build directory map
// ---------------------------------------------------------------------------

function buildDirectoryMap(baseDir, maxDepth, prefix) {
  const lines = [];
  const IGNORE = new Set([
    '.git', '.github', '.cursor', 'node_modules', '.cxx', 'venv',
    '__pycache__', '.gradle', '.idea', 'xcframework', 'dist',
  ]);

  function shouldSkip(name, fullPath) {
    if (name.startsWith('.')) return true;
    if (IGNORE.has(name)) return true;
    if (name.startsWith('build')) return true;
    if (fullPath.includes('_deps')) return true;
    return false;
  }

  function walk(dir, depth) {
    if (depth > maxDepth) return;
    const entries = listDir(dir).filter(e => !shouldSkip(e, path.join(dir, e)));

    for (const entry of entries) {
      const full = path.join(dir, entry);
      if (!isDir(full)) continue;

      const rel = path.relative(baseDir, full);
      const cCount = countFiles(full, '.c');
      const hCount = countFiles(full, '.h');
      const fileSummary = [];
      if (cCount > 0) fileSummary.push(`${cCount} .c`);
      if (hCount > 0) fileSummary.push(`${hCount} .h`);
      const countStr = fileSummary.length ? ` (${fileSummary.join(', ')})` : '';

      // Check for README.md or AGENTS.md for a description
      let desc = '';
      const agentsPath = path.join(full, 'AGENTS.md');
      const readmePath = path.join(full, 'README.md');
      if (fs.existsSync(agentsPath)) {
        const firstLine = readFile(agentsPath).split('\n').find(l => l.startsWith('# '));
        if (firstLine) desc = firstLine.replace(/^#\s+.*?-\s*/, '').trim();
      } else if (fs.existsSync(readmePath)) {
        const firstLine = readFile(readmePath).split('\n').find(l => l.startsWith('# '));
        if (firstLine) desc = firstLine.replace(/^#\s+/, '').trim();
      }

      const indent = '  '.repeat(depth);
      const descStr = desc ? ` -- ${desc}` : '';
      lines.push(`${indent}- \`${prefix}${rel}/\`${countStr}${descStr}`);

      walk(full, depth + 1);
    }
  }

  walk(baseDir, 0);
  return lines;
}

// ---------------------------------------------------------------------------
// Generate module index for a source directory
// ---------------------------------------------------------------------------

function generateModuleIndex(srcDir, headerFiles) {
  const lines = ['| File | Public Functions | Description |', '|------|----------------|-------------|'];

  for (const hFile of headerFiles) {
    const fullPath = path.join(srcDir, hFile);
    if (!fs.existsSync(fullPath)) continue;
    const funcs = parseHeaderFunctions(fullPath);
    if (funcs.length === 0) continue;

    const funcNames = funcs.map(f => '`' + f.name + '()`').join(', ');
    const desc = funcs[0] ? funcs[0].description : '';
    lines.push(`| \`${hFile}\` | ${funcNames} | ${desc} |`);
  }

  return lines;
}

// ---------------------------------------------------------------------------
// Collect all header files recursively
// ---------------------------------------------------------------------------

function findHeaders(dir, base) {
  let results = [];
  if (!fs.existsSync(dir)) return results;
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    const rel = path.relative(base, full);
    if (entry.isDirectory()) {
      results = results.concat(findHeaders(full, base));
    } else if (entry.name.endsWith('.h')) {
      results.push(rel);
    }
  }
  return results;
}

// ---------------------------------------------------------------------------
// Insert auto-generated content into AGENTS.md files
// ---------------------------------------------------------------------------

function updateAutoSection(filePath, sectionId, newContent) {
  if (!fs.existsSync(filePath)) {
    console.log(`  SKIP ${relPath(filePath)} (file not found)`);
    return;
  }

  const content = readFile(filePath);
  const startMarker = `<!-- AUTO:${sectionId}:START -->`;
  const endMarker = `<!-- AUTO:${sectionId}:END -->`;

  const startIdx = content.indexOf(startMarker);
  const endIdx = content.indexOf(endMarker);

  if (startIdx < 0 || endIdx < 0) {
    return; // No markers for this section
  }

  const before = content.substring(0, startIdx + startMarker.length);
  const after = content.substring(endIdx);
  const newSection = '\n' + newContent.join('\n') + '\n';

  const updated = before + newSection + after;
  if (updated !== content) {
    fs.writeFileSync(filePath, updated);
    console.log(`  UPDATED ${relPath(filePath)} [${sectionId}]`);
  } else {
    console.log(`  OK ${relPath(filePath)} [${sectionId}] (no changes)`);
  }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

function main() {
  console.log('Generating agent documentation...\n');

  // 1. Root AGENTS.md -- DIRECTORY_MAP
  console.log('1. Directory map...');
  const dirMap = buildDirectoryMap(ROOT, 2, '');
  updateAutoSection(path.join(ROOT, 'AGENTS.md'), 'DIRECTORY_MAP', dirMap);

  // 2. Root AGENTS.md -- CMAKE_OPTIONS
  console.log('2. CMake options...');
  const cmakeOpts = parseCmakeOptions(ROOT);
  const cmakeLines = [
    '',
    '### All CMake Options (auto-generated)',
    '',
    '| Option | Default | Description | Source |',
    '|--------|---------|-------------|--------|',
    ...cmakeOpts.sort((a, b) => a.name.localeCompare(b.name)).map(o =>
      `| \`${o.name}\` | ${o.default} | ${o.description} | ${o.file} |`
    ),
    '',
  ];
  updateAutoSection(path.join(ROOT, 'AGENTS.md'), 'CMAKE_OPTIONS', cmakeLines);

  // 3. Root AGENTS.md -- MODULE_INDEX
  console.log('3. Module index...');
  const moduleIndexLines = ['', '### Public API Index (auto-generated)', ''];
  const apiHeaders = [
    'verifier/verify.h',
    'prover/prover.h',
    'util/bytes.h',
    'util/ssz.h',
    'util/state.h',
    'util/crypto.h',
    'util/json.h',
    'util/plugin.h',
  ];
  for (const hdr of apiHeaders) {
    const fullPath = path.join(SRC, hdr);
    const funcs = parseHeaderFunctions(fullPath);
    if (funcs.length === 0) continue;
    moduleIndexLines.push(`**\`${hdr}\`**`);
    moduleIndexLines.push('');
    for (const f of funcs) {
      const descStr = f.description ? ` -- ${f.description}` : '';
      moduleIndexLines.push(`- \`${f.name}()\`${descStr}`);
    }
    moduleIndexLines.push('');
  }
  updateAutoSection(path.join(ROOT, 'AGENTS.md'), 'MODULE_INDEX', moduleIndexLines);

  // 4. src/AGENTS.md -- SRC_MODULE_INDEX
  console.log('4. Source module index...');
  const srcDirs = listDir(SRC).filter(d => isDir(path.join(SRC, d)) && !d.startsWith('.'));
  const srcModuleLines = ['', '### Source Modules (auto-generated)', ''];
  for (const dir of srcDirs) {
    const dirFull = path.join(SRC, dir);
    const cCount = countFiles(dirFull, '.c');
    const hCount = countFiles(dirFull, '.h');
    srcModuleLines.push(`- \`${dir}/\` -- ${cCount} .c, ${hCount} .h files`);
  }
  srcModuleLines.push('');
  updateAutoSection(path.join(SRC, 'AGENTS.md'), 'SRC_MODULE_INDEX', srcModuleLines);

  // 5. src/util/AGENTS.md -- UTIL_FILES
  console.log('5. Util files...');
  const utilHeaders = findHeaders(path.join(SRC, 'util'), path.join(SRC, 'util'));
  const utilLines = generateModuleIndex(path.join(SRC, 'util'), utilHeaders);
  updateAutoSection(path.join(SRC, 'util', 'AGENTS.md'), 'UTIL_FILES', ['', '### Public Functions (auto-generated)', '', ...utilLines, '']);

  // 6. src/chains/eth/AGENTS.md -- ETH_MODULE_INDEX
  console.log('6. ETH module index...');
  const ethHeaders = findHeaders(path.join(SRC, 'chains', 'eth'), path.join(SRC, 'chains', 'eth'));
  const ethLines = generateModuleIndex(path.join(SRC, 'chains', 'eth'), ethHeaders);
  updateAutoSection(path.join(SRC, 'chains', 'eth', 'AGENTS.md'), 'ETH_MODULE_INDEX', ['', '### Public Functions (auto-generated)', '', ...ethLines, '']);

  // 7. src/chains/op/AGENTS.md -- OP_MODULE_INDEX
  console.log('7. OP module index...');
  const opHeaders = findHeaders(path.join(SRC, 'chains', 'op'), path.join(SRC, 'chains', 'op'));
  const opLines = generateModuleIndex(path.join(SRC, 'chains', 'op'), opHeaders);
  updateAutoSection(path.join(SRC, 'chains', 'op', 'AGENTS.md'), 'OP_MODULE_INDEX', ['', '### Public Functions (auto-generated)', '', ...opLines, '']);

  // 8. src/server/AGENTS.md -- SERVER_FILES
  console.log('8. Server files...');
  const serverHeaders = findHeaders(path.join(SRC, 'server'), path.join(SRC, 'server'));
  const serverLines = generateModuleIndex(path.join(SRC, 'server'), serverHeaders);
  updateAutoSection(path.join(SRC, 'server', 'AGENTS.md'), 'SERVER_FILES', ['', '### Public Functions (auto-generated)', '', ...serverLines, '']);

  // 9. bindings/AGENTS.md -- BINDINGS_INDEX
  console.log('9. Bindings index...');
  const bindingsDir = path.join(ROOT, 'bindings');
  const bindingsDirs = listDir(bindingsDir).filter(d => isDir(path.join(bindingsDir, d)) && !d.startsWith('.'));
  const bindingsLines = ['', '### Binding Modules (auto-generated)', ''];
  for (const dir of bindingsDirs) {
    const dirFull = path.join(bindingsDir, dir);
    const fileCount = countFiles(dirFull);
    bindingsLines.push(`- \`${dir}/\` -- ${fileCount} files`);
  }
  bindingsLines.push('');
  updateAutoSection(path.join(bindingsDir, 'AGENTS.md'), 'BINDINGS_INDEX', bindingsLines);

  console.log('\nDone.');
}

main();
