// SPDX-License-Identifier: GPL-3.0-only
import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import {WclapModule, EVENT, PROCESS} from './host/clap-host.mjs';
import {memoryFiles} from './host/wasi.mjs';

const bytes = fs.readFileSync(new URL('../../build/wasi-module/wclap/module.wasm', import.meta.url));
const compiled = await WebAssembly.compile(bytes);
const xml = fs.readFileSync(new URL('./fixtures/single.xml', import.meta.url), 'utf8');
function binary(text) {
  const data = Buffer.from(text), state = Buffer.alloc(9 + data.length);
  state.writeUInt32LE(0x21324356); state.writeUInt32LE(data.length, 4);
  state.set(data, 8); return state;
}
const fixture = Buffer.from(fs.readFileSync(new URL('./fixtures/single.compact.hex', import.meta.url), 'utf8').trim(), 'hex');
async function create(state = fixture) {
  const module = await WclapModule.load({module: compiled, files: memoryFiles(), user: memoryFiles(), seed: 42});
  const plugin = module.createPlugin('org.surge-synth-team.stochas');
  assert.equal(plugin.loadState(state), true);
  return {module, plugin};
}
test('DSP module has only WASI imports and no browser entrypoints', () => {
  for (const entry of WebAssembly.Module.imports(compiled)) assert.equal(entry.module, 'wasi_snapshot_preview1');
  const names = WebAssembly.Module.exports(compiled).map(entry => entry.name);
  assert.deepEqual(names.sort(), ['memory', 'clap_entry', '__indirect_function_table', 'malloc', 'free', '_initialize', '__trap'].sort());
});
test('actual descriptor, native parameter IDs/text, MIDI ports and zero audio ABI', async () => {
  const {module, plugin: p} = await create();
  try {
    assert.equal(module.descriptors()[0].id, 'org.surge-synth-team.stochas');
    assert.equal(p.paramCount(), 55);
    assert.deepEqual(p.audioPorts(true), []); assert.deepEqual(p.audioPorts(false), []);
    assert.equal(p.notePorts(true)[0].id, 32); assert.equal(p.notePorts(false)[0].id, 4);
    for (const id of [1569,1603,1637,1692]) {
      for (let pattern = 1; pattern <= 8; ++pattern) {
        assert.equal(p.paramFromText(id, String(pattern)), pattern / 8);
        assert.equal(p.paramText(id, pattern / 8), String(pattern));
      }
    }
    assert.equal(p.paramFromText(1695, 'stop'), 0.5);
    assert.equal(p.paramFromText(1695, 'play'), 1);
    p.activate(48000, 1, 512); assert.equal(p.latency(), 0);
  } finally { p.destroy(); }
});
test('portable compact runtime state roundtrips exactly; refused state is transactional', async () => {
  const {plugin: p} = await create();
  try {
    assert.deepEqual(Buffer.from(p.saveState()), fixture);
    const reserved = Buffer.from(fixture); reserved[6] = 1;
    const invalidRows = Buffer.from(fixture); invalidRows.writeInt32LE(999, 668);
    for (const state of [Buffer.alloc(0), Buffer.from('bad'), binary(xml), reserved, invalidRows,
      fixture.subarray(0, fixture.length - 1), Buffer.concat([fixture, Buffer.from([0])])]) {
      assert.equal(p.loadState(state), false);
      assert.deepEqual(Buffer.from(p.saveState()), fixture);
    }
  } finally { p.destroy(); }
});
test('all native parameter IDs/values retain original XML-state load semantics', async () => {
  const {plugin: p} = await create();
  try {
    p.activate(48000, 1, 512);
    const values = Array.from({length: p.paramCount()}, (_, index) => ({
      type: EVENT.PARAM_VALUE, time: 0, id: p.paramInfo(index).id, value: Math.fround((index + 1) / 64),
    }));
    p.flush(values);
    // Original JUCE parameters quantize into their integer control ranges.
    // Compare against their native text/value conversion, not raw input floats.
    const expected = values.map(event => p.paramFromText(event.id, p.paramText(event.id, event.value)));
    for (let index = 0; index < values.length; ++index)
      assert.equal(p.paramValue(values[index].id), expected[index], `${values[index].id}: ${p.paramInfo(index).name} text/value conversion`);
    assert.equal(p.loadState(fixture), true);
    for (let index = 0; index < values.length; ++index)
      assert.equal(p.paramValue(values[index].id), expected[index]);
  } finally { p.destroy(); }
});
for (const block of [128,512]) {
  test(`native fixed-point transport and exact MIDI offsets (${block} frames)`, async () => {
    const {plugin: p} = await create();
    try {
      p.activate(48000, 1, 512);
      const events = [];
      for (let frame = 0; frame < 192000; frame += block) {
        const result = p.process(block, {outputs: 0, transport: {
          playing: true, tempo: 120, beatsFixed: BigInt(Math.trunc((frame / 24000) * 2**31)),
        }});
        assert.equal(result.status, PROCESS.CONTINUE);
        assert.deepEqual(result.buses, []);
        events.push(...result.events.map(event => [frame + event.time, ...event.data]));
      }
      assert.deepEqual(events, [[0,144,60,100],[3000,128,60,0],[96000,144,60,100],[99000,128,60,0]]);
    } finally { p.destroy(); }
  });
}
test('MIDI passthrough preserves first/last sample and one-frame blocks', async () => {
  const passthrough = Buffer.from(fixture);
  passthrough.writeInt32LE(3, 34); passthrough.writeInt32LE(0, 38);
  const {plugin: p} = await create(passthrough);
  try {
    p.activate(48000, 1, 512);
    for (const count of [1,2,17,512]) {
      const events = [{type: EVENT.MIDI, time: 0, data: [0x90, 72, 99]},
        {type: EVENT.MIDI, time: count-1, data: [0x80, 72, 0]}];
      const output = p.process(count, {outputs: 0, events, transport: {playing:false, tempo:120, beats:0}});
      assert.equal(output.status, PROCESS.CONTINUE);
      assert.deepEqual(output.events.map(event => ({type:event.type,time:event.time,data:event.data})), events);
    }
  } finally { p.destroy(); }
});

