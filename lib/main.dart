import 'package:firebase_auth/firebase_auth.dart';
import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';
import 'firebase_options.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await Firebase.initializeApp(options: DefaultFirebaseOptions.currentPlatform);
  runApp(const SmartGardenApp());
}

class SmartGardenApp extends StatelessWidget {
  const SmartGardenApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'Smart Garden',
    debugShowCheckedModeBanner: false,
    theme: ThemeData(colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF20865B)),
      useMaterial3: true),
    home: const DashboardPage(),
  );
}

class DashboardPage extends StatefulWidget {
  const DashboardPage({super.key});
  @override
  State<DashboardPage> createState() => _DashboardPageState();
}

class _DashboardPageState extends State<DashboardPage> {
  final _db = FirebaseDatabase.instance;
  bool _busy = false;
  String? _error;
  double? _draftThreshold; // gia tri slider khi dang keo

  @override
  void initState() {
    super.initState();
    _signIn();
  }

  Future<void> _signIn() async {
    try {
      if (FirebaseAuth.instance.currentUser == null) {
        await FirebaseAuth.instance.signInAnonymously();
      }
      if (mounted) setState(() => _error = null);
    } catch (e) {
      if (mounted) setState(() => _error = 'Đăng nhập Firebase thất bại: $e');
    }
  }

