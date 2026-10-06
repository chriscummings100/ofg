// Dedicated CPU worker: initialize a small WASM module and yield between cancellable meshing batches.
import createGenerator from './terrain-generator.mjs';

let module;
let current;
let scheduled = false;
// Message tasks yield to cancel/release messages without the delay imposed on nested zero-delay timers.
const continuation = new MessageChannel();
continuation.port1.onmessage = step;

// Schedules exactly one bounded batch, allowing cancel/release messages to run between batches.
function schedule() {
    if (scheduled || !current || current.held) return;
    scheduled = true;
    continuation.port2.postMessage(null);
}

// Publishes one terminal result and drops worker-owned allocations before accepting another job.
function finish(outcome, error = '') {
    const message = { type: 'result', wire: current.wire, outcome, error, worker: current.worker };
    const transfer = [message.wire.buffer];
    if (outcome === 0) {
        const vp = module._terrainVertices(), vb = module._terrainVertexBytes();
        const ip = module._terrainIndices(), ib = module._terrainIndexBytes();
        message.vertices = module.HEAPU8.slice(vp, vp + vb);
        message.indices = module.HEAPU8.slice(ip, ip + ib);
        const parts = module._terrainPartOffsets();
        message.partOffsets = new Uint32Array(module.HEAPU8.slice(parts, parts + 32).buffer);
        message.certifiedEmpty = Boolean(module._terrainCertifiedEmpty());
        transfer.push(message.vertices.buffer, message.indices.buffer, message.partOffsets.buffer);
    }
    module._terrainDiscard();
    current = undefined;
    postMessage(message, transfer);
}

// Runs on this worker only; exceptions become request diagnostics, never silent stalled jobs.
function step() {
    scheduled = false;
    if (!current) return;
    if (current.cancelled) { finish(1); return; }
    if (current.held) return;
    try {
        if (!current.started) {
            if (current.fail) { finish(2, 'Injected terrain generation failure.'); return; }
            const pointer = module._malloc(current.wire.byteLength);
            try {
                module.HEAPU8.set(current.wire, pointer);
                if (module._terrainBegin(pointer) < 0) {
                    finish(2, module.UTF8ToString(module._terrainError()));
                    return;
                }
            } finally { module._free(pointer); }
            current.started = true;
        }
        const status = module._terrainStep();
        if (status < 0) finish(2, module.UTF8ToString(module._terrainError()));
        else if (status > 0) finish(0);
        else schedule();
    } catch (error) { finish(2, String(error)); }
}

onmessage = ({data}) => {
    if (data.type === 'build') {
        if (current) throw new Error('Terrain worker received overlapping requests.');
        const flags = new DataView(data.wire.buffer).getUint32(96, true);
        current = { ...data, held: Boolean(flags & 1), fail: Boolean(flags & 2), started: false };
        schedule();
    } else if (data.type === 'cancel' && current?.key === data.key) {
        current.cancelled = true;
        current.held = false;
        schedule();
    } else if (data.type === 'release' && current) {
        current.held = false;
        schedule();
    }
};

try {
    module = await createGenerator();
    postMessage({type: 'ready'});
} catch (error) { postMessage({type: 'fatal', error: String(error)}); }
