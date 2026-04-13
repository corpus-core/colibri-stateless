/**
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

import type { SolidityStorageLayout } from './types.js';
import { getBundledCompiler } from './compiler.js';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type ASTNode = Record<string, any>;

const SOLIDITY_IDENTIFIER_RE = /^[a-zA-Z_$][a-zA-Z0-9_$]*$/;

function assertIdentifier(name: string): string {
    if (!SOLIDITY_IDENTIFIER_RE.test(name) || name.length > 128) {
        throw new Error(`Invalid Solidity identifier: ${name}`);
    }
    return name;
}

interface ContractSkeleton {
    name: string;
    kind: 'contract' | 'library' | 'interface';
    bases: string[];
    stateVars: string[];
}

/**
 * Extract the storage layout for a contract by building a minimal "skeleton"
 * from the original Solidity source and compiling it with the bundled solc.
 *
 * The skeleton contains only state variable declarations, struct/enum
 * definitions, and the inheritance chain. This works for **all** Solidity
 * versions because storage layout rules have been stable since 0.4.x.
 *
 * @param sources - Source files as returned by Sourcify `{ "file.sol": { content: "..." } }`
 * @param contractName - Target contract name (uses the last contract if omitted)
 * @return The solc-generated storage layout, or null on failure
 */
export async function extractStorageLayout(
    sources: Record<string, { content: string }>,
    contractName?: string,
): Promise<SolidityStorageLayout | null> {
    const parser = await import('@solidity-parser/parser');

    const structs: string[] = [];
    const enums: string[] = [];
    const contracts = new Map<string, ContractSkeleton>();
    let lastContractName = '';

    for (const [, source] of Object.entries(sources)) {
        let ast: ASTNode;
        try {
            ast = parser.parse(source.content, { tolerant: true, loc: false, range: false });
        } catch {
            continue;
        }

        for (const node of ast.children as ASTNode[]) {
            if (node.type === 'StructDefinition' && !node.isContractPart) {
                structs.push(emitStruct(node));
            }
            if (node.type === 'EnumDefinition' && !node.isContractPart) {
                enums.push(emitEnum(node));
            }
            if (node.type === 'ContractDefinition') {
                const skeleton = extractContractSkeleton(node);
                contracts.set(skeleton.name, skeleton);
                lastContractName = skeleton.name;

                for (const sub of node.subNodes as ASTNode[]) {
                    if (sub.type === 'StructDefinition') structs.push(emitStruct(sub));
                    if (sub.type === 'EnumDefinition') enums.push(emitEnum(sub));
                }
            }
        }
    }

    const target = contractName || lastContractName;
    if (!target || !contracts.has(target)) return null;

    const skeleton = buildSkeletonSource(target, contracts, structs, enums);
    return compileSkeleton(skeleton, target);
}

function extractContractSkeleton(node: ASTNode): ContractSkeleton {
    const bases = (node.baseContracts || []).map(
        (bc: ASTNode) => bc.baseName?.namePath || bc.baseName?.name || '',
    ).filter(Boolean) as string[];

    const stateVars: string[] = [];
    for (const sub of node.subNodes as ASTNode[]) {
        if (sub.type !== 'StateVariableDeclaration') continue;
        const v = sub.variables?.[0];
        if (!v) continue;
        if (v.isDeclaredConst || v.isImmutable) continue;

        const typeName = emitType(v.typeName);
        if (!typeName) continue;

        const visibility = v.visibility || 'internal';
        stateVars.push(`    ${typeName} ${visibility} ${assertIdentifier(v.name)};`);
    }

    const kind: ContractSkeleton['kind'] = node.kind === 'library' ? 'library'
        : node.kind === 'interface' ? 'interface'
        : 'contract';

    return { name: assertIdentifier(node.name), kind, bases, stateVars };
}

