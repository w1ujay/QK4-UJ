// Bundles the main service and its dependencies (the SDK and ws) into dist/app.js, because Ulanzi
// Studio installs no npm dependencies. dist/ is committed so installing the plugin is copying the folder.
import { build } from 'esbuild';
import { mkdir, writeFile } from 'node:fs/promises';

await mkdir('dist', { recursive: true });
await build({
  bundle: true,
  entryPoints: ['plugin/app.js'],
  format: 'cjs',
  outfile: 'dist/app.js',
  platform: 'node',
  target: 'node20',
  legalComments: 'inline',
  // ws loads these optional native speed-ups inside try/catch; they are not installed.
  external: ['bufferutil', 'utf-8-validate'],
});
// The package root is "type": "module"; the bundle is CommonJS.
await writeFile('dist/package.json', '{\n  "type": "commonjs"\n}\n');
