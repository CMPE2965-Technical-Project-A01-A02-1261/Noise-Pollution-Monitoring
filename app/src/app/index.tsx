import { useState } from "react";
import { Text, View, StyleSheet, Pressable, TextInput } from "react-native";
import { GetStatus } from "../services/GetStatus";

export default function Index() {
  const [password, setPassword] = useState("");
  const [isSending, setIsSending] = useState(false);

  const requestStatus = async () => {
    setIsSending(true);
    try {
      await GetStatus(password);
    } finally {
      setIsSending(false);
    }
  };

  return (
    <View style={styles.container}>
      <TextInput
        accessibilityLabel="XBee BLE password"
        autoCapitalize="none"
        autoCorrect={false}
        onChangeText={setPassword}
        placeholder="XBee BLE password"
        secureTextEntry
        style={styles.passwordInput}
        value={password}
      />
      <Pressable
        accessibilityRole="button"
        disabled={isSending}
        onPress={requestStatus}
        style={styles.button}
      >
        <Text style={styles.buttonText}>{isSending ? "Connecting..." : "Get Status"}</Text>
      </Pressable>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    alignItems: "center",
    justifyContent: "center",
    padding: 24,
  },
  passwordInput: {
    alignSelf: "stretch",
    borderColor: "#777",
    borderRadius: 8,
    borderWidth: 1,
    marginBottom: 16,
    padding: 12,
  },
  button: {
    alignItems: "center",
    backgroundColor: "#208AEF",
    borderRadius: 8,
    paddingHorizontal: 20,
    paddingVertical: 12,
  },
  buttonText: {
    color: "#fff",
    fontWeight: "600",
  },
});
