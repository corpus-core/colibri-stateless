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

import { parse } from '@solidity-parser/parser';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type AstNode = Record<string, any>;

export interface SourceSlice {
    filename: string;
    kind: 'function' | 'modifier' | 'state' | 'enum' | 'struct';
    name: string;
    /** Comment-stripped source of this definition. */
    text: string;
    /** Declaring contract. Empty for a file-level declaration. */
    contractName: string;
    /**
     * Reconstructed declaration such as `abstract contract Foo is Bar`.
     * Empty for a file-level declaration, which is emitted on its own.
     */
    contractHeader: string;
    /** Start offset in the original file, used to order members. */
    start: number;
}

export interface SliceUsedFunctionsOptions {
    sources: Record<string, { content: string }>;
    /** Preferred contract. Bases are searched when the name is not defined there. */
    contractName?: string | null;
    /** Function names executed by the trace, in trace order. */
    entryNames: string[];
    /**
     * Character budget for the stripped texts. `null` means no cap.
     * A definition is included only when its whole text fits.
     */
    maxChars: number | null;
    includeFile: (filename: string, content: string) => boolean;
    sanitize: (source: string) => string;
}

interface Def {
    id: string;
    kind: 'function' | 'modifier' | 'state' | 'enum' | 'struct';
    name: string;
    filename: string;
    start: number;
    end: number;
    contractName: string;
    node: AstNode;
    isConst: boolean;
    isImmutable: boolean;
}

interface ContractIndex {
    name: string;
    kind: string;
    header: string;
    bases: string[];
    functions: Def[];
    modifiers: Def[];
    stateVars: Def[];
    enums: Def[];
    structs: Def[];
}

interface SourceIndex {
    contracts: Map<string, ContractIndex>;
    files: Map<string, string>;
    globals: Def[];
}

const IDENT_RE = /^[A-Za-z_$][A-Za-z0-9_$]*$/;

/**
 * Keep the functions and modifiers reachable from the trace entry points.
 *
 * Each included function also pulls in the storage variables of its contract,
 * constants it names, and enums or structs it references. Those declarations
 * share the same character budget. Returns `null` when nothing in the sources
 * matches an entry name, so the caller can fall back to a file window. Returns
 * an empty list when entries matched but none of them fit in the budget.
 *
 * @param options - Sources, entry names, budget, and sanitizer
 * @return Selected definitions, or `null` when no entry matched
 */
export function sliceUsedFunctions(options: SliceUsedFunctionsOptions): SourceSlice[] | null {
    const entryNames = uniqueIdents(options.entryNames);
    if (entryNames.length === 0) return null;

    const indexed = indexSources(options.sources, options.includeFile);
    if (indexed.contracts.size === 0) return null;

    const roots = resolveEntries(indexed.contracts, options.contractName, entryNames);
    if (roots.length === 0) return null;

    const budget = options.maxChars === null ? Number.POSITIVE_INFINITY : options.maxChars;
    return expand(roots, indexed, budget, options.sanitize);
}

/**
 * Drop blank and duplicate names. Parser names are identifiers; anything else
 * cannot match a definition and is ignored.
 *
 * @param names - Candidate function names
 * @return Stable unique list
 */
function uniqueIdents(names: string[]): string[] {
    const out: string[] = [];
    const seen = new Set<string>();
    for (const name of names) {
        if (!name || seen.has(name)) continue;
        if (!IDENT_RE.test(name)) continue;
        seen.add(name);
        out.push(name);
    }
    return out;
}

/**
 * Parse embeddable sources into contracts, functions, and modifiers.
 *
 * @param sources - Sourcify source map
 * @param includeFile - File filter (drops Yul and non-Solidity artifacts)
 * @return Contract index and original file text
 */
