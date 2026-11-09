// Main-thread side of the Chimera audio engine: decodes the embedded wasm, loads the worklet
// (its source is embedded as text, run from a Blob URL) and returns the AudioWorkletNode.
// web/build.sh fills in CHIMERA_WASM_B64 and the #chimera-worklet-src text.
window.ChimeraAudio = {
    async create (ctx) {
        const b64 = document.getElementById ('chimera-wasm').textContent.trim();
        const bin = atob (b64);
        const wasm = new Uint8Array (bin.length);
        for (let i = 0; i < bin.length; ++i) wasm[i] = bin.charCodeAt (i);
        await WebAssembly.compile (wasm);   // fail early, with a clear error, on browsers without the features we use
        const src = document.getElementById ('chimera-worklet-src').textContent;
        const url = URL.createObjectURL (new Blob ([src], { type: 'text/javascript' }));
        try { await ctx.audioWorklet.addModule (url); } finally { URL.revokeObjectURL (url); }
        return new AudioWorkletNode (ctx, 'chimera', {
            numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
            processorOptions: { wasm: wasm.buffer },
        });
    },
};
