// Chimera AudioWorkletProcessor. The Emscripten glue (createChimera) is prepended to this file
// by web/build.sh. The wasm binary arrives in processorOptions.wasm and is compiled here
// synchronously (allowed off the main thread), so no fetch/atob is needed in the worklet.

class ChimeraWorklet extends AudioWorkletProcessor {
    constructor (options) {
        super();
        this.M = null;
        this.queue = [];
        this.meterCountdown = 0;
        this.port.onmessage = (e) => { if (this.M) this.handle (e.data); else this.queue.push (e.data); };

        const wasmBytes = options.processorOptions.wasm;
        createChimera ({
            instantiateWasm: (imports, done) => {
                const module = new WebAssembly.Module (wasmBytes);
                const instance = new WebAssembly.Instance (module, imports);
                done (instance, module);
                return instance.exports;
            },
            print: () => {}, printErr: (t) => this.port.postMessage ({ type: 'log', message: String (t) }),
        }).then ((M) => {
            this.M = M;
            M._chimera_init (sampleRate);
            M._chimera_seed ((Math.random() * 0xffffffff) >>> 0);
            const presets = [];
            const n = M._chimera_preset_count();
            for (let i = 0; i < n; ++i)
                presets.push ({ name: M.UTF8ToString (M._chimera_preset_name (i)),
                                category: M.UTF8ToString (M._chimera_preset_category (i)) });
            this.port.postMessage ({ type: 'ready', presets, json: this.patchJson(),
                                     library: JSON.parse (M.UTF8ToString (M._chimera_library_info())),
                                     presetIndex: M._chimera_current_preset() });
            for (const m of this.queue) this.handle (m);
            this.queue = [];
        }).catch ((err) => this.port.postMessage ({ type: 'error', message: 'Engine failed to start: ' + err }));
    }

    patchJson() { return this.M.UTF8ToString (this.M._chimera_patch_json()); }
    sendPatch() { this.port.postMessage ({ type: 'patch', json: this.patchJson() }); }

    withString (s, fn) {
        const M = this.M;
        const p = M.stringToNewUTF8 (s);
        try { return fn (p); } finally { M._chimera_free (p); }
    }

    withBytes (bytes, fn) {
        const M = this.M;
        const p = M._chimera_alloc (bytes.byteLength);
        M.HEAPU8.set (bytes, p);
        try { return fn (p); } finally { M._chimera_free (p); }
    }

    withFloats (arr, fn) {
        if (! arr) return fn (0);
        const M = this.M;
        const p = M._chimera_alloc (arr.length * 4);
        M.HEAPF32.set (arr, p >> 2);
        try { return fn (p); } finally { M._chimera_free (p); }
    }

    error (message) { this.port.postMessage ({ type: 'error', message }); }

    handle (m) {
        const M = this.M;
        switch (m.type) {
            case 'midi': {
                const d = m.data;
                if (d && d.length >= 1) M._chimera_midi (d[0] | 0, (d[1] | 0) & 127, (d[2] | 0) & 127);
                break;
            }
            case 'patch':
                if (! this.withString (m.json, (p) => M._chimera_set_patch_json (p)))
                    this.error ('Patch not applied: ' + M.UTF8ToString (M._chimera_last_error()));
                break;
            case 'macro': M._chimera_macro (m.index | 0, +m.value); break;
            case 'panic': M._chimera_panic(); break;
            case 'preset':
                if (M._chimera_load_preset (m.index | 0)) {
                    this.port.postMessage ({ type: 'presetIndex', index: m.index | 0 });
                    this.sendPatch();
                }
                break;
            case 'randomPreset': {
                const i = M._chimera_random_preset();
                this.port.postMessage ({ type: 'presetIndex', index: i });
                this.sendPatch();
                break;
            }
            case 'randomPatch':
                M._chimera_random_patch();
                this.port.postMessage ({ type: 'presetIndex', index: -1 });
                this.sendPatch();
                break;
            case 'cart': {
                const bytes = new Uint8Array (m.bytes);
                const n = this.withBytes (bytes, (p) => this.withString (m.name || 'Cart', (np) => M._chimera_load_cart (m.layer | 0, p, bytes.length, np)));
                if (n > 0) this.sendPatch();
                else this.error ((m.name || 'That file') + ': ' + M.UTF8ToString (M._chimera_last_error()));
                break;
            }
            case 'builtinCart': M._chimera_builtin_cart (m.layer | 0); this.sendPatch(); break;
            case 'libraryCart': M._chimera_library_cart (m.layer | 0, m.cart | 0, 0); this.sendPatch(); break;
            case 'randomLibraryVoice': {
                const i = M._chimera_random_library_voice (m.layer | 0, m.sameCategory ? 1 : 0);
                this.port.postMessage ({ type: 'cartVoiceIndex', layer: m.layer | 0, index: i });
                this.sendPatch();
                break;
            }
            case 'cartVoice': M._chimera_cart_voice (m.layer | 0, m.index | 0); this.sendPatch(); break;
            case 'randomCartVoice': {
                const i = M._chimera_random_cart_voice (m.layer | 0);
                this.port.postMessage ({ type: 'cartVoiceIndex', layer: m.layer | 0, index: i });
                this.sendPatch();
                break;
            }
            case 'saveCart': {
                const n = M._chimera_save_cart (m.layer | 0);
                const p = M._chimera_bytes();
                const out = M.HEAPU8.slice (p, p + n);
                this.port.postMessage ({ type: 'cartFile', layer: m.layer | 0, bytes: out.buffer, name: 'chimera-cart.syx' }, [out.buffer]);
                break;
            }
            case 'sample': {
                const left = m.left, right = m.right || null;
                if (! left || left.length < 4) { this.error ('That sample is empty.'); break; }
                const ok = this.withFloats (left, (lp) => this.withFloats (right, (rp) =>
                    this.withString (m.name || 'Sample', (np) =>
                        M._chimera_load_sample (m.layer | 0, lp, rp, left.length, +m.sampleRate || sampleRate, np))));
                if (ok) this.sendPatch(); else this.error ('Could not load that sample.');
                break;
            }
            default: break;
        }
    }

    process (inputs, outputs) {
        const out = outputs[0];
        if (! this.M || ! out || out.length === 0) return true;
        const n = out[0].length;
        const p = this.M._chimera_render (n) >> 2;
        const heap = this.M.HEAPF32;   // re-read: memory growth replaces the view
        out[0].set (heap.subarray (p, p + n));
        if (out.length > 1) out[1].set (heap.subarray (p + 128, p + 128 + n));
        if ((this.meterCountdown -= n) <= 0) {
            this.meterCountdown = Math.round (sampleRate / 20);
            this.port.postMessage ({ type: 'meter', voices: this.M._chimera_voices(), peak: this.M._chimera_peak() });
        }
        return true;
    }
}

registerProcessor ('chimera', ChimeraWorklet);
