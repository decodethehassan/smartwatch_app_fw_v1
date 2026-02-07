import React, { useState } from "react";
import { View, Text, TextInput, Button, Alert } from "react-native";
import {
  createUserWithEmailAndPassword,
  signInWithEmailAndPassword,
} from "firebase/auth";
import { auth } from "../firebase/firebaseConfig";

export default function AuthScreen() {
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");

  const login = async () => {
    try {
      await signInWithEmailAndPassword(auth, email, password);
    } catch (err: any) {
      Alert.alert("Login Error", err.message);
    }
  };

  const register = async () => {
    try {
      await createUserWithEmailAndPassword(auth, email, password);
    } catch (err: any) {
      Alert.alert("Register Error", err.message);
    }
  };

  return (
    <View style={{ padding: 20, marginTop: 80 }}>
      <Text style={{ fontSize: 24, fontWeight: "bold" }}>
        Smartwatch Login
      </Text>

      <TextInput
        placeholder="Email"
        autoCapitalize="none"
        style={{ borderWidth: 1, marginTop: 20, padding: 10 }}
        value={email}
        onChangeText={setEmail}
      />

      <TextInput
        placeholder="Password"
        secureTextEntry
        style={{ borderWidth: 1, marginTop: 10, padding: 10 }}
        value={password}
        onChangeText={setPassword}
      />

      <View style={{ marginTop: 20 }}>
        <Button title="Login" onPress={login} />
      </View>

      <View style={{ marginTop: 10 }}>
        <Button title="Register" onPress={register} />
      </View>
    </View>
  );
}
