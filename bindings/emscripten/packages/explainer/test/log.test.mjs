import { describe, it, afterEach } from 'node:test';
import assert from 'node:assert/strict';
import {
    parseLogLevel, getExplainerLogLevel, setExplainerLogLevel, setExplainerLogSink,
    explainerLog, resetExplainerLogForTests, elapsedMs,
} from '../dist/log.js';
import {
    explainerLog as explainerLogFromIndex,
    parseLogLevel as parseLogLevelFromIndex,
    getExplainerLogLevel as getExplainerLogLevelFromIndex,
    setExplainerLogLevel as setExplainerLogLevelFromIndex,
    setExplainerLogSink as setExplainerLogSinkFromIndex,
} from '../dist/index.js';

describe('parseLogLevel', () => {
    it('normalizes aliases and defaults unknown values to warn', () => {
        assert.equal(parseLogLevel('debug'), 'debug');
        assert.equal(parseLogLevel('INFO'), 'info');
        assert.equal(parseLogLevel('warning'), 'warn');
        assert.equal(parseLogLevel('off'), 'silent');
        assert.equal(parseLogLevel('none'), 'silent');
        assert.equal(parseLogLevel('nope'), 'warn');
        assert.equal(parseLogLevel(''), 'warn');
        assert.equal(parseLogLevel(undefined), 'warn');
        assert.equal(parseLogLevel(null), 'warn');
        assert.equal(parseLogLevel('  Debug  '), 'debug');
        assert.equal(parseLogLevel('error'), 'error');
        assert.equal(parseLogLevel('silent'), 'silent');
    });
});

describe('package exports', () => {
    it('re-exports the log API from the package entry', () => {
        assert.equal(parseLogLevelFromIndex, parseLogLevel);
        assert.equal(explainerLogFromIndex, explainerLog);
        assert.equal(getExplainerLogLevelFromIndex, getExplainerLogLevel);
        assert.equal(setExplainerLogLevelFromIndex, setExplainerLogLevel);
        assert.equal(setExplainerLogSinkFromIndex, setExplainerLogSink);
    });
});

describe('explainerLog filtering', () => {
    afterEach(() => {
        resetExplainerLogForTests();
    });

    it('emits only levels at or below the configured threshold', () => {
        const lines = [];
        setExplainerLogSink((level, message, extra) => {
            lines.push({ level, message, extra });
        });
        setExplainerLogLevel('info');

        explainerLog('debug', 'hidden');
        explainerLog('info', 'shown', { scope: 'enrich', ms: 12 });
        explainerLog('warn', 'also');
        explainerLog('error', 'yes');

        assert.deepEqual(lines.map(l => l.message), ['shown', 'also', 'yes']);
        assert.equal(lines[0].extra.scope, 'enrich');
        assert.equal(getExplainerLogLevel(), 'info');
    });

    it('silent emits nothing', () => {
        const lines = [];
        setExplainerLogSink((level, message) => { lines.push(message); });
        setExplainerLogLevel('silent');
        explainerLog('error', 'nope');
        assert.equal(lines.length, 0);
    });

    it('error threshold hides warn/info/debug', () => {
        const lines = [];
        setExplainerLogSink((level, message) => { lines.push(message); });
        setExplainerLogLevel('error');
        explainerLog('debug', 'd');
        explainerLog('info', 'i');
        explainerLog('warn', 'w');
        explainerLog('error', 'e');
        assert.deepEqual(lines, ['e']);
    });

    it('explainerLog(silent) never emits even at debug', () => {
        const lines = [];
        setExplainerLogSink((level, message) => { lines.push(message); });
        setExplainerLogLevel('debug');
        explainerLog('silent', 'must not appear');
        assert.equal(lines.length, 0);
    });

    it('a throwing sink does not throw to the caller', () => {
        setExplainerLogSink(() => { throw new Error('sink down'); });
        setExplainerLogLevel('debug');
        assert.doesNotThrow(() => explainerLog('debug', 'x'));
    });

    it('setExplainerLogLevel() without args or with null reloads the env default', () => {
        const previous = process.env.C4_EXPLAINER_LOG_LEVEL;
        process.env.C4_EXPLAINER_LOG_LEVEL = 'debug';
        try {
            setExplainerLogLevel();
            assert.equal(getExplainerLogLevel(), 'debug');
            setExplainerLogLevel('error');
            assert.equal(getExplainerLogLevel(), 'error');
            setExplainerLogLevel(null);
            assert.equal(getExplainerLogLevel(), 'debug');
        } finally {
            if (previous === undefined) delete process.env.C4_EXPLAINER_LOG_LEVEL;
            else process.env.C4_EXPLAINER_LOG_LEVEL = previous;
            resetExplainerLogForTests();
        }
    });

    it('setExplainerLogLevel accepts aliases', () => {
        setExplainerLogLevel('warning');
        assert.equal(getExplainerLogLevel(), 'warn');
        setExplainerLogLevel('off');
        assert.equal(getExplainerLogLevel(), 'silent');
        setExplainerLogLevel('nope');
        assert.equal(getExplainerLogLevel(), 'warn');
    });
});

describe('default sink', () => {
    afterEach(() => {
        resetExplainerLogForTests();
    });

    it('routes levels to console, prefixes scope, and strips scope from extra', () => {
        const captured = { error: [], warn: [], info: [], debug: [] };
        const orig = {
            error: console.error,
            warn: console.warn,
            info: console.info,
            debug: console.debug,
        };
        console.error = (...args) => { captured.error.push(args); };
        console.warn = (...args) => { captured.warn.push(args); };
        console.info = (...args) => { captured.info.push(args); };
        console.debug = (...args) => { captured.debug.push(args); };
        try {
            setExplainerLogSink(null);
            setExplainerLogLevel('debug');
            explainerLog('error', 'boom', { scope: 'enrich' });
            explainerLog('warn', 'careful');
            explainerLog('info', 'hello', { scope: 'solc', ms: 5 });
            explainerLog('debug', 'detail', { foo: 1 });

            assert.deepEqual(captured.error, [['[enrich] boom']]);
            assert.deepEqual(captured.warn, [['[explainer] careful']]);
            assert.deepEqual(captured.info, [['[solc] hello', { ms: 5 }]]);
            assert.deepEqual(captured.debug, [['[explainer] detail', { foo: 1 }]]);
        } finally {
            console.error = orig.error;
            console.warn = orig.warn;
            console.info = orig.info;
            console.debug = orig.debug;
        }
    });

    it('setExplainerLogSink(null) restores the default console sink after a custom one', () => {
        const custom = [];
        const captured = [];
        const origWarn = console.warn;
        console.warn = (...args) => { captured.push(args); };
        try {
            setExplainerLogSink((level, message) => { custom.push(message); });
            setExplainerLogLevel('warn');
            explainerLog('warn', 'via-custom');
            setExplainerLogSink(null);
            explainerLog('warn', 'via-default');
            assert.deepEqual(custom, ['via-custom']);
            assert.deepEqual(captured, [['[explainer] via-default']]);
        } finally {
            console.warn = origWarn;
        }
    });
});

describe('elapsedMs', () => {
    it('returns Date.now() minus the start timestamp', () => {
        const origNow = Date.now;
        Date.now = () => 1_000;
        try {
            assert.equal(elapsedMs(400), 600);
            assert.equal(elapsedMs(1_000), 0);
        } finally {
            Date.now = origNow;
        }
    });
});
