// Incremental builds may leave an already prepared runtime in place.
import { readFileSync, writeFileSync } from 'node:fs';
const [bundlePath, runtimePath] = process.argv.slice(2);
const bundle = readFileSync(bundlePath);
const runtime = readFileSync(runtimePath);
if (!runtime.subarray(0, bundle.length).equals(bundle)) {
  writeFileSync(runtimePath, Buffer.concat([bundle, runtime]));
}
