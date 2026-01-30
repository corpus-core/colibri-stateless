const path = require("node:path");
const fs = require("node:fs");
const { getDefaultConfig } = require("expo/metro-config");
const { resolve } = require("metro-resolver");

const projectRoot = __dirname;
const colibriBuildDir = path.resolve(projectRoot, "../../../build/emscripten");
const colibriBuildDirReal = fs.existsSync(colibriBuildDir)
  ? fs.realpathSync(colibriBuildDir)
  : colibriBuildDir;
const colibriEntry = path.join(colibriBuildDirReal, "index.js");

const config = getDefaultConfig(projectRoot);

// Metro can struggle with scoped packages + `package.json` exports.
// Resolve Colibri explicitly to the build artifact entrypoint.
// Also ensure the build folder is watched so Metro can compute file hashes.
config.watchFolders = [
  ...(config.watchFolders ?? []),
  colibriBuildDir,
  colibriBuildDirReal,
];

// Ensure resolution always includes this project's node_modules, even when the
// origin module is outside the project root (e.g. from the Colibri build dir).
config.resolver.nodeModulesPaths = [path.join(projectRoot, "node_modules")];
config.resolver.disableHierarchicalLookup = true;

// Explicitly map Babel runtime helpers used by Metro's transform pipeline.
config.resolver.extraNodeModules = {
  "@babel/runtime": path.join(projectRoot, "node_modules", "@babel", "runtime"),
};
config.resolver.resolveRequest = (context, moduleName, platform) => {
  if (moduleName === "@corpus-core/colibri-stateless") {
    return { type: "sourceFile", filePath: colibriEntry };
  }
  return resolve(context, moduleName, platform);
};

module.exports = config;

