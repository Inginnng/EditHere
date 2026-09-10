import { detectRegions } from './detector.js';
self.onmessage = ({ data }) => { try { self.postMessage({ id: data.id, candidates: detectRegions(new Uint8ClampedArray(data.buffer), data.width, data.height, data.originalWidth, data.originalHeight) }); } catch (error) { self.postMessage({ id: data.id, error: error.message }); } };
