import React from "react";
import { Text, View } from "react-native";
import Colibri from "@corpus-core/colibri-stateless";

export default function App() {
  // Import-time smoke check. The actual WASM is initialized lazily on first use.
  // eslint-disable-next-line no-new
  new Colibri();

  return (
    <View style={{ padding: 24 }}>
      <Text>Colibri RN-web test</Text>
    </View>
  );
}

