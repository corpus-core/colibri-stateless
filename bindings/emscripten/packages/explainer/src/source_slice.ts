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
    kind: 'function' | 'modifier';
    name: string;
    /** Comment-stripped source of this definition. */
    text: string;
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
    kind: 'function' | 'modifier';
    name: string;
    filename: string;
    start: number;
    end: number;
    contractName: string;
    node: AstNode;
}

interface ContractIndex {
    name: string;
    bases: string[];
    functions: Def[];
    modifiers: Def[];
}

/**
 * Keep the functions and modifiers reachable from the trace entry points.
 *
 * Returns `null` when nothing in the sources matches an entry name, so the
 * caller can fall back to a file window. Returns an empty list when entries
 * matched but none of them fit in the budget.
 *
 * @param options - Sources, entry names, budget, and sanitizer
 * @return Selected definitions, or `null` when no entry matched
 */
export function sliceUsedFunctions(options: SliceUsedFunctionsOptions): SourceSlice[] | null {
    const entryNames = uniqueIdents(options.entryNames);
    if (entryNames.length === 0) return null;

    const { contracts, files } = indexSources(options.sources, options.includeFile);
    if (contracts.size === 0) return null;

    const roots = resolveEntries(contracts, options.contractName, entryNames);
    if (roots.length === 0) return null;

    const budget = options.maxChars === null ? Number.POSITIVE_INFINITY : options.maxChars;
    return expand(roots, contracts, files, budget, options.sanitize);
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
        if (!/^[A-Za-z_$][A-Za-z0-9_$]*$/.test(name)) continue;
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
): { contracts: Map<string, ContractIndex>; files: Map<string, string> } {
    const contracts = new Map<string, ContractIndex>();
    const files = new Map<string, string>();

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
            if (node?.type !== 'ContractDefinition' || typeof node.name !== 'string') continue;
            rememberContract(contracts, filename, content, node);
        }
    }

    return { contracts, files };
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
        contract = { name, bases: [], functions: [], modifiers: [] };
        contracts.set(name, contract);
    }
    for (const base of baseNames(node)) {
        if (!contract.bases.includes(base)) contract.bases.push(base);
    }
    for (const sub of (node.subNodes ?? []) as AstNode[]) {
        if (sub?.type !== 'FunctionDefinition' && sub?.type !== 'ModifierDefinition') continue;
        const def = toDef(filename, content, name, sub);
        if (!def) continue;
        const list = def.kind === 'function' ? contract.functions : contract.modifiers;
        list.push(def);
    }
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
    if (!name) return null;
    const range = node.range as [number, number] | undefined;
    if (!range || range.length < 2 || range[0] < 0 || range[1] < range[0]) return null;
    const end = Math.min(content.length, range[1] + 1);
    return {
        id: `${filename}:${range[0]}:${name}`,
        kind: node.type === 'ModifierDefinition' ? 'modifier' : 'function',
        name,
        filename,
        start: range[0],
        end,
        contractName,
        node,
    };
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
    contracts: Map<string, ContractIndex>,
    files: Map<string, string>,
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
            const content = files.get(def.filename);
            const raw = content ? content.slice(def.start, def.end) : '';
            const text = sanitize(raw).trim();
            if (text && text.length <= remaining) {
                slices.push({ filename: def.filename, kind: def.kind, name: def.name, text });
                remaining -= text.length;
            }
            for (const child of callees(contracts, def)) {
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
