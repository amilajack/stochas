// SPDX-License-Identifier: GPL-3.0-or-later
// Loaded only by the WCLAP UI. Emscripten runs main() on its application pthread;
// DOM canvases are transferred to that pthread before JUCE paints them.
if (ENVIRONMENT_IS_PTHREAD) {
  const canvases = new Map();
  const sizes = new Map();
  const pending = new Map();
  let paintCalls = 0;
  globalThis.StochasWorkerCanvas = {
    count() { return canvases.size; },
    paints() { return paintCalls; },
    prepare(id, width, height) {
      sizes.set(id, [width, height]);
      pending.delete(id);
      const canvas = canvases.get(id)?.canvas;
      if (canvas && (canvas.width !== width || canvas.height !== height)) {
        canvas.width = width;
        canvas.height = height;
      }
    },
    paint(id, x, y, width, height, pointer) {
      ++paintCalls;
      const bytes = HEAPU8.subarray(pointer, pointer + width * height * 4);
      const entry = canvases.get(id);
      if (!entry) {
        // A newly transferred canvas arrives in a later worker event. Stage one
        // backing image per peer so partial repaints cannot be lost or queued
        // without bound while the transfer is in flight.
        const [canvasWidth, canvasHeight] = sizes.get(id) || [width, height];
        let image = pending.get(id);
        if (!image) {
          image = new Uint8ClampedArray(canvasWidth * canvasHeight * 4);
          pending.set(id, image);
        }
        for (let row = 0; row < height; ++row)
          image.set(bytes.subarray(row * width * 4, (row + 1) * width * 4),
            ((y + row) * canvasWidth + x) * 4);
        return;
      }
      const image = entry.image?.width === width && entry.image.height === height
        ? entry.image : (entry.image = entry.context.createImageData(width, height));
      image.data.set(bytes);
      entry.context.putImageData(image, x, y);
    },
    remove(id) { canvases.delete(id); sizes.delete(id); pending.delete(id); },
  };
  addEventListener('message', event => {
    if (event.data?.stochasCall) {
      const {id, name, returnType, argTypes, args} = event.data.stochasCall;
      try {
        const value = Module.ccall(name, returnType, argTypes, args);
        postMessage({stochasReply: {id, value}});
      } catch (error) {
        postMessage({stochasReply: {id, error: String(error)}});
      }
      return;
    }
    if (event.data?.stochasMessage instanceof ArrayBuffer) {
      const bytes = new Uint8Array(event.data.stochasMessage);
      const pointer = Module._stochas_wclap_ui_buffer(bytes.length);
      if (!pointer) { postMessage({stochasFailure: 'An incoming plugin message exceeded the editor limit'}); return; }
      HEAPU8.set(bytes, pointer);
      Module._stochas_wclap_ui_receive(pointer, bytes.length);
      postMessage({stochasMessageDone: true});
      return;
    }
    if (event.data?.stochasCanvas) {
      const {id, canvas} = event.data.stochasCanvas;
      const [width, height] = sizes.get(id) || [canvas.width, canvas.height];
      canvas.width = width;
      canvas.height = height;
      const context = canvas.getContext('2d', {alpha: true});
      if (!context) throw new Error('Stochas cannot create a worker canvas context');
      canvas.addEventListener('contextlost', () =>
        postMessage({stochasFailure: 'The graphics context was lost'}));
      canvases.set(id, {canvas, context, image: null});
      const frame = pending.get(id);
      pending.delete(id);
      if (frame) {
        const image = context.createImageData(width, height);
        image.data.set(frame);
        context.putImageData(image, 0, 0);
      }
    }
  });
}
