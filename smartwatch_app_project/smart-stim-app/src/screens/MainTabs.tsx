import React from "react";
import { createBottomTabNavigator } from "@react-navigation/bottom-tabs";
import DeviceScanner from "../components/DeviceScanner";
import { View, Text } from "react-native";

const Tab = createBottomTabNavigator();

const Placeholder = ({ title }: { title: string }) => (
  <View style={{ flex: 1, alignItems: "center", justifyContent: "center" }}>
    <Text>{title} (Coming Soon)</Text>
  </View>
);

export default function MainTabs() {
  return (
    <Tab.Navigator>
      <Tab.Screen name="Devices" component={DeviceScanner} />
      <Tab.Screen name="Stim" children={() => <Placeholder title="Stim" />} />
      <Tab.Screen name="Sensors" children={() => <Placeholder title="Sensors" />} />
    </Tab.Navigator>
  );
}
