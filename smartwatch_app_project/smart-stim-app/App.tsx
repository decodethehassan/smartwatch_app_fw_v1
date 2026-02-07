import React from "react";
import { NavigationContainer } from "@react-navigation/native";
import { ActivityIndicator, View } from "react-native";

import { BLEProvider } from "./src/functionality/BLEContext";
import { AuthProvider, useAuth } from "./src/auth/AuthContext";

import AuthScreen from "./src/screens/AuthScreen";
import MainTabs from "./src/screens/MainTabs";

function Root() {
  const { user, loading } = useAuth();

  if (loading) {
    return (
      <View style={{ flex: 1, justifyContent: "center" }}>
        <ActivityIndicator size="large" />
      </View>
    );
  }

  return user ? <MainTabs /> : <AuthScreen />;
}

export default function App() {
  return (
    <AuthProvider>
      <BLEProvider>
        <NavigationContainer>
          <Root />
        </NavigationContainer>
      </BLEProvider>
    </AuthProvider>
  );
}
