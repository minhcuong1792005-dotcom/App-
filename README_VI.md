# Smart Garden Flutter + Firebase + ESP32

Ứng dụng Flutter Android để theo dõi 2 cảm biến đất, độ ẩm trung bình, nhiệt độ, độ ẩm không khí, trạng thái bơm/cảnh báo; bật/tắt bơm và chỉnh ngưỡng tự động qua Firebase RTDB.

## Quan trọng trước khi build
Trong `lib/firebase_options.dart`, `apiKey` và `databaseURL` lấy từ tệp ESP32 bạn gửi. Tuy nhiên `appId` và `messagingSenderId` Android hiện là giá trị giữ chỗ vì chưa có `google-services.json` hoặc Firebase Android App ID thật. Bạn PHẢI cấu hình Firebase Android app trước khi build chạy thật:
1. Firebase Console → Project settings → Your apps → Add app → Android.
2. Package name: `com.example.smart_garden_flutter` (hoặc package bạn chọn trong `android/app/build.gradle`).
3. Tải `google-services.json`; hoặc chạy `dart pub global activate flutterfire_cli`, `flutterfire configure --project=tuoicayiot-cc958`, rồi dùng file `firebase_options.dart` do FlutterFire sinh ra.
4. Không tự đoán `appId`/sender ID. Thay bằng giá trị thật.
5. Authentication → Sign-in method → bật **Anonymous**.
6. Realtime Database → Rules: deploy file `firebase/database.rules.json`.

## Tạo bộ Android đầy đủ
Cài Flutter SDK + Android Studio/Android SDK. Trong thư mục cha:
```bash
flutter create --platforms=android --org com.example smart_garden_flutter
```
Sau đó chép đè `lib/`, `pubspec.yaml`, và `android/app/src/main/AndroidManifest.xml` từ gói này vào project vừa tạo. Giữ các file Gradle/Gradle wrapper do `flutter create` tạo ra.
Tiếp:
```bash
cd smart_garden_flutter
flutter pub get
flutterfire configure --project=tuoicayiot-cc958
flutter run
```
Chọn điện thoại Android đã bật Developer options → USB debugging, kết nối cáp USB và chấp nhận thông báo cho phép gỡ lỗi.

## Xuất APK
Khi app chạy được trên máy thật:
```bash
flutter build apk --release
```
APK nằm ở:
`build/app/outputs/flutter-apk/app-release.apk`
Chép APK sang điện thoại, mở file và cho phép cài ứng dụng từ nguồn đó nếu Android hỏi.

## Rules
Rule bạn đưa ban đầu (`.read: true`, `.write: true`) cho phép mọi người trên Internet đọc/ghi dữ liệu, rất nguy hiểm. Dùng tối thiểu:
```json
{
  "rules": {
    ".read": "auth != null",
    ".write": "auth != null"
  }
}
```
Quy tắc này yêu cầu đăng nhập Firebase, nhưng bất kỳ người dùng nào đăng nhập Anonymous vẫn có thể đọc/ghi toàn bộ database. Phù hợp demo/đồ án, chưa đủ an toàn cho sản phẩm thực tế. Cần siết theo UID/path và cân nhắc App Check.

## Kiểm tra ESP32
- Wi-Fi 2.4 GHz; SSID/password không được để nguyên `YOUR_WIFI_NAME`.
- API key và database URL phải khớp project.
- Dùng thư viện `Firebase_ESP_Client` của Mobizt như code hiện tại.
- Mở Serial Monitor 115200 baud. Kiểm tra `Firebase Anonymous Auth: OK`, lỗi signup/token và `Firebase status error`.
- Đảm bảo Firebase Database URL đúng region của RTDB trong Firebase Console.
- `Firebase.ready()` ngay sau `Firebase.begin()` có thể chưa sẵn sàng; không nên chỉ khởi tạo dữ liệu một lần trong `setup()`. Code nên khởi tạo mặc định trong `loop()` sau khi `Firebase.ready()` true.
- Trước khi thử điều khiển bơm, tháo tải nguy hiểm hoặc thử relay không nối máy bơm để xác nhận relay active HIGH/LOW đúng; thêm giới hạn thời gian bơm.

## Cấu trúc dữ liệu app đọc/ghi
- `/status`: soil1, soil2, soilAvg, temperature, humidity, pumpState, updatedAt
- `/control/manualPump`: boolean
- `/control/threshold`: số nguyên 10–90
- `/alerts/latest`: type, message, timestamp

## Giới hạn
Gói này chứa mã nguồn Flutter và hướng dẫn cấu hình. Chưa thể cung cấp APK đã build và kiểm thử trong gói này: Firebase Android App ID chưa được cung cấp và cần build bằng Flutter SDK trên máy có Android toolchain. Sau khi cấu hình đúng, build APK theo lệnh trên.