function emitType(typeNode: ASTNode): string {
    if (!typeNode) return '';

    switch (typeNode.type) {
        case 'ElementaryTypeName':
            return normalizeElementaryType(typeNode.name);
        case 'UserDefinedTypeName':
            return typeNode.namePath || typeNode.name || '';
        case 'ArrayTypeName': {
            const base = emitType(typeNode.baseTypeName);
            if (!base) return '';
            const len = typeNode.length?.number;
            return len ? `${base}[${len}]` : `${base}[]`;
        }
        case 'Mapping': {
            const key = emitType(typeNode.keyType);
            const value = emitType(typeNode.valueType);
            if (!key || !value) return '';
            return `mapping(${key} => ${value})`;
        }
        default:
            return '';
    }
}

function normalizeElementaryType(name: string): string {
    if (name === 'uint') return 'uint256';
    if (name === 'int') return 'int256';
    if (name === 'byte') return 'bytes1';
    return name;
}

function emitStruct(node: ASTNode): string {
    const members = (node.members || [])
        .map((m: ASTNode) => `    ${emitType(m.typeName)} ${assertIdentifier(m.name)};`)
        .filter((s: string) => s.trim().length > 1);
    return `struct ${assertIdentifier(node.name)} {\n${members.join('\n')}\n}`;
}

function emitEnum(node: ASTNode): string {
    const values = (node.members || []).map((m: ASTNode) => assertIdentifier(m.name)).join(', ');
    return `enum ${assertIdentifier(node.name)} { ${values} }`;
}

function buildSkeletonSource(
    target: string,
    contracts: Map<string, ContractSkeleton>,
    structs: string[],
    enums: string[],
): string {
    const lines: string[] = ['// SPDX-License-Identifier: MIT', 'pragma solidity >=0.8.0;', ''];

    for (const s of structs) lines.push(s, '');
    for (const e of enums) lines.push(e, '');

    const emitted = new Set<string>();
    emitContract(target, contracts, lines, emitted);

    return lines.join('\n');
}

function emitContract(
    name: string,
    contracts: Map<string, ContractSkeleton>,
    lines: string[],
    emitted: Set<string>,
): void {
    if (emitted.has(name)) return;
    emitted.add(name);

    const skeleton = contracts.get(name);
    if (!skeleton) return;

    for (const base of skeleton.bases) {
        emitContract(base, contracts, lines, emitted);
    }

    const keyword = skeleton.kind;
    const inheritance = skeleton.bases.length > 0
        ? ` is ${skeleton.bases.join(', ')}`
        : '';

    if (skeleton.stateVars.length === 0) {
        lines.push(`${keyword} ${skeleton.name}${inheritance} {}`, '');
    } else {
        lines.push(`${keyword} ${skeleton.name}${inheritance} {`);
        for (const v of skeleton.stateVars) lines.push(v);
        lines.push('}', '');
    }
}

async function compileSkeleton(
    source: string,
    contractName: string,
): Promise<SolidityStorageLayout | null> {
    const compiler = await getBundledCompiler();

    const input = JSON.stringify({
        language: 'Solidity',
        sources: { 'skeleton.sol': { content: source } },
        settings: {
            outputSelection: { '*': { '*': ['storageLayout'] } },
        },
    });

    let output: Record<string, unknown>;
    try {
        output = JSON.parse(compiler.compile(input));
    } catch {
        return null;
    }

    const errors = output.errors as Array<{ severity: string; message: string }> | undefined;
    const hasErrors = errors?.some(e => e.severity === 'error');
    if (hasErrors) return null;

    const contracts = output.contracts as Record<string, Record<string, Record<string, unknown>>> | undefined;
    if (!contracts) return null;

    for (const file of Object.values(contracts)) {
        const contractOutput = file[contractName];
        if (contractOutput?.storageLayout) {
            const layout = contractOutput.storageLayout as SolidityStorageLayout;
            if (layout.storage) return layout;
        }
    }

    return null;
}