function indexSources(
    sources: Record<string, { content: string }>,
    includeFile: (filename: string, content: string) => boolean,
): SourceIndex {
    const contracts = new Map<string, ContractIndex>();
    const files = new Map<string, string>();
    const globals: Def[] = [];

    for (const [filename, source] of Object.entries(sources)) {
        const content = source?.content ?? '';
        if (!includeFile(filename, content)) continue;
        let ast: AstNode;
        try {
            ast = parse(content, { tolerant: true, range: true, loc: false });
        } catch {
            continue;
        }
        files.set(filename, content);
        for (const node of (ast.children ?? []) as AstNode[]) {
            if (!node || typeof node.type !== 'string') continue;
            if (node.type === 'ContractDefinition' && typeof node.name === 'string') {
                rememberContract(contracts, filename, content, node);
                continue;
            }
            const kind = node.type === 'EnumDefinition' ? 'enum'
                : node.type === 'StructDefinition' ? 'struct'
                    : node.type === 'FileLevelConstant' ? 'state'
                        : null;
            if (!kind) continue;
            const def = declarationDef(filename, content, '', node, kind);
            if (def) globals.push(def);
        }
    }

    return { contracts, files, globals };
}

/**
 * Merge one contract's function and modifier definitions into the index.
 *
 * Definitions without a body (interfaces, abstract stubs) are skipped: they
 * do not show which storage the transaction writes.
 *
 * @param contracts - Index under construction
 * @param filename - File that contains the contract
 * @param content - Original file text, used only to validate the range
 * @param node - `ContractDefinition` AST node
 */
function rememberContract(
    contracts: Map<string, ContractIndex>,
    filename: string,
    content: string,
    node: AstNode,
): void {
    const name = node.name as string;
    let contract = contracts.get(name);
    if (!contract) {
        contract = {
            name,
            kind: typeof node.kind === 'string' ? node.kind : 'contract',
            header: '',
            bases: [],
            functions: [],
            modifiers: [],
            stateVars: [],
            enums: [],
            structs: [],
        };
        contracts.set(name, contract);
    }
    if (typeof node.kind === 'string') contract.kind = node.kind;
    for (const base of baseNames(node)) {
        if (!contract.bases.includes(base)) contract.bases.push(base);
    }
    contract.header = headerFor(contract.kind, name, contract.bases);
    for (const sub of (node.subNodes ?? []) as AstNode[]) {
        if (!sub || typeof sub.type !== 'string') continue;
        if (sub.type === 'FunctionDefinition' || sub.type === 'ModifierDefinition') {
            const def = toDef(filename, content, name, sub);
            if (!def) continue;
            const list = def.kind === 'function' ? contract.functions : contract.modifiers;
            list.push(def);
            continue;
        }
        const kind = sub.type === 'StateVariableDeclaration' ? 'state'
            : sub.type === 'EnumDefinition' ? 'enum'
                : sub.type === 'StructDefinition' ? 'struct'
                    : null;
        if (!kind) continue;
        const def = declarationDef(filename, content, name, sub, kind);
        if (!def) continue;
        const list = kind === 'state' ? contract.stateVars : kind === 'enum' ? contract.enums : contract.structs;
        list.push(def);
    }
}

/**
 * Solidity declaration rebuilt from the AST, not from raw header text.
 *
 * The name and base names are identifiers, so a comment or string in the
 * source header cannot change what the prompt shows.
 *
 * @param kind - Parser contract kind (`contract`, `abstract`, `library`, `interface`)
 * @param name - Contract name
 * @param bases - Inherited contract names
 * @return Header without the body, or an empty string when the name is not an identifier
 */
function headerFor(kind: string, name: string, bases: string[]): string {
    if (!IDENT_RE.test(name)) return '';
    const safeBases = bases.filter(base => IDENT_RE.test(base));
    const keyword = kind === 'library' ? 'library'
        : kind === 'interface' ? 'interface'
            : kind === 'abstract' ? 'abstract contract'
                : 'contract';
    return safeBases.length > 0 ? `${keyword} ${name} is ${safeBases.join(', ')}` : `${keyword} ${name}`;
}

/**
 * Inheritance names in source order.
 *
 * @param node - `ContractDefinition` AST node
 * @return Base contract names
 */
function baseNames(node: AstNode): string[] {
    const bases = (node.baseContracts ?? []) as AstNode[];
    const names: string[] = [];
    for (const base of bases) {
        const path = base?.baseName?.namePath ?? base?.baseName?.name;
        if (typeof path === 'string' && path) {
            const last = path.includes('.') ? path.slice(path.lastIndexOf('.') + 1) : path;
            names.push(last);
        }
    }
    return names;
}

