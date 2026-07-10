const path = require("node:path");

let emscriptenBuildDir;
try {
  ({ emscriptenBuildDir } = require("./rn_web_test_build_config.cjs"));
} catch (e) {
  throw new Error(
    "Missing generated `rn_web_test_build_config.cjs`. " +
      "Build the Emscripten package first (CMake will generate this file)."
  );
}

const escapeForRegex = (p) => p.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
const buildDirRegex = escapeForRegex(path.resolve(emscriptenBuildDir));

module.exports = {
  preset: "jest-expo",
  testEnvironment: "node",
  testMatch: ["**/__tests__/**/*.test.js"],
  // Always load the CJS entry for Node/Jest.
  moduleNameMapper: {
    "^@corpus-core/colibri-stateless$": path.join(
      emscriptenBuildDir,
      "cjs",
      "index.js"
    ),
  },
  // Don't transform the generated Emscripten/TS build output. Transforming it
  // can inject Babel runtime helpers and cause resolution issues.
  transformIgnorePatterns: [
    `^${buildDirRegex}.*`,
    "node_modules/(?!((jest-)?react-native|@react-native(-community)?|expo(nent)?|@expo(nent)?/.*|expo-modules-core|@expo/.*)/)",
  ],
};

