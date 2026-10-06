// Application-side worker transport; callbacks only enqueue results, never reenter suspended application WASM.
globalThis.TerrainWorkerPool = class {
    constructor() {
        this.jobs = new Map();
        this.queue = [];
        this.results = [];
        this.workers = [];
        this.failures = [];
        for (let index = 0; index < 2; ++index) {
            const slot = {index, ready: false, job: null, failed: false,
                worker: new Worker('terrain-worker.js', {type: 'module'})};
            slot.worker.onmessage = ({data}) => {
                if (data.type === 'ready') { slot.ready = true; this.pump(); }
                else if (data.type === 'fatal') this.failWorker(slot, data.error);
                else if (data.type === 'result') {
                    if (!slot.job) { this.failWorker(slot, 'Unexpected terrain worker result.'); return; }
                    data.key = slot.job.key;
                    this.results.push(data);
                    slot.job = null;
                    this.pump();
                }
            };
            slot.worker.onerror = event => { event.preventDefault(); this.failWorker(slot, event.message); };
            this.workers.push(slot);
        }
    }
    // Keeps all 128 identity bits intact instead of converting request IDs to JavaScript numbers.
    key(wire) { return Array.from(wire.subarray(0,16), b => b.toString(16).padStart(2,'0')).join(''); }
    submit(wire) {
        const key = this.key(wire);
        if (this.jobs.size >= 64 || this.jobs.has(key)) throw new Error('Terrain worker queue full or duplicate identity.');
        const job = {key, wire};
        this.jobs.set(key, job);
        this.queue.push(job);
        this.pump();
    }
    // Sends only to idle initialized workers; transfers a copy because cancellation retains the request descriptor.
    pump() {
        for (const slot of this.workers) {
            if (!slot.ready || slot.failed || slot.job || !this.queue.length) continue;
            slot.job = this.queue.shift();
            const wire = slot.job.wire.slice();
            slot.worker.postMessage({type: 'build', key: slot.job.key, wire, worker: slot.index}, [wire.buffer]);
        }
        if (this.workers.length === 2 && this.workers.every(slot => slot.failed)) {
            for (const job of this.queue.splice(0)) this.results.push({...job, outcome: 2, error: this.failures.join('; ')});
        }
    }
    cancel(identity) {
        const key = this.key(identity);
        const queued = this.queue.findIndex(job => job.key === key);
        if (queued >= 0) {
            this.results.push({...this.queue.splice(queued,1)[0], outcome: 1});
        } else {
            const slot = this.workers.find(slot => slot.job?.key === key);
            if (slot) slot.worker.postMessage({type:'cancel', key});
            else if (!this.jobs.has(key)) {
                const wire = new Uint8Array(104);
                wire.set(identity.subarray(0,16));
                this.results.push({key, wire, outcome:1});
            }
        }
    }
    release() {
        for (const job of this.queue) new DataView(job.wire.buffer).setUint32(96,
            new DataView(job.wire.buffer).getUint32(96,true) & ~1, true);
        for (const slot of this.workers) slot.worker.postMessage({type:'release'});
    }
    failWorker(slot, error) {
        if (slot.failed) return;
        slot.failed = true;
        this.failures.push(String(error));
        slot.worker.terminate();
        if (slot.job) this.results.push({...slot.job, outcome:2, error:String(error)});
        slot.job = null;
        this.pump();
    }
    stop() { for (const slot of this.workers) slot.worker.terminate(); this.jobs.clear(); this.queue=[]; this.results=[]; }
};
