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

import type { SolidityStorageEntry, SolidityStorageLayout, SolidityStorageType } from './types.js';
import { getBundledCompiler } from './compiler.js';
import { elapsedMs, explainerLog } from './log.js';

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
    const fileCount = Object.keys(sources).length;
    explainerLog('debug', 'parse sources for layout', {
        scope: 'layout', files: fileCount, contract: contractName,
    });
    const parseStarted = Date.now();
    const parser = await import('@solidity-parser/parser');

    const structs = new Map<string, StructSkeleton>();
    const enums = new Map<string, string>();
    const contracts = new Map<string, ContractSkeleton>();
    const locationConstants = new Map<string, bigint>();
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
                rememberStruct(structs, node);
            }
            if (node.type === 'EnumDefinition' && !node.isContractPart) {
                rememberType(enums, node.name, emitEnum(node));
            }
            if (node.type === 'ContractDefinition') {
                const skeleton = extractContractSkeleton(node);
                contracts.set(skeleton.name, skeleton);
                lastContractName = skeleton.name;

                for (const sub of node.subNodes as ASTNode[]) {
                    if (sub.type === 'StructDefinition') rememberStruct(structs, sub);
                    if (sub.type === 'EnumDefinition') rememberType(enums, sub.name, emitEnum(sub));
                    rememberLocationConstant(locationConstants, sub);
                }
            }
        }
    }

    explainerLog('debug', 'sources parsed', {
        scope: 'layout', ms: elapsedMs(parseStarted), contracts: contracts.size, files: fileCount,
    });

    const target = contractName || lastContractName;
    if (!target || !contracts.has(target)) return null;

    const skeleton = buildSkeletonSource(
        target, contracts, orderStructs(structs), [...enums.values()],
    );
    const compiled = await compileSkeleton(skeleton, target);
    const namespaced = buildNamespacedLayout(structs, locationConstants);
    return mergeLayouts(compiled, namespaced);
}

interface StructSkeleton {
    source: string;
    deps: string[];
    members: ASTNode[];
}

/**
 * Keep the first definition of a flattened struct or enum. Duplicate names
 * appear when the same OpenZeppelin file is present twice (upgradeable +
 * non-upgradeable).
 *
 * @param types - Name → skeleton source
 * @param name - Type identifier
 * @param source - Emitted Solidity
 */
function rememberType(types: Map<string, string>, name: string, source: string): void {
    const key = assertIdentifier(name);
    if (!types.has(key)) types.set(key, source);
}

/**
 * Record a struct once, including names of other user-defined types it
 * references so emission can be ordered.
 *
 * @param structs - Name → skeleton + dependencies
 * @param node - Parser struct node
 */
function rememberStruct(structs: Map<string, StructSkeleton>, node: ASTNode): void {
    const key = assertIdentifier(node.name);
    if (structs.has(key)) return;
    const deps = new Set<string>();
    for (const member of node.members || []) {
        collectNamedTypeRefs(member.typeName, deps);
    }
    structs.set(key, {
        source: emitStruct(node),
        deps: [...deps],
        members: (node.members || []).filter((m: ASTNode) => m?.name && m.typeName),
    });
}

/**
 * Record `bytes32 constant FooStorageLocation = 0x…32` for ERC-7201 namespaces.
 *
 * @param constants - Constant name → slot
 * @param node - Contract sub-node
 */
function rememberLocationConstant(constants: Map<string, bigint>, node: ASTNode): void {
    if (node.type !== 'StateVariableDeclaration') return;
    const variable = node.variables?.[0];
    if (!variable?.isDeclaredConst || !variable.name) return;
    if (variable.typeName?.name !== 'bytes32') return;
    const raw = variable.expression?.number;
    if (typeof raw !== 'string' || !/^0x[0-9a-fA-F]{64}$/i.test(raw)) return;
    const name = assertIdentifier(variable.name);
    if (!constants.has(name)) constants.set(name, BigInt(raw));
}

/**
 * Emit structs so that referenced types appear first (`LimitConfig` before
 * `Storage { LimitConfig limitConfig; }`).
 *
 * @param structs - Collected struct skeletons
 * @return Solidity struct definitions in dependency order
 */
