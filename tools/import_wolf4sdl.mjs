// Reproducibly import the GPL Wolf4SDL engine used by the embedded reference port.
import fs from "node:fs/promises";
import path from "node:path";

const owner = "rgomez31UAQ";
const repo = "Flipper-Zero-ESP32_Firmware-Diy";
const revision = "main";
const sourcePrefix = "applications/main/wolf3d/lib/wolf4sdl/";
const destination = path.resolve("lib/Wolf4SDL/src");
const headers = { "User-Agent": "ESP32-CYD-Tester-import" };

const treeResponse = await fetch(
  `https://api.github.com/repos/${owner}/${repo}/git/trees/${revision}?recursive=1`,
  { headers },
);
if (!treeResponse.ok) throw new Error(`GitHub tree request failed: ${treeResponse.status}`);
const tree = await treeResponse.json();
const files = tree.tree.filter(
  (entry) => entry.type === "blob" && entry.path.startsWith(sourcePrefix),
);

await fs.mkdir(destination, { recursive: true });
for (const entry of files) {
  const relative = entry.path.slice(sourcePrefix.length);
  const response = await fetch(
    `https://raw.githubusercontent.com/${owner}/${repo}/${revision}/${entry.path}`,
    { headers },
  );
  if (!response.ok) throw new Error(`Download failed (${response.status}): ${relative}`);
  const target = path.join(destination, relative);
  await fs.mkdir(path.dirname(target), { recursive: true });
  await fs.writeFile(target, Buffer.from(await response.arrayBuffer()));
  process.stdout.write(`Imported ${relative}\n`);
}

const shimFiles = ["SDL.h", "SDL_mixer.h", "SDL_syswm.h", "wolf3d_keymap.h"];
for (const filename of shimFiles) {
  const upstreamPath = `applications/main/wolf3d/${filename}`;
  const response = await fetch(
    `https://raw.githubusercontent.com/${owner}/${repo}/${revision}/${upstreamPath}`,
    { headers },
  );
  if (!response.ok) throw new Error(`Shim download failed (${response.status}): ${filename}`);
  await fs.writeFile(path.join(destination, filename), Buffer.from(await response.arrayBuffer()));
  process.stdout.write(`Imported ${filename}\n`);
}

await fs.writeFile(
  path.resolve("lib/Wolf4SDL/UPSTREAM.md"),
  `# Wolf4SDL upstream\n\nImported from https://github.com/${owner}/${repo}/tree/${revision}/${sourcePrefix}\n\nThe engine retains its upstream GPL and id Software license files in \`src/\`. Game data is not included.\n`,
);
