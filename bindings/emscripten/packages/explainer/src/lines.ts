/**
 * Copyright (c) 2025 corpus.core
 *
 * SPDX-License-Identifier: MIT
 */

import type { ExplanationLine } from './types.js';

const KEYWORDS = ['SUMMARY', 'STEP', 'RISK', 'NOTE'] as const;
type LineType = ExplanationLine['type'];

const TYPE_BY_KEYWORD: Record<string, LineType> = {
    SUMMARY: 'summary',
    STEP: 'step',
    RISK: 'risk',
    NOTE: 'note',
};

/**
 * Incremental parser for the explanation line protocol.
 *
 * Feed token deltas; each completed line is returned. Call `finish` for a
 * trailing line that never received a newline.
 */
export class LineStreamParser {
    private buffer = '';
    private readonly allowed: Set<string>;

    /**
     * @param allowedRefs - Ids present in the user prompt. Others are dropped.
     */
    constructor(allowedRefs: Iterable<string>) {
        this.allowed = new Set(allowedRefs);
    }

    /**
     * Parse newly arrived text.
     *
     * @param delta - Next fragment of the model output
     * @return Lines that ended with a newline in this fragment
     */
    push(delta: string): ExplanationLine[] {
        this.buffer += delta;
        const out: ExplanationLine[] = [];
        let nl = this.buffer.indexOf('\n');
        while (nl >= 0) {
            const raw = this.buffer.slice(0, nl);
            this.buffer = this.buffer.slice(nl + 1);
            const line = parseLine(raw, this.allowed);
            if (line) out.push(line);
            nl = this.buffer.indexOf('\n');
        }
        return out;
    }

    /**
     * Flush a trailing line with no newline.
     *
     * @return The last line, or an empty list
     */
    finish(): ExplanationLine[] {
        const raw = this.buffer;
        this.buffer = '';
        const line = parseLine(raw, this.allowed);
        return line ? [line] : [];
    }
}

/**
 * Parse a complete model reply.
 *
 * @param text - Raw model output
 * @param allowedRefs - Ids present in the user prompt
 * @return Lines in order, including malformed ones
 */
export function parseExplanation(text: string, allowedRefs: Iterable<string>): ExplanationLine[] {
    const parser = new LineStreamParser(allowedRefs);
    return [...parser.push(text.endsWith('\n') ? text : `${text}\n`), ...parser.finish()];
}

/**
 * EBNF grammar for WebLLM constrained decoding (`response_format.type = "grammar"`).
 *
 * `ref` is the set of ids actually included in the prompt. User mode omits `STEP`.
 *
 * @param refs - Ids from `promptRefs`
 * @param mode - `user` hides step lines from the grammar
 * @return EBNF grammar, or `undefined` when there is nothing to cite
 */
export function buildLineGrammar(refs: string[], mode: 'user' | 'developer'): string | undefined {
    if (refs.length === 0) return undefined;
    const alt = refs.map(id => `"${id}"`).join(' | ');
    const steps = mode === 'developer'
        ? 'steps ::= step? step? step? step? step? step?'
        : 'steps ::= ""';
    return [
        'root ::= summaries steps risks notes',
        'summaries ::= summary summary? summary?',
        steps,
        'risks ::= risk? risk? risk?',
        'notes ::= note? note? note?',
        'summary ::= "SUMMARY " text "\\n"',
        'step ::= "STEP " refs " " text "\\n"',
        'risk ::= "RISK " refs " " text "\\n"',
        'note ::= "NOTE " refs " " text "\\n"',
        'refs ::= ref ("," ref)*',
        'text ::= [^\\n]+',
        `ref ::= ${alt}`,
    ].join('\n');
}

function parseLine(raw: string, allowed: Set<string>): ExplanationLine | undefined {
    const trimmed = raw.trim();
    if (!trimmed) return undefined;
    const keyword = KEYWORDS.find(word => trimmed === word || trimmed.startsWith(`${word} `));
    if (!keyword) {
        return { type: 'note', refs: [], text: trimmed, malformed: true };
    }
    const type = TYPE_BY_KEYWORD[keyword];
    const rest = trimmed.slice(keyword.length).trim();
    if (type === 'summary') {
        if (!rest) return { type, refs: [], text: '', malformed: true };
        return { type, refs: [], text: rest };
    }
    const space = rest.indexOf(' ');
    const refPart = space === -1 ? rest : rest.slice(0, space);
    const text = space === -1 ? '' : rest.slice(space + 1).trim();
    if (!refPart || !text || /\s/.test(refPart)) {
        return { type, refs: [], text: rest, malformed: true };
    }
    const cited = refPart.split(',');
    const refs: string[] = [];
    const droppedRefs: string[] = [];
    for (const id of cited) {
        if (!/^[clsb]\d+$/.test(id)) {
            return { type, refs: [], text, malformed: true, droppedRefs: cited };
        }
        if (allowed.has(id)) refs.push(id);
        else droppedRefs.push(id);
    }
    const line: ExplanationLine = { type, refs, text };
    if (droppedRefs.length) line.droppedRefs = droppedRefs;
    return line;
}
