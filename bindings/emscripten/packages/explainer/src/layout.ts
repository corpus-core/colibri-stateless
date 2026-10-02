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
 * definitions, user-defined value types (`type Timestamp is uint64`), and the
 * inheritance chain. This works for **all** Solidity versions because storage
 * layout rules have been stable since 0.4.x. Value types are required so a
 * packed slot such as `Timestamp` + `enum` + `bool` still compiles; without
 * them the skeleton fails and the slot stays unnamed. An empty library whose
 * name is already a value type, struct, or enum is omitted (`type Address is
 * bytes32` inside `library types` must not also emit OpenZeppelin
 * `library Address`). Constants of the form `types.Uint256.wrap(0x…)` are
 * explicit slot pointers and are added even when the skeleton has no state
 * variables.
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
    const valueTypes = new Map<string, string>();
    const contracts = new Map<string, ContractSkeleton>();
    const locationConstants = new Map<string, bigint>();
    const slotPointers = new Map<string, SlotPointer>();
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
            if (node.type === 'TypeDefinition') rememberValueType(valueTypes, node);
            if (node.type === 'ContractDefinition') {
                const skeleton = extractContractSkeleton(node);
                contracts.set(skeleton.name, skeleton);
                lastContractName = skeleton.name;

                for (const sub of node.subNodes as ASTNode[]) {
                    if (sub.type === 'StructDefinition') rememberStruct(structs, sub);
                    if (sub.type === 'EnumDefinition') rememberType(enums, sub.name, emitEnum(sub));
                    if (sub.type === 'TypeDefinition') rememberValueType(valueTypes, sub);
                    rememberLocationConstant(locationConstants, sub);
                    rememberSlotPointer(slotPointers, sub, skeleton.name);
                }
            }
        }
    }

    explainerLog('debug', 'sources parsed', {
        scope: 'layout', ms: elapsedMs(parseStarted), contracts: contracts.size, files: fileCount,
    });

    const target = contractName || lastContractName;
    if (!target || !contracts.has(target)) return null;

    const occupied = new Set<string>([...valueTypes.keys(), ...structs.keys(), ...enums.keys()]);
    const skeleton = buildSkeletonSource(
        target, contracts, orderStructs(structs), [...enums.values()], [...valueTypes.values()], occupied,
    );
    const compiled = await compileSkeleton(skeleton, target);
    const namespaced = buildNamespacedLayout(structs, locationConstants);
    const pointed = buildSlotPointerLayout(slotPointers, structs);
    return mergeLayouts(mergeLayouts(compiled, namespaced), pointed);
}

interface StructSkeleton {
    source: string;
    deps: string[];
    members: ASTNode[];
}

const VALUE_TYPE_UNDERLYING_RE = /^(?:address|bool|uint(?:8|16|32|64|128|256)|int(?:8|16|32|64|128|256)|bytes(?:[1-9]|1[0-9]|2[0-9]|3[0-2]))$/;

/**
 * Record `type Timestamp is uint64` so the skeleton can name that alias.
 *
 * Solidity user-defined value types occupy the same slots as their underlying
 * value type. The underlying type is taken from the AST, not from source text.
 *
 * @param types - Name → `type Name is Underlying;`
 * @param node - `TypeDefinition` AST node
 */
