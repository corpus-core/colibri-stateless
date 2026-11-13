import { readFileSync } from 'node:fs';
process.env.C4_STATE_DIR = process.env.HOME + '/.colibri';
const { enrichSimulation } = await import('./dist/enrich.js');

const sim = JSON.parse(readFileSync('./test/data/scan/0xe1a5247f0fc2a05e64c06fc3f461e5099c4f535b1ee78127c2437b5ad86645f1/sim.json', 'utf8'));
const tx = JSON.parse(readFileSync('./test/data/scan/0xe1a5247f0fc2a05e64c06fc3f461e5099c4f535b1ee78127c2437b5ad86645f1/tx.json', 'utf8'));
const txParams = { to: tx.meta.to, from: tx.meta.from, data: tx.meta.input, value: tx.meta.value };

const ctx = await enrichSimulation(sim, txParams, 1);

const target = '0x173272739bd7aa6e4e214714048a9fe699453059';
const reads = ctx.resolvedReads?.get(target) ?? [];
console.log(`OTUP has ${reads.length} resolved reads:`);
for (const r of reads) {
    const vn = r.resolved?.variableName;
    const members = r.resolved?.members?.map(m => m.variableName).join(',') || '';
    const base = r.resolved?.baseSlot;
    const keys = r.resolved?.keys?.map(k => `${k.type}:${k.value}`).join(',') || '';
    console.log(`  slot=${r.slot}`);
    console.log(`    varName=${vn ?? ''}  members=[${members}]  base=${base}  keys=[${keys}]`);
}