/**
 * Build a sliceable definition when the node has a body and a usable range.
 *
 * Parser ranges stop on the closing brace; the end index is exclusive of that
 * character, so the slice extends one byte past `range[1]`.
 *
 * @param filename - Source filename
 * @param content - Original file text
 * @param contractName - Enclosing contract
 * @param node - Function or modifier AST node
 * @return Definition, or `null` when it cannot be sliced
 */
function toDef(filename: string, content: string, contractName: string, node: AstNode): Def | null {
    if (!node.body) return null;
    const name = typeof node.name === 'string' ? node.name : '';
    if (!name || !IDENT_RE.test(name)) return null;
    const span = spanOf(content, node);
    if (!span) return null;
    return {
        id: `${filename}:${span.start}:${name}`,
        kind: node.type === 'ModifierDefinition' ? 'modifier' : 'function',
        name,
        filename,
        start: span.start,
        end: span.end,
        contractName,
        node,
        isConst: false,
        isImmutable: false,
    };
}

/**
 * Slice a state variable, enum, struct, or file-level constant.
 *
 * @param filename - Source filename
 * @param content - Original file text
 * @param contractName - Enclosing contract, or empty at file scope
 * @param node - Declaration AST node
 * @param kind - Declaration kind
 * @return Definition, or `null` when it has no identifier or no range
 */
function declarationDef(
    filename: string,
    content: string,
    contractName: string,
    node: AstNode,
    kind: 'state' | 'enum' | 'struct',
): Def | null {
    let name = '';
    let isConst = false;
    let isImmutable = false;
    if (node.type === 'FileLevelConstant') {
        if (typeof node.name !== 'string') return null;
        name = node.name;
        isConst = true;
    } else if (kind === 'state') {
        const variable = (node.variables as AstNode[] | undefined)?.[0];
        if (!variable || typeof variable.name !== 'string') return null;
        name = variable.name;
        isConst = variable.isDeclaredConst === true;
        isImmutable = variable.isImmutable === true;
    } else if (typeof node.name === 'string') {
        name = node.name;
    }
    if (!name || !IDENT_RE.test(name)) return null;
    const span = spanOf(content, node);
    if (!span) return null;
    return {
        id: `${filename}:${span.start}:${name}`,
        kind,
        name,
        filename,
        start: span.start,
        end: span.end,
        contractName,
        node,
        isConst,
        isImmutable,
    };
}

/**
 * Source span of a node. Parser ranges exclude the closing character.
 *
 * @param content - Original file text
 * @param node - AST node with a `range`
 * @return Inclusive-exclusive span, or `null` when the range is unusable
 */
function spanOf(content: string, node: AstNode): { start: number; end: number } | null {
    const range = node.range as [number, number] | undefined;
    if (!range || range.length < 2 || range[0] < 0 || range[1] < range[0]) return null;
    return { start: range[0], end: Math.min(content.length, range[1] + 1) };
}

/**
 * All overloads of each entry name on the target contract and its bases.
 *
 * When `contractName` is missing or unknown, every contract in the bundle is
 * searched. A contract that defines the name hides the same name on its bases.
 *
 * @param contracts - Parsed contracts
 * @param contractName - Preferred contract, if known
 * @param entryNames - Trace function names
 * @return Entry definitions in trace order
 */
function resolveEntries(
    contracts: Map<string, ContractIndex>,
    contractName: string | null | undefined,
    entryNames: string[],
): Def[] {
    const starts = contractName && contracts.has(contractName)
        ? [contractName]
        : [...contracts.keys()];
    const out: Def[] = [];
    const seen = new Set<string>();
    for (const name of entryNames) {
        for (const start of starts) {
            for (const def of findNamed(contracts, start, name, 'function', false)) {
                if (seen.has(def.id)) continue;
                seen.add(def.id);
                out.push(def);
            }
        }
    }
    return out;
}

/**
 * Find function or modifier definitions by name.
 *
 * @param contracts - Parsed contracts
 * @param start - Contract to start from
 * @param name - Definition name
 * @param kind - Function or modifier
 * @param basesOnly - Skip `start` itself (`super.foo`)
 * @return Matching definitions that have a body
 */
