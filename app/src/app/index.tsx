import { Text, View, StyleSheet, Pressable } from "react-native";
import { GetStatus } from "../services/GetStatus";
export default function Index() {
  return (
    <View style={styles.container}>
      <Pressable onPress={GetStatus} >
        <Text>Get Status</Text>
      </Pressable>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    alignItems: "center",
    justifyContent: "center",
  },
});
