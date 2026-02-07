import { initializeApp } from "firebase/app";
import {
  initializeAuth,
  getReactNativePersistence,
} from "firebase/auth";
import { getFirestore } from "firebase/firestore";
import AsyncStorage from "@react-native-async-storage/async-storage";

const firebaseConfig = {
  apiKey: "AIzaSyB6Pmc8kbPNltkHhhGIn6L1-IA1ocDaMMs",
  authDomain: "smartwatch-app-dfaeb.firebaseapp.com",
  projectId: "smartwatch-app-dfaeb",
  storageBucket: "smartwatch-app-dfaeb.firebasestorage.app",
  messagingSenderId: "773653416632",
  appId: "1:773653416632:web:6430931e9b5f8a359b424d",
};
const app = initializeApp(firebaseConfig);

// ✅ FIXED AUTH INITIALIZATION FOR REACT NATIVE
export const auth = initializeAuth(app, {
  persistence: getReactNativePersistence(AsyncStorage),
});

export const db = getFirestore(app);