function findNamed(
    contracts: Map<string, ContractIndex>,
    start: string,
    name: string,
    kind: 'function' | 'modifier',
    basesOnly: boolean,
): Def[] {
    const out: Def[] = [];
    const seen = new Set<string>();
    const queue: Array<{ name: string; allowOwn: boolean }> = [{ name: start, allowOwn: !basesOnly }];
    while (queue.length > 0) {
        const item = queue.shift();
        if (!item || seen.has(item.name)) continue;
        seen.add(item.name);
        const contract = contracts.get(item.name);
        if (!contract) continue;
        if (item.allowOwn) {
            const list = kind === 'function' ? contract.functions : contract.modifiers;
            const found = list.filter(def => def.name === name);
            if (found.length > 0) {
                out.push(...found);
                continue;
            }
        }
        for (const base of contract.bases) queue.push({ name: base, allowOwn: true });
    }
    return out;
}

/**
 * Breadth-first inclusion. Each wave is entry functions, then the modifiers
 * and calls discovered from the previous wave. A definition that does not fit
 * is skipped, and its callees are still queued so a smaller one can use the
 * remaining budget.
 *
 * @param roots - Entry definitions
 * @param contracts - Parsed contracts
 * @param files - Original file text
 * @param budget - Remaining characters
 * @param sanitize - Comment stripper and fence redaction
 * @return Included slices
 */
function expand(
    roots: Def[],
    indexed: SourceIndex,
    budget: number,
    sanitize: (source: string) => string,
): SourceSlice[] {
    const slices: SourceSlice[] = [];
    const seen = new Set<string>();
    let wave = roots;
    let remaining = budget;

    while (wave.length > 0) {
        const next: Def[] = [];
        const queued = new Set<string>();
        for (const def of wave) {
            if (seen.has(def.id)) continue;
            seen.add(def.id);
            const text = materialize(def, indexed.files, sanitize);
            if (text && text.length <= remaining && headerOk(def, indexed.contracts)) {
                slices.push(toSlice(def, text, indexed.contracts));
                remaining -= text.length;
                for (const extra of contextOf(indexed, def)) {
                    if (seen.has(extra.id)) continue;
                    seen.add(extra.id);
                    const extraText = materialize(extra, indexed.files, sanitize);
                    if (!extraText || extraText.length > remaining || !headerOk(extra, indexed.contracts)) continue;
                    slices.push(toSlice(extra, extraText, indexed.contracts));
                    remaining -= extraText.length;
                }
            }
            for (const child of callees(indexed.contracts, def)) {
                if (seen.has(child.id) || queued.has(child.id)) continue;
                queued.add(child.id);
                next.push(child);
            }
        }
        wave = next;
    }

    return slices;
}

/**
 * Storage of the enclosing contract, plus constants, enums, and structs the
 * body names.
 *
 * Real storage variables (not `constant` or `immutable`) are always included.
 * Constants and immutables are included only when the function mentions them,
 * including names used inside inline assembly. Enum and struct lookup prefers
 * the enclosing contract and its bases, then a contract whose name appears in
 * the same body (`Math.Rounding`).
 *
 * @param indexed - Parsed sources
 * @param def - Function or modifier that was just included
 * @return Declarations to embed with `def`
 */
function contextOf(indexed: SourceIndex, def: Def): Def[] {
    if (def.kind !== 'function' && def.kind !== 'modifier') return [];
    const refs = referencedNames(def.node);
    const out: Def[] = [];
    const seen = new Set<string>();
    const push = (item: Def | null | undefined): void => {
        if (!item || item.id === def.id || seen.has(item.id)) return;
        seen.add(item.id);
        out.push(item);
    };

    const scopes = ancestors(indexed.contracts, def.contractName);
    for (const scope of scopes) {
        const contract = indexed.contracts.get(scope);
        if (!contract) continue;
        for (const variable of contract.stateVars) {
            const ownStorage = scope === def.contractName && !variable.isConst && !variable.isImmutable;
            if (ownStorage || refs.has(variable.name)) push(variable);
        }
    }
    for (const name of refs) {
        push(lookupType(indexed, def.contractName, refs, name, 'enum'));
        push(lookupType(indexed, def.contractName, refs, name, 'struct'));
    }
    for (const global of indexed.globals) {
        if (global.kind === 'state' && refs.has(global.name)) push(global);
    }
    return out;
}

/**
 * Names written in a function: identifiers, assembly calls, type names, and
 * member names. Matching them against declarations filters out locals.
 *
 * @param node - Function or modifier AST node
 * @return Referenced names
 */
