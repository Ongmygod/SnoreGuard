import 'dart:typed_data';
import '../../models/snore_event.dart';
import '../../utils/timestamp_utils.dart';
import '../../utils/session_date_utils.dart';

class BlePacketParser {
  /// Parse a 7-byte BLE notification into a [SnoreEvent].
  /// Returns null if the packet is malformed (wrong length or invalid fields).
  ///
  /// Packet layout (little-endian):
  ///   [0..3] uint32  event_timestamp (Unix epoch seconds)
  ///   [4]    uint8   duration_s (0–255)
  ///   [5]    uint8   haptic_success (0 or 1)
  ///   [6]    uint8   haptic_flag (0 or 1)
  static SnoreEvent? parseEventPacket(List<int> bytes) {
    if (bytes.length != 7) return null;

    final bd = ByteData.sublistView(Uint8List.fromList(bytes));
    final timestamp = bd.getUint32(0, Endian.little);
    final durationS = bytes[4];
    final hapticSuccess = bytes[5];
    final hapticFlag = bytes[6];

    // Validate binary flag fields
    if (hapticSuccess > 1 || hapticFlag > 1) return null;
    // Reject zero-duration events as likely garbage
    if (durationS == 0 && hapticFlag == 0) return null;

    final isFallback = TimestampUtils.isFallbackTimestamp(timestamp);

    // If fallback timestamp, use today's date to group events meaningfully
    final sessionDate = isFallback
        ? SessionDateUtils.today()
        : SessionDateUtils.computeSessionDateFromEpoch(timestamp);

    return SnoreEvent(
      sessionDate: sessionDate,
      eventTimestamp: timestamp,
      durationS: durationS,
      hapticSuccess: hapticSuccess,
      hapticFlag: hapticFlag,
      isFallbackTimestamp: isFallback,
    );
  }

  /// Encode the current Unix epoch as a 4-byte little-endian payload
  /// for the Time Sync characteristic.
  static Uint8List encodeTimeSyncPayload() {
    final epoch = TimestampUtils.nowSeconds();
    final bd = ByteData(4);
    bd.setUint32(0, epoch, Endian.little);
    return bd.buffer.asUint8List();
  }

  /// Encode a haptic level (0–4) as a single byte.
  static Uint8List encodeHapticLevel(int level) {
    assert(level >= 0 && level <= 4, 'Haptic level must be 0–4');
    return Uint8List.fromList([level]);
  }

  /// Encode a sync ack byte: 0x01 = success (clear log), 0x00 = failure (keep log).
  static Uint8List encodeSyncAck({required bool success}) {
    return Uint8List.fromList([success ? 0x01 : 0x00]);
  }
}