test('editor-closed MIDI recording survives immediate project save and fresh reload', async () => {
  const {plugin: p} = await create();
  let recorded;
  try {
    p.activate(48000, 1, 512);
    p.flush([{type: EVENT.PARAM_VALUE, time: 0, id: 1694, value: 1}]);
    // No GUI, webview, telemetry polling, or main-thread callback service. A
    // project save must commit completed recordings even with the editor closed.
    for (let frame = 0; frame < 4096; frame += 512) {
      const events = frame === 0 ? [{type: EVENT.MIDI, time: 0, data: [0x90, 72, 99]}]
        : frame === 2048 ? [{type: EVENT.MIDI, time: 0, data: [0x80, 72, 0]}] : [];
      const result = p.process(512, {outputs: 0, events, transport: {
        playing: true, tempo: 120, beatsFixed: BigInt(Math.trunc(frame / 24000 * 2**31)),
      }});
      assert.equal(result.status, PROCESS.CONTINUE);
    }
    recorded = Buffer.from(p.saveState());
    assert.notDeepEqual(recorded, fixture);
  } finally { p.destroy(); }
  const {plugin: reloaded} = await create(recorded);
  try {
    assert.deepEqual(Buffer.from(reloaded.saveState()), recorded);
    reloaded.activate(48000, 1, 512);
    const result = reloaded.process(512, {outputs: 0, transport: {
      playing: true, tempo: 120, beatsFixed: 0n,
    }});
    assert.equal(result.status, PROCESS.CONTINUE);
    assert.deepEqual(result.events.map(event => [event.time, ...event.data]), [[0, 0x90, 72, 99]]);
  } finally { reloaded.destroy(); }
});

test('host event refusal is explicit and reset permits deterministic retry', async () => {
  const {plugin: p} = await create();
  try {
    p.activate(48000, 1, 512);
    const transport = {playing: true, tempo: 120, beatsFixed: 0n};
    const refused = p.process(512, {outputs: 0, transport, outputLimit: 0});
    assert.equal(refused.status, PROCESS.ERROR);
    assert.deepEqual(refused.events, []);
    p.reset();
    const retried = p.process(512, {outputs: 0, transport});
    assert.equal(retried.status, PROCESS.CONTINUE);
    // The processor cannot know how many earlier events a refusing host kept.
    // Reset releases the attempted voice before replaying the preserved pattern.
    assert.deepEqual(retried.events.map(event => [event.time, ...event.data]),
      [[0, 0x80, 60, 0], [0, 0x90, 60, 100]]);
    assert.deepEqual(Buffer.from(p.saveState()), fixture);
  } finally { p.destroy(); }
});