function referencedNames(node: AstNode): Set<string> {
    const names = new Set<string>();
    const walk = (current: AstNode | null | undefined): void => {
        if (!current || typeof current !== 'object') return;
        if (current.type === 'Identifier' && typeof current.name === 'string') names.add(current.name);
        if (current.type === 'AssemblyCall' && typeof current.functionName === 'string') names.add(current.functionName);
        if (current.type === 'UserDefinedTypeName' && typeof current.namePath === 'string') {
            for (const part of current.namePath.split('.')) {
                if (part) names.add(part);
            }
        }
        if (current.type === 'MemberAccess' && typeof current.memberName === 'string') names.add(current.memberName);
        for (const value of Object.values(current)) {
            if (Array.isArray(value)) {
                for (const item of value) {
                    if (item && typeof item === 'object' && (item as AstNode).type) walk(item as AstNode);
                }
            } else if (value && typeof value === 'object' && (value as AstNode).type) {
                walk(value as AstNode);
            }
        }
    };
    walk(node);
    return names;
}

/**
 * Contract and its bases, starting at `start`.
 *
 * @param contracts - Parsed contracts
 * @param start - Contract that contains the function
 * @return Names in breadth-first order
 */
function ancestors(contracts: Map<string, ContractIndex>, start: string): string[] {
    const out: string[] = [];
    const seen = new Set<string>();
    const queue = [start];
    while (queue.length > 0) {
        const name = queue.shift();
        if (!name || seen.has(name)) continue;
        seen.add(name);
        out.push(name);
        const contract = contracts.get(name);
        if (!contract) continue;
        for (const base of contract.bases) queue.push(base);
    }
    return out;
}

/**
 * Resolve an enum or struct name mentioned by a function.
 *
 * @param indexed - Parsed sources
 * @param owner - Contract that contains the function
 * @param refs - Names seen in that function
 * @param name - Candidate type name
 * @param kind - `enum` or `struct`
 * @return Matching definition, or `null` when it is missing or ambiguous
 */
function lookupType(
    indexed: SourceIndex,
    owner: string,
    refs: Set<string>,
    name: string,
    kind: 'enum' | 'struct',
): Def | null {
    for (const scope of ancestors(indexed.contracts, owner)) {
        const contract = indexed.contracts.get(scope);
        const list = kind === 'enum' ? contract?.enums : contract?.structs;
        const hit = list?.find(item => item.name === name);
        if (hit) return hit;
    }
    const qualified: Def[] = [];
    for (const [contractName, contract] of indexed.contracts) {
        if (!refs.has(contractName)) continue;
        const list = kind === 'enum' ? contract.enums : contract.structs;
        const hit = list.find(item => item.name === name);
        if (hit) qualified.push(hit);
    }
    if (qualified.length === 1) return qualified[0];
    if (qualified.length > 1) return null;
    const globals = indexed.globals.filter(item => item.kind === kind && item.name === name);
    return globals.length === 1 ? globals[0] : null;
}

/**
 * Comment-stripped text of one definition.
 *
 * @param def - Definition to slice
 * @param files - Original file text
 * @param sanitize - Comment stripper and fence redaction
 * @return Trimmed text, possibly empty
 */
function materialize(def: Def, files: Map<string, string>, sanitize: (source: string) => string): string {
    const content = files.get(def.filename);
    const raw = content ? content.slice(def.start, def.end) : '';
    return sanitize(raw).trim();
}

/**
 * A contract member needs a reconstructed header. File-level declarations do not.
 *
 * @param def - Definition about to be embedded
 * @param contracts - Parsed contracts
 * @return Whether the definition can be shown safely
 */
function headerOk(def: Def, contracts: Map<string, ContractIndex>): boolean {
    if (!def.contractName) return true;
    return Boolean(contracts.get(def.contractName)?.header);
}

/**
 * Copy a definition into the public slice shape.
 *
 * @param def - Included definition
 * @param text - Sanitized source
 * @param contracts - Parsed contracts, for the header
 * @return Prompt slice
 */
function toSlice(def: Def, text: string, contracts: Map<string, ContractIndex>): SourceSlice {
    return {
        filename: def.filename,
        kind: def.kind,
        name: def.name,
        text,
        contractName: def.contractName,
        contractHeader: def.contractName ? (contracts.get(def.contractName)?.header ?? '') : '',
        start: def.start,
    };
}