  Future<void> _setPump(bool on) async {
    setState(() { _busy = true; _error = null; });
    try {
      await _signIn();
      if (on) {
        await _db.ref('control').update({'manualPump': true});
      } else {
        // stopSeq tang moi lan bam DUNG de ESP32 luon nhan duoc lenh,
        // ke ca khi bom dang chay o che do tu dong (manualPump van la false).
        await _db.ref('control').update({
          'manualPump': false,
          'stopSeq': ServerValue.increment(1),
        });
      }
    } catch (e) {
      if (mounted) setState(() => _error = 'Không gửi được lệnh bơm: $e');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _setThreshold(double value) async {
    try {
      await _db.ref('control/threshold').set(value.round());
    } catch (e) {
      if (mounted) setState(() => _error = 'Không cập nhật được ngưỡng: $e');
    } finally {
      if (mounted) setState(() => _draftThreshold = null);
    }
  }

  Stream<DatabaseEvent> _stream(String path) => _db.ref(path).onValue;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('🌱 Smart Garden'), actions: [
        IconButton(tooltip: 'Thử kết nối lại', onPressed: _signIn, icon: const Icon(Icons.refresh))
      ]),
      body: ListView(padding: const EdgeInsets.all(16), children: [
        if (_error != null) Card(color: Theme.of(context).colorScheme.errorContainer,
          child: Padding(padding: const EdgeInsets.all(12), child: Text(_error!))),
        StreamBuilder<DatabaseEvent>(
          stream: _stream('status'),
          builder: (context, snap) {
            if (snap.hasError) return _errorCard('Lỗi đọc /status: ${snap.error}');
            final value = snap.data?.snapshot.value;
            final m = value is Map ? value : <Object?, Object?>{};
            final updated = _num(m['updatedAt']);
            final recent = updated > 0 && DateTime.now().millisecondsSinceEpoch - updated < 20000;
            return Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(
              crossAxisAlignment: CrossAxisAlignment.start, children: [
                Row(children: [const Icon(Icons.sensors), const SizedBox(width: 8),
                  const Text('Trạng thái ESP32', style: TextStyle(fontWeight: FontWeight.bold)),
                  const Spacer(), Chip(label: Text(recent ? 'Đang gửi dữ liệu' : 'Chưa có dữ liệu mới'))]),
                const SizedBox(height: 12),
                Row(children: [
                  Expanded(child: _metric('Độ ẩm đất 1', '${_num(m['soil1']).round()}%', Icons.grass)),
                  Expanded(child: _metric('Độ ẩm đất 2', '${_num(m['soil2']).round()}%', Icons.grass)),
                ]),
                Row(children: [
                  Expanded(child: _metric('Độ ẩm trung bình', '${_num(m['soilAvg']).round()}%', Icons.water_drop)),
                  Expanded(child: _metric('Nhiệt độ', '${_fmt(m['temperature'])} °C', Icons.thermostat)),
                ]),
                Row(children: [
                  Expanded(child: _metric('Độ ẩm không khí', '${_fmt(m['humidity'])}%', Icons.air)),
                  Expanded(child: _metric('Máy bơm', '${m['pumpState'] ?? '—'}', Icons.water)),
                ]),
                if (updated > 0) Text('Cập nhật: ${DateTime.fromMillisecondsSinceEpoch(updated.toInt()).toLocal()}',
                  style: Theme.of(context).textTheme.bodySmall),
              ],
            )));
          },
        ),
        StreamBuilder<DatabaseEvent>(
          stream: _stream('control'),
          builder: (context, snap) {
            final v = snap.data?.snapshot.value;
            final m = v is Map ? v : <Object?, Object?>{};
            final threshold = (_draftThreshold ?? _num(m['threshold'], fallback: 40)).clamp(10, 90).toDouble();
            final manual = m['manualPump'] == true;
            return Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(
              crossAxisAlignment: CrossAxisAlignment.start, children: [
                const Text('Điều khiển máy bơm', style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
                const SizedBox(height: 8),
                Text(manual ? 'Lệnh thủ công: BẬT' : 'Lệnh thủ công: TẮT'),
                const SizedBox(height: 8),
                Row(children: [
                  Expanded(child: FilledButton.icon(onPressed: _busy ? null : () => _setPump(true),
                    icon: const Icon(Icons.play_arrow), label: const Text('TƯỚI NGAY'))),
                  const SizedBox(width: 10),
                  Expanded(child: OutlinedButton.icon(onPressed: _busy ? null : () => _setPump(false),
                    icon: const Icon(Icons.stop), label: const Text('DỪNG TƯỚI'))),
                ]),
                const SizedBox(height: 20),
                Text('Ngưỡng tự động: ${threshold.round()}%'),
                Slider(min: 10, max: 90, divisions: 80, value: threshold,
                  label: '${threshold.round()}%',
                  onChanged: (v) => setState(() => _draftThreshold = v),
                  onChangeEnd: _setThreshold),
                const Text('Bơm tự động khi độ ẩm trung bình thấp hơn ngưỡng này.',
                  style: TextStyle(color: Colors.black54)),
              ],
            )));
          },
        ),
        StreamBuilder<DatabaseEvent>(
          stream: _stream('alerts/latest'),
          builder: (context, snap) {
            final v = snap.data?.snapshot.value;
            final m = v is Map ? v : <Object?, Object?>{};
            if (m.isEmpty) return const SizedBox.shrink();
            return Card(child: ListTile(leading: const Icon(Icons.notifications_active),
              title: Text('${m['type'] ?? 'Thông báo'}'),
              subtitle: Text('${m['message'] ?? ''}')));
          },
        ),
        const Padding(padding: EdgeInsets.all(8), child: Text(
          'Nếu màn hình báo chưa có dữ liệu: kiểm tra Wi-Fi ESP32, Firebase Authentication (Anonymous), '
          'Realtime Database URL và Rules.', style: TextStyle(fontSize: 12))),
      ]),
    );
  }

  Widget _errorCard(String text) => Card(child: Padding(padding: const EdgeInsets.all(12), child: Text(text)));
  Widget _metric(String label, String value, IconData icon) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 4),
    child: Row(children: [Icon(icon, size: 24), const SizedBox(width: 8),
      Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(label, style: const TextStyle(fontSize: 12, color: Colors.black54)),
        Text(value, style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w600)),
      ]))]),
  );
  double _num(Object? v, {double fallback = 0}) =>
      v is num ? v.toDouble() : double.tryParse('$v') ?? fallback;
  String _fmt(Object? v) => v is num ? v.toStringAsFixed(1) : '—';
}
