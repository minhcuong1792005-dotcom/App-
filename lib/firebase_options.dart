// Demo configuration for project tuoicayiot-cc958.
// Confirm API key and database URL in Firebase Console before release.
// For production, prefer `flutterfire configure` and replace this file.
import 'package:firebase_core/firebase_core.dart';
import 'package:flutter/foundation.dart';

class DefaultFirebaseOptions {
  static FirebaseOptions get currentPlatform {
    if (kIsWeb) {
      throw UnsupportedError('This project is configured for Android mobile.');
    }
    return const FirebaseOptions(
      apiKey: 'AIzaSyCIgA2b3SXkoUX3cid8v_HJdd8U-C2UW-M',
      appId: '1:470013552042:android:88f13f2e8335cbe90db7d3',
      messagingSenderId: '470013552042',
      projectId: 'tuoicayiot-cc958',
      databaseURL: 'https://tuoicayiot-cc958-default-rtdb.firebaseio.com',
    );
  }
}