function orderStructs(structs: Map<string, StructSkeleton>): string[] {
    const remaining = new Set(structs.keys());
    const out: string[] = [];

    while (remaining.size > 0) {
        const ready = [...remaining].filter(name => {
            const info = structs.get(name);
            if (!info) return true;
            return info.deps.every(dep => dep === name || !remaining.has(dep));
        });
        if (ready.length === 0) {
            for (const name of remaining) {
                const info = structs.get(name);
                if (info) out.push(info.source);
            }
            break;
        }
        for (const name of ready) {
            const info = structs.get(name);
            if (info) out.push(info.source);
            remaining.delete(name);
        }
    }
    return out;
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

        stateVars.push(`    ${typeName} ${emitVisibility(v.visibility)} ${assertIdentifier(v.name)};`);
    }

    const kind: ContractSkeleton['kind'] = node.kind === 'library' ? 'library'
        : node.kind === 'interface' ? 'interface'
            : 'contract';

    return { name: assertIdentifier(node.name), kind, bases, stateVars };
}

const STATE_VAR_VISIBILITIES = new Set(['public', 'private', 'internal']);

/**
 * Map a parser visibility to a Solidity state-variable specifier.
 *
 * `@solidity-parser/parser` reports omitted visibility as `"default"`, which is
 * a reserved keyword and not valid syntax. Solidity's default for state
 * variables is `internal`.
 *
 * @param raw - Parser `visibility` field
 * @return `public`, `private`, or `internal`
 */
function emitVisibility(raw?: string): string {
    if (raw && STATE_VAR_VISIBILITIES.has(raw)) return raw;
    return 'internal';
}