function rememberValueType(types: Map<string, string>, node: ASTNode): void {
    if (typeof node.name !== 'string') return;
    if (!SOLIDITY_IDENTIFIER_RE.test(node.name) || node.name.length > 128) return;
    if (types.has(node.name)) return;
    const underlying = emitType(node.definition);
    if (!underlying || !VALUE_TYPE_UNDERLYING_RE.test(underlying)) return;
    types.set(node.name, `type ${node.name} is ${underlying};`);
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
 * Record `bytes32 constant FooStorageLocation = 0x…32` (also `FOO_STORAGE_LOCATION`) for ERC-7201 namespaces.
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

interface SlotPointer {
    name: string;
    slot: bigint;
    contract: string;
    /** Flattened user-defined type, e.g. `Uint256`, `BalanceMapping`, `TicketArray`. */
    typeName: string;
}

/**
 * Read `Type.wrap(0x{64 hex})` from a constant initializer.
 *
 * Kiln-style slots are user-defined value types over `bytes32`, initialized
 * with `.wrap`, not a bare `bytes32` literal.
 *
 * @param expr - Variable initializer AST
 * @return 32-byte hex literal, or `null` when the expression is not a wrap call
 */
function wrapSlotLiteral(expr: ASTNode | undefined): string | null {
    if (!expr || expr.type !== 'FunctionCall') return null;
    const callee = expr.expression;
    if (!callee || callee.type !== 'MemberAccess' || callee.memberName !== 'wrap') return null;
    if (!Array.isArray(expr.arguments) || expr.arguments.length !== 1) return null;
    const raw = expr.arguments[0]?.number;
    if (typeof raw !== 'string' || !/^0x[0-9a-fA-F]{64}$/i.test(raw)) return null;
    return raw;
}

/**
 * Record `types.Uint256 internal constant $name = types.Uint256.wrap(0x…)`.
 *
 * The constant is the storage slot itself. The first declaration of a slot wins.
 *
 * @param pointers - Slot hex → pointer
 * @param node - Contract sub-node
 * @param contract - Contract that declares the constant
 */
function rememberSlotPointer(pointers: Map<string, SlotPointer>, node: ASTNode, contract: string): void {
    if (node.type !== 'StateVariableDeclaration') return;
    const variable = node.variables?.[0];
    if (!variable?.isDeclaredConst || typeof variable.name !== 'string') return;
    const raw = wrapSlotLiteral(variable.expression) ?? wrapSlotLiteral(node.initialValue);
    if (!raw) return;
    if (variable.typeName?.type !== 'UserDefinedTypeName') return;
    const typeName = flattenUserDefinedType(rawUserDefinedName(variable.typeName));
    if (!typeName) return;
    const key = raw.toLowerCase();
    if (pointers.has(key)) return;
    pointers.set(key, {
        name: assertIdentifier(variable.name),
        slot: BigInt(raw),
        contract,
        typeName,
    });
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
 * Fold a Solidity identifier so `ERC20Storage`, `ERC20StorageLocation`, and
 * `ERC20_STORAGE_LOCATION` share one key.
 *
 * @param name - Struct or constant identifier
 * @return Lowercase name with underscores removed
 */
function normalizeStorageIdent(name: string): string {
    return name.toLowerCase().replace(/_/g, '');
}

/**
 * Prefix that links an ERC-7201 location constant to its struct.
 *
 * `ERC20StorageLocation` and `ERC20_STORAGE_LOCATION` both yield `erc20`.
 * Constants that do not end in `StorageLocation` are ignored.
 *
 * @param constName - `bytes32` constant identifier
 * @return Prefix, or `null` when the name is not a storage-location constant
 */
function storageLocationPrefix(constName: string): string | null {
    const normalized = normalizeStorageIdent(constName);
    const suffix = 'storagelocation';
    if (!normalized.endsWith(suffix)) return null;
    const prefix = normalized.slice(0, -suffix.length);
    return prefix.length > 0 ? prefix : null;
}

/**
 * Prefix of an ERC-7201 struct (`ERC20Storage` → `erc20`).
 *
 * @param structName - Struct identifier
 * @return Prefix, or `null` when the name does not end in `Storage`
 */
function storageStructPrefix(structName: string): string | null {
    const normalized = normalizeStorageIdent(structName);
    const suffix = 'storage';
    if (!normalized.endsWith(suffix)) return null;
    const prefix = normalized.slice(0, -suffix.length);
    return prefix.length > 0 ? prefix : null;
}

/**
 * Turn ERC-7201 structs into layout entries at `location + memberSlot`.
 *
 * A location constant is paired with the struct that shares its prefix:
 * `ERC20StorageLocation` and `ERC20_STORAGE_LOCATION` both match `ERC20Storage`.
 * The first struct wins when two names fold to the same prefix.
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
    const structsByPrefix = new Map<string, { name: string; skeleton: StructSkeleton }>();

    for (const [name, skeleton] of structs) {
        const prefix = storageStructPrefix(name);
        if (!prefix || structsByPrefix.has(prefix)) continue;
        structsByPrefix.set(prefix, { name, skeleton });
    }

    for (const [constName, base] of constants) {
        const prefix = storageLocationPrefix(constName);
        if (!prefix) continue;
        const match = structsByPrefix.get(prefix);
        if (!match?.skeleton.members.length) continue;
        appendNamespacedMembers(match.skeleton.members, match.name, base, storage, types);
    }

    return storage.length ? { storage, types } : null;
}

/**
 * True when a type occupies a whole storage slot and cannot be packed.
 *
 * @param info - solc-like type descriptor
 * @return Whether the next member must start at a fresh slot
 */
function occupiesFullSlot(info: SolidityStorageType): boolean {
    const bytes = Number(info.numberOfBytes) || 32;
    return info.encoding === 'mapping' || info.encoding === 'bytes'
        || info.encoding === 'dynamic_array' || bytes >= 32;
}

/**
 * Build a struct type from a parsed struct so an array of that struct has the
 * right element stride (`Ticket` is two slots: two packed `uint128` plus one more).
 *
 * @param name - Struct identifier
 * @param skeleton - Parsed members
 * @param types - Type map to extend
 * @return Type id
 */
function ensureStructType(
    name: string,
    skeleton: StructSkeleton,
    types: Record<string, SolidityStorageType>,
): string {
    const id = `t_struct_${name}`;
    if (types[id]) return id;

    const members: SolidityStorageEntry[] = [];
    let slot = 0n;
    let offset = 0;
    for (const member of skeleton.members) {
        const typeId = synthesizeType(member.typeName, types);
        if (!typeId) continue;
        const info = types[typeId];
        if (!info) continue;
        const bytes = Number(info.numberOfBytes) || 32;
        if (occupiesFullSlot(info)) {
            if (offset > 0) {
                slot += 1n;
                offset = 0;
            }
            members.push({
                slot: slot.toString(), type: typeId, astId: 0, label: member.name, offset: 0, contract: name,
            });
            slot += 1n;
            continue;
        }
        if (offset + bytes > 32) {
            slot += 1n;
            offset = 0;
        }
        members.push({
            slot: slot.toString(), type: typeId, astId: 0, label: member.name, offset, contract: name,
        });
        offset += bytes;
        if (offset >= 32) {
            slot += 1n;
            offset = 0;
        }
    }

    const span = offset > 0 ? slot + 1n : slot;
    types[id] = {
        label: name,
        encoding: 'inplace',
        numberOfBytes: String((span > 0n ? span : 1n) * 32n),
        members,
    };
    return id;
}

/**
 * Turn `Type.wrap(0x…)` constants into layout entries at that exact slot.
 *
 * `TicketArray` uses struct `Ticket` when that struct was parsed, so dynamic
 * array data is stepped by the struct's slot span. Other names become a
 * full-word value, a dynamic `uint256[]`, or a mapping. The variable name is
 * kept, including a leading `$`.
 *
 * @param pointers - Collected slot pointers
 * @param structs - Parsed structs, used for `*Array` element types
 * @return Layout, or `null` when nothing was collected
 */
function buildSlotPointerLayout(
    pointers: Map<string, SlotPointer>,
    structs: Map<string, StructSkeleton>,
): SolidityStorageLayout | null {
    if (pointers.size === 0) return null;
    const storage: SolidityStorageEntry[] = [];
    const types: Record<string, SolidityStorageType> = {};

    for (const pointer of pointers.values()) {
        const typeId = slotPointerTypeId(pointer.typeName, structs, types);
        if (!typeId || !types[typeId]) continue;
        storage.push({
            slot: pointer.slot.toString(),
            type: typeId,
            astId: 0,
            label: pointer.name,
            offset: 0,
            contract: pointer.contract,
        });
    }

    return storage.length ? { storage, types } : null;
}

/**
 * solc-like type id for a slot-pointer value type.
 *
 * @param typeName - Flattened type (`Uint256`, `TicketArray`, `BalanceMapping`)
 * @param structs - Parsed structs
 * @param types - Type map to extend
 * @return Type id
 */
function slotPointerTypeId(
    typeName: string,
    structs: Map<string, StructSkeleton>,
    types: Record<string, SolidityStorageType>,
): string {
    const structArray = typeName.endsWith('Array') && typeName !== 'Array'
        ? typeName.slice(0, -'Array'.length)
        : '';
    const structSkeleton = structArray ? structs.get(structArray) : undefined;
    if (structSkeleton) {
        const base = ensureStructType(structArray, structSkeleton, types);
        const id = `t_array(${base})dyn`;
        if (!types[id]) {
            types[id] = {
                label: `${structArray}[]`,
                encoding: 'dynamic_array',
                numberOfBytes: '32',
                base,
            };
        }
        return id;
    }

    if (typeName === 'Array' || typeName.endsWith('Array')) {
        const base = 't_uint256';
        if (!types[base]) types[base] = elementaryTypeInfo('uint256');
        const id = `t_array(${base})dyn`;
        if (!types[id]) {
            types[id] = {
                label: 'uint256[]',
                encoding: 'dynamic_array',
                numberOfBytes: '32',
                base,
            };
        }
        return id;
    }

    if (typeName === 'Mapping' || typeName.endsWith('Mapping')) {
        const key = 't_bytes32';
        const value = 't_bytes32';
        if (!types[key]) types[key] = elementaryTypeInfo('bytes32');
        const id = `t_mapping(${key},${value})`;
        if (!types[id]) {
            types[id] = {
                label: 'mapping(bytes32 => bytes32)',
                encoding: 'mapping',
                numberOfBytes: '32',
                key,
                value,
            };
        }
        return id;
    }

    const elementary = slotPointerElementary(typeName);
    const id = `t_${elementary}`;
    if (!types[id]) types[id] = elementaryTypeInfo(elementary);
    return id;
}

/**
 * Elementary type used for a non-mapping, non-array slot pointer.
 *
 * @param typeName - Flattened value-type name
 * @return Normalized elementary type
 */
function slotPointerElementary(typeName: string): string {
    if (typeName === 'Address') return 'address';
    if (typeName === 'Bool') return 'bool';
    if (typeName === 'String' || typeName === 'Bytes') return 'bytes';
    if (typeName === 'Bytes32') return 'bytes32';
    if (typeName === 'Uint256') return 'uint256';
    return 'bytes32';
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
    valueTypes: string[] = [],
    occupied: ReadonlySet<string> = new Set(),
): string {
    const lines: string[] = ['// SPDX-License-Identifier: MIT', 'pragma solidity >=0.8.0;', ''];

    for (const e of enums) lines.push(e, '');
    // After enums: a value type may alias an elementary type used by a struct.
    for (const valueType of valueTypes) lines.push(valueType, '');
    for (const s of structs) lines.push(s, '');

    const emitted = new Set<string>();
    // Interfaces/libraries first so contract-typed state vars (e.g. `IUniswapV2Router02`)
    // have a declaration. Inheritance-only emission used to drop those and solc
    // then failed the skeleton with a missing identifier.
    for (const [name, sk] of contracts) {
        if (sk.kind === 'interface' || sk.kind === 'library') {
            emitContract(name, contracts, lines, emitted, occupied);
        }
    }
    emitContract(target, contracts, lines, emitted, occupied);
    for (const name of contracts.keys()) {
        emitContract(name, contracts, lines, emitted, occupied);
    }

    return lines.join('\n');
}

function emitContract(
    name: string,
    contracts: Map<string, ContractSkeleton>,
    lines: string[],
    emitted: Set<string>,
    occupied: ReadonlySet<string>,
): void {
    if (emitted.has(name)) return;
    emitted.add(name);

    const skeleton = contracts.get(name);
    if (!skeleton) return;

    // A library is not a storage type. Flattened value types such as
    // `type Address is bytes32` share the file scope with OpenZeppelin's
    // `library Address`, and emitting both makes solc reject the skeleton.
    if (skeleton.kind === 'library' && occupied.has(name)) return;

    for (const base of skeleton.bases) {
        emitContract(base, contracts, lines, emitted, occupied);
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
        // The browser compiler only exposes `compileAsync` (worker). Node's
        // bundled solc compiles synchronously.
        const raw = compiler.compileAsync
            ? await compiler.compileAsync(input)
            : compiler.compile(input);
        output = JSON.parse(raw);
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