/**
 * Modifiers attached to a function, then functions it may call.
 *
 * Bare calls, `this.foo`, and `super.foo` resolve inside the contract and its
 * bases. `Lib.foo` resolves only when `Lib` is a contract or library in this
 * bundle. External calls such as `token.transfer` are left to their own trace
 * frame. Every overload of a matched name is included.
 *
 * @param contracts - Parsed contracts
 * @param def - Definition whose body is scanned
 * @return Newly referenced definitions
 */
function callees(contracts: Map<string, ContractIndex>, def: Def): Def[] {
    const out: Def[] = [];
    const seen = new Set<string>();
    const push = (found: Def[]): void => {
        for (const item of found) {
            if (item.id === def.id || seen.has(item.id)) continue;
            seen.add(item.id);
            out.push(item);
        }
    };

    if (def.kind === 'function') {
        for (const mod of (def.node.modifiers ?? []) as AstNode[]) {
            const name = modifierName(mod);
            if (!name) continue;
            push(findNamed(contracts, def.contractName, name, 'modifier', false));
        }
    }

    walkCalls(def.node, call => {
        const ref = resolveCall(call, contracts, def.contractName);
        if (!ref) return;
        push(findNamed(contracts, ref.contract, ref.name, 'function', ref.basesOnly));
    });
    return out;
}

/**
 * Modifier invocation name (`onlyOwner`), ignoring base-constructor calls that
 * the parser also reports as modifier invocations without a string name.
 *
 * @param node - AST node that may be a `ModifierInvocation`
 * @return Modifier name, or `null`
 */
function modifierName(node: AstNode): string | null {
    if (node?.type !== 'ModifierInvocation') return null;
    return typeof node.name === 'string' && node.name ? node.name : null;
}

/**
 * Visit every function call under `node`.
 *
 * @param node - AST subtree
 * @param visit - Callback for each `FunctionCall`
 */
function walkCalls(node: AstNode, visit: (call: AstNode) => void): void {
    if (!node || typeof node !== 'object') return;
    if (node.type === 'FunctionCall') visit(node);
    for (const value of Object.values(node)) {
        if (Array.isArray(value)) {
            for (const item of value) {
                if (item && typeof item === 'object' && (item as AstNode).type) walkCalls(item as AstNode, visit);
            }
        } else if (value && typeof value === 'object' && (value as AstNode).type) {
            walkCalls(value as AstNode, visit);
        }
    }
}

interface CallRef {
    contract: string;
    name: string;
    basesOnly: boolean;
}

/**
 * Map a call expression to a contract-local function name.
 *
 * @param call - `FunctionCall` node
 * @param contracts - Known contracts and libraries
 * @param owner - Contract that contains the caller
 * @return Resolution target, or `null` for an external or built-in call
 */
function resolveCall(
    call: AstNode,
    contracts: Map<string, ContractIndex>,
    owner: string,
): CallRef | null {
    const expr = unwrapCallee(call.expression as AstNode | undefined);
    if (!expr) return null;
    if (expr.type === 'Identifier' && typeof expr.name === 'string') {
        return { contract: owner, name: expr.name, basesOnly: false };
    }
    if (expr.type !== 'MemberAccess' || typeof expr.memberName !== 'string') return null;
    const inner = expr.expression as AstNode | undefined;
    if (!inner || inner.type !== 'Identifier' || typeof inner.name !== 'string') return null;
    if (inner.name === 'this') return { contract: owner, name: expr.memberName, basesOnly: false };
    if (inner.name === 'super') return { contract: owner, name: expr.memberName, basesOnly: true };
    if (contracts.has(inner.name)) return { contract: inner.name, name: expr.memberName, basesOnly: false };
    return null;
}

/**
 * Unwrap `foo{value: 1}()` into `foo`.
 *
 * @param expr - Call expression
 * @return Expression the call actually invokes
 */
function unwrapCallee(expr: AstNode | undefined): AstNode | null {
    let current = expr ?? null;
    while (current && current.type === 'NameValueExpression') {
        current = (current.expression as AstNode | undefined) ?? null;
    }
    return current;
}
