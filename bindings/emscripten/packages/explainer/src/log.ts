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

export type LogLevel = 'silent' | 'error' | 'warn' | 'info' | 'debug';

const LEVEL_RANK: Record<LogLevel, number> = {
    silent: 0,
    error: 1,
    warn: 2,
    info: 3,
    debug: 4,
};

export type ExplainerLogFn = (level: LogLevel, message: string, extra?: Record<string, unknown>) => void;

const ENV_NAME = 'C4_EXPLAINER_LOG_LEVEL';

let currentLevel: LogLevel = readLevelFromEnv();
let sink: ExplainerLogFn = defaultSink;

/**
 * Parse a log-level name. Unknown values become `warn`.
 *
 * @param raw - Level string, e.g. `"debug"`
 * @return Normalized level
 */
export function parseLogLevel(raw: string | undefined | null): LogLevel {
    if (!raw) return 'warn';
    const key = raw.trim().toLowerCase();
    if (key === 'silent' || key === 'error' || key === 'warn' || key === 'info' || key === 'debug') {
        return key;
    }
    if (key === 'warning') return 'warn';
    if (key === 'off' || key === 'none') return 'silent';
    return 'warn';
}

/**
 * Read `C4_EXPLAINER_LOG_LEVEL` from the environment.
 *
 * @return Configured level, or `warn` when unset / not in Node
 */
function readLevelFromEnv(): LogLevel {
    if (typeof process === 'undefined' || !process.env) return 'warn';
    return parseLogLevel(process.env[ENV_NAME]);
}

/**
 * Default writer: `console.error` / `warn` / `info` / `debug` with a scope prefix.
 *
 * @param level - Severity
 * @param message - Short description
 * @param extra - Optional structured fields (`scope` becomes the bracket prefix)
 */
function defaultSink(level: LogLevel, message: string, extra?: Record<string, unknown>): void {
    if (typeof console === 'undefined') return;
    const scope = extra && typeof extra.scope === 'string' && extra.scope ? extra.scope : 'explainer';
    const rest = extra ? { ...extra } : undefined;
    if (rest && 'scope' in rest) delete rest.scope;
    const hasRest = rest && Object.keys(rest).length > 0;
    const line = `[${scope}] ${message}`;
    if (level === 'error' && typeof console.error === 'function') {
        if (hasRest) console.error(line, rest);
        else console.error(line);
        return;
    }
    if (level === 'debug' && typeof console.debug === 'function') {
        if (hasRest) console.debug(line, rest);
        else console.debug(line);
        return;
    }
    const write = level === 'info' && typeof console.info === 'function' ? console.info : console.warn;
    if (hasRest) write(line, rest);
    else write(line);
}

/**
 * Current minimum level that will be emitted.
 *
 * @return Active log level
 */
export function getExplainerLogLevel(): LogLevel {
    return currentLevel;
}

/**
 * Set the minimum log level. Omit to re-read `C4_EXPLAINER_LOG_LEVEL`.
 *
 * @param level - New level, or omit to reload from the environment
 */
export function setExplainerLogLevel(level?: LogLevel | string | null): void {
    currentLevel = level === undefined || level === null
        ? readLevelFromEnv()
        : parseLogLevel(level);
}

/**
 * Replace the log writer. Pass `null` to restore the default sink.
 *
 * @param fn - Custom writer, or `null` / omit to restore
 */
export function setExplainerLogSink(fn?: ExplainerLogFn | null): void {
    sink = fn ?? defaultSink;
}

/**
 * Reset level (from env) and sink. Test-only.
 */
export function resetExplainerLogForTests(): void {
    currentLevel = readLevelFromEnv();
    sink = defaultSink;
}

/**
 * Emit a log line if `level` is at or above the configured threshold.
 *
 * @param level - Severity
 * @param message - Short description (no secrets / source dumps)
 * @param extra - Optional structured fields
 */
export function explainerLog(level: LogLevel, message: string, extra?: Record<string, unknown>): void {
    if (level === 'silent') return;
    if (LEVEL_RANK[level] > LEVEL_RANK[currentLevel]) return;
    try {
        sink(level, message, extra);
    } catch { /* a broken sink must not fail the caller */ }
}

/**
 * Milliseconds since `started` (from `Date.now()`).
 *
 * @param started - Epoch ms at the start of the span
 * @return Elapsed milliseconds
 */
export function elapsedMs(started: number): number {
    return Date.now() - started;
}