function emitType(typeNode: ASTNode): string {
    if (!typeNode) return '';

    switch (typeNode.type) {
        case 'ElementaryTypeName':
            return normalizeElementaryType(typeNode.name);
        case 'UserDefinedTypeName':
            return flattenUserDefinedType(rawUserDefinedName(typeNode));
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

function rawUserDefinedName(typeNode: ASTNode): string {
    const path = typeNode.namePath;
    if (typeof path === 'string' && path) return path;
    if (Array.isArray(path) && path.length) return path.map(String).join('.');
    if (typeof typeNode.name === 'string' && typeNode.name) return typeNode.name;
    return '';
}

/**
 * Skeleton structs are file-scope, so `RateLimit.Storage` becomes `Storage`.
 *
 * @param raw - Parser `namePath` (`Library.Type` or a bare identifier)
 * @return Last identifier segment, or empty if invalid
 */
function flattenUserDefinedType(raw: string): string {
    if (!raw) return '';
    const last = raw.includes('.') ? raw.slice(raw.lastIndexOf('.') + 1) : raw;
    if (!SOLIDITY_IDENTIFIER_RE.test(last) || last.length > 128) return '';
    return last;
}

/**
 * Collect flattened user-defined type names referenced by a type node.
 *
 * @param typeNode - Parser type AST
 * @param into - Destination set
 */
function collectNamedTypeRefs(typeNode: ASTNode | undefined, into: Set<string>): void {
    if (!typeNode) return;
    switch (typeNode.type) {
        case 'UserDefinedTypeName': {
            const name = flattenUserDefinedType(rawUserDefinedName(typeNode));
            if (name) into.add(name);
            return;
        }
        case 'ArrayTypeName':
            collectNamedTypeRefs(typeNode.baseTypeName, into);
            return;
        case 'Mapping':
            collectNamedTypeRefs(typeNode.keyType, into);
            collectNamedTypeRefs(typeNode.valueType, into);
            return;
        default:
            return;
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

/**
 * Merge a compiled skeleton layout with ERC-7201 namespaced struct fields.
 *
 * @param compiled - solc layout, or `null` if the skeleton failed
 * @param namespaced - Synthesized layout from `FooStorageLocation` constants
 * @return Combined layout, or `null` when both sides are empty
 */
function mergeLayouts(
    compiled: SolidityStorageLayout | null,
    namespaced: SolidityStorageLayout | null,
): SolidityStorageLayout | null {
    if (!compiled?.storage?.length) return namespaced;
    if (!namespaced?.storage?.length) return compiled;
    return {
        storage: [...compiled.storage, ...namespaced.storage],
        types: { ...(compiled.types ?? {}), ...(namespaced.types ?? {}) },
    };
}

/**
 * Turn ERC-7201 structs (`bytes32 constant FooStorageLocation = 0x…`) into
 * layout entries at `location + memberSlot`.
 *
 * @param structs - Parsed structs
 * @param constants - bytes32 location constants
 * @return Layout, or `null` when nothing matched
 */
function buildNamespacedLayout(
    structs: Map<string, StructSkeleton>,
    constants: Map<string, bigint>,
): SolidityStorageLayout | null {
    const storage: SolidityStorageEntry[] = [];
    const types: Record<string, SolidityStorageType> = {};

    for (const [constName, base] of constants) {
        if (!constName.endsWith('Location')) continue;
        const structName = constName.slice(0, -'Location'.length);
        const skeleton = structs.get(structName);
        if (!skeleton?.members.length) continue;
        appendNamespacedMembers(skeleton.members, structName, base, storage, types);
    }

    return storage.length ? { storage, types } : null;
}

/**
 * Append packed struct members starting at `baseSlot`.
 *
 * @param members - Parser struct members
 * @param contract - Struct name (layout `contract` field)
 * @param baseSlot - ERC-7201 namespace slot
 * @param storage - Destination entries
 * @param types - Destination type map
 */
function appendNamespacedMembers(
    members: ASTNode[],
    contract: string,
    baseSlot: bigint,
    storage: SolidityStorageEntry[],
    types: Record<string, SolidityStorageType>,
): void {
    let slot = 0n;
    let offset = 0;

    for (const member of members) {
        const typeId = synthesizeType(member.typeName, types);
        if (!typeId) continue;
        const info = types[typeId];
        if (!info) continue;

        const bytes = Number(info.numberOfBytes) || 32;
        const fullSlot = info.encoding === 'mapping' || info.encoding === 'bytes'
            || info.encoding === 'dynamic_array' || bytes >= 32;

        if (fullSlot) {
            if (offset > 0) {
                slot += 1n;
                offset = 0;
            }
            storage.push({
                slot: (baseSlot + slot).toString(),
                type: typeId,
                astId: 0,
                label: member.name,
                offset: 0,
                contract,
            });
            slot += 1n;
            continue;
        }

        if (offset + bytes > 32) {
            slot += 1n;
            offset = 0;
        }
        storage.push({
            slot: (baseSlot + slot).toString(),
            type: typeId,
            astId: 0,
            label: member.name,
            offset,
            contract,
        });
        offset += bytes;
        if (offset >= 32) {
            slot += 1n;
            offset = 0;
        }
    }
}

/**
 * Assign a solc-like type id and record it in `types`.
 *
 * @param typeNode - Parser type AST
 * @param types - Type map to extend
 * @return Type id, or empty if the type cannot be represented
 */
function synthesizeType(typeNode: ASTNode | undefined, types: Record<string, SolidityStorageType>): string {
    if (!typeNode) return '';

    switch (typeNode.type) {
        case 'ElementaryTypeName': {
            const name = normalizeElementaryType(typeNode.name);
            const id = `t_${name}`;
            if (!types[id]) types[id] = elementaryTypeInfo(name);
            return id;
        }
        case 'Mapping': {
            const key = synthesizeType(typeNode.keyType, types);
            const value = synthesizeType(typeNode.valueType, types);
            if (!key || !value) return '';
            const id = `t_mapping(${key},${value})`;
            if (!types[id]) {
                const keyLabel = types[key]?.label ?? key;
                const valueLabel = types[value]?.label ?? value;
                types[id] = {
                    label: `mapping(${keyLabel} => ${valueLabel})`,
                    encoding: 'mapping',
                    numberOfBytes: '32',
                    key,
                    value,
                };
            }
            return id;
        }
        case 'ArrayTypeName': {
            const base = synthesizeType(typeNode.baseTypeName, types);
            if (!base) return '';
            const len = typeNode.length?.number;
            if (len) {
                const id = `t_array(${base})${len}`;
                const baseBytes = Number(types[base]?.numberOfBytes) || 32;
                if (!types[id]) {
                    types[id] = {
                        label: `${types[base]?.label ?? base}[${len}]`,
                        encoding: 'inplace',
                        numberOfBytes: String(baseBytes * Number(len)),
                        base,
                    };
                }
                return id;
            }
            const id = `t_array(${base})dyn`;
            if (!types[id]) {
                types[id] = {
                    label: `${types[base]?.label ?? base}[]`,
                    encoding: 'dynamic_array',
                    numberOfBytes: '32',
                    base,
                };
            }
            return id;
        }
        case 'UserDefinedTypeName': {
            const name = flattenUserDefinedType(rawUserDefinedName(typeNode));
            if (!name) return '';
            const id = `t_user_${name}`;
            if (!types[id]) {
                types[id] = { label: name, encoding: 'inplace', numberOfBytes: '20' };
            }
            return id;
        }
        default:
            return '';
    }
}

/**
 * solc-style type metadata for an elementary Solidity type.
 *
 * @param name - Normalized type name (`uint256`, `address`, …)
 * @return Type descriptor
 */
function elementaryTypeInfo(name: string): SolidityStorageType {
    if (name === 'string' || name === 'bytes') {
        return { label: name, encoding: 'bytes', numberOfBytes: '32' };
    }
    if (name === 'address') {
        return { label: 'address', encoding: 'inplace', numberOfBytes: '20' };
    }
    if (name === 'bool') {
        return { label: 'bool', encoding: 'inplace', numberOfBytes: '1' };
    }
    const uintMatch = /^uint(\d+)$/.exec(name);
    if (uintMatch) {
        return { label: name, encoding: 'inplace', numberOfBytes: String(Number(uintMatch[1]) / 8) };
    }
    const intMatch = /^int(\d+)$/.exec(name);
    if (intMatch) {
        return { label: name, encoding: 'inplace', numberOfBytes: String(Number(intMatch[1]) / 8) };
    }
    const bytesMatch = /^bytes(\d+)$/.exec(name);
    if (bytesMatch) {
        return { label: name, encoding: 'inplace', numberOfBytes: bytesMatch[1] };
    }
    return { label: name, encoding: 'inplace', numberOfBytes: '32' };
}

function buildSkeletonSource(
    target: string,
    contracts: Map<string, ContractSkeleton>,
    structs: string[],
    enums: string[],
): string {
    const lines: string[] = ['// SPDX-License-Identifier: MIT', 'pragma solidity >=0.8.0;', ''];

    for (const e of enums) lines.push(e, '');
    for (const s of structs) lines.push(s, '');

    const emitted = new Set<string>();
    // Interfaces/libraries first so contract-typed state vars (e.g. `IUniswapV2Router02`)
    // have a declaration. Inheritance-only emission used to drop those and solc
    // then failed the skeleton with a missing identifier.
    for (const [name, sk] of contracts) {
        if (sk.kind === 'interface' || sk.kind === 'library') {
            emitContract(name, contracts, lines, emitted);
        }
    }
    emitContract(target, contracts, lines, emitted);
    for (const name of contracts.keys()) {
        emitContract(name, contracts, lines, emitted);
    }

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

/**
 * Compile a storage-only skeleton with the bundled solc.
 *
 * @param source - Skeleton Solidity source
 * @param contractName - Contract to read `storageLayout` from
 * @return Layout, or `null` if compilation fails
 */
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

    explainerLog('debug', 'compile skeleton', { scope: 'layout', contract: contractName });
    const started = Date.now();
    let output: Record<string, unknown>;
    try {
        output = JSON.parse(compiler.compile(input));
    } catch (err) {
        explainerLog('warn', 'skeleton compile failed', {
            scope: 'layout',
            contract: contractName,
            error: err instanceof Error ? err.message : String(err),
        });
        return null;
    }
    explainerLog('debug', 'skeleton compiled', { scope: 'layout', contract: contractName, ms: elapsedMs(started) });

    const errors = output.errors as Array<{ severity: string; message?: string; formattedMessage?: string }> | undefined;
    const compileErrors = errors?.filter(e => e.severity === 'error') ?? [];
    if (compileErrors.length > 0) {
        const first = compileErrors[0];
        explainerLog('warn', 'skeleton compile errors', {
            scope: 'layout',
            contract: contractName,
            error: first.formattedMessage ?? first.message,
            count: compileErrors.length,
        });
        return null;
    }

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
