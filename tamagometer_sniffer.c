#include "tamagometer_sniffer.h"

#include <furi.h>
#include <furi_hal_infrared.h>
#include <infrared_worker.h>
#include <stdarg.h>
#include <stdio.h>
#include <storage/storage.h>
#include <string.h>

#define SNIFFER_QUEUE_DEPTH   8U
#define SNIFFER_MAX_TIMINGS   1024U
#define SNIFFER_RX_TIMEOUT_US 7500U

typedef struct {
    uint32_t timestamp_ms;
    size_t timing_count;
    uint32_t timings[];
} TamaSnifferFrame;

typedef struct {
    FuriMessageQueue* queue;
    uint32_t started_at;
    volatile uint32_t dropped;
} TamaSnifferReceiver;

typedef struct {
    bool recognized;
    bool checksum_valid;
    uint8_t byte_count;
    uint8_t bytes[TAMA_SNIFFER_MAX_BYTES];
} TamaDecodedLegacyFrame;

static TamaDecodedLegacyFrame decode_legacy_frame(const uint32_t* timings, size_t count) {
    TamaDecodedLegacyFrame decoded = {0};
    decoded.recognized = tama_legacy_decode(
        timings, count, decoded.bytes, &decoded.byte_count, &decoded.checksum_valid);
    return decoded;
}

static bool write_text(File* file, const char* text) {
    size_t length = strlen(text);
    return storage_file_write(file, text, length) == length;
}

static bool write_format(File* file, const char* format, ...) {
    char buffer[192];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if(length < 0 || (size_t)length >= sizeof(buffer)) return false;
    return storage_file_write(file, buffer, (size_t)length) == (size_t)length;
}

static bool write_raw_timings(File* file, const TamaSnifferFrame* frame) {
    const size_t capacity = frame->timing_count * 12U + 7U;
    char* line = malloc(capacity);
    if(!line) return false;
    size_t used = strlcpy(line, "raw:", capacity);
    bool valid = used < capacity;
    for(size_t index = 0; valid && index < frame->timing_count; index++) {
        int length =
            snprintf(line + used, capacity - used, " %lu", (unsigned long)frame->timings[index]);
        if(length < 0 || (size_t)length >= capacity - used) {
            valid = false;
        } else {
            used += (size_t)length;
        }
    }
    if(valid && used + 1U < capacity) {
        line[used++] = '\n';
        line[used] = '\0';
    } else {
        valid = false;
    }
    bool written = valid && storage_file_write(file, line, used) == used;
    free(line);
    return written;
}

static bool write_frame(
    File* file,
    uint32_t frame_number,
    const TamaSnifferFrame* frame,
    const TamaDecodedLegacyFrame* decoded) {
    bool written = write_format(
        file,
        "\n[frame %lu]\ntime_ms: %lu\ntimings: %u\nformat: %s\n",
        (unsigned long)frame_number,
        (unsigned long)frame->timestamp_ms,
        (unsigned int)frame->timing_count,
        decoded->recognized ? (decoded->byte_count == 9U  ? "legacy-9" :
                               decoded->byte_count == 18U ? "legacy-18" :
                               decoded->byte_count == 20U ? "legacy-20" :
                                                            "legacy-24") :
                              "unknown");
    if(decoded->recognized) {
        written = written && write_format(
                                 file,
                                 "bytes: %u\ntype: 0x%02X\nchecksum: %s\ndata:",
                                 decoded->byte_count,
                                 decoded->bytes[1],
                                 decoded->checksum_valid ? "ok" : "invalid");
        for(uint8_t index = 0; written && index < decoded->byte_count; index++)
            written = write_format(file, " %02X", decoded->bytes[index]);
        written = written && write_text(file, "\n");
    }

    return written && write_raw_timings(file, frame);
}

static void received_signal_callback(void* context, InfraredWorkerSignal* signal) {
    TamaSnifferReceiver* receiver = context;
    if(infrared_worker_signal_is_decoded(signal)) return;

    const uint32_t* timings;
    size_t count;
    infrared_worker_get_raw_signal(signal, &timings, &count);
    if(count == 0U || count > SNIFFER_MAX_TIMINGS) {
        receiver->dropped++;
        return;
    }

    TamaSnifferFrame* frame = malloc(sizeof(TamaSnifferFrame) + count * sizeof(uint32_t));
    if(!frame) {
        receiver->dropped++;
        return;
    }
    frame->timestamp_ms = furi_get_tick() - receiver->started_at;
    frame->timing_count = count;
    memcpy(frame->timings, timings, count * sizeof(uint32_t));
    if(furi_message_queue_put(receiver->queue, &frame, 0) != FuriStatusOk) {
        receiver->dropped++;
        free(frame);
    }
}

static bool open_capture_file(Storage* storage, File* file, TamaSnifferSummary* summary) {
    for(uint16_t index = 0; index < 1000U; index++) {
        snprintf(
            summary->file_name, sizeof(summary->file_name), "connection_capture_%03u.txt", index);
        FuriString* path = furi_string_alloc_printf(APP_DATA_PATH("%s"), summary->file_name);
        storage_common_resolve_path_and_ensure_app_directory(storage, path);
        bool opened =
            storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW);
        furi_string_free(path);
        if(opened) return true;
        storage_file_close(file);
    }
    summary->file_name[0] = '\0';
    return false;
}

static void report_summary(
    TamaSnifferProgressCallback progress,
    const TamaSnifferSummary* summary,
    void* context) {
    if(progress) progress(summary, context);
}

bool tama_sniffer_capture(
    TamaCancelCallback cancelled,
    TamaSnifferProgressCallback progress,
    void* context,
    TamaSnifferSummary* summary) {
    memset(summary, 0, sizeof(*summary));
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(!open_capture_file(storage, file, summary)) {
        summary->write_failed = true;
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        report_summary(progress, summary, context);
        return false;
    }

    bool written = write_text(
        file,
        "Tamagometer Enhanced Connection capture\n"
        "Format: 1\n"
        "Receiver: Flipper Zero infrared\n"
        "Direction: unknown\n"
        "Timing unit: microseconds\n"
        "Timestamp: signal end, milliseconds since capture start\n"
        "Carrier: receiver-demodulated, nominal 38000 Hz\n"
        "Legacy byte order: decoded LSB-first\n");

    FuriMessageQueue* queue =
        furi_message_queue_alloc(SNIFFER_QUEUE_DEPTH, sizeof(TamaSnifferFrame*));
    TamaSnifferReceiver receiver = {
        .queue = queue,
        .started_at = furi_get_tick(),
        .dropped = 0,
    };
    InfraredWorker* worker = infrared_worker_alloc();
    infrared_worker_rx_enable_signal_decoding(worker, false);
    infrared_worker_rx_enable_blink_on_receiving(worker, true);
    infrared_worker_rx_set_received_signal_callback(worker, received_signal_callback, &receiver);
    infrared_worker_rx_start(worker);
    furi_hal_infrared_async_rx_set_timeout(SNIFFER_RX_TIMEOUT_US);
    report_summary(progress, summary, context);

    while(written && !(cancelled && cancelled(context))) {
        TamaSnifferFrame* frame = NULL;
        if(furi_message_queue_get(queue, &frame, furi_ms_to_ticks(100U)) != FuriStatusOk) {
            continue;
        }
        TamaDecodedLegacyFrame decoded = decode_legacy_frame(frame->timings, frame->timing_count);
        summary->captured_frames++;
        if(decoded.recognized) {
            summary->legacy_frames++;
            if(decoded.checksum_valid) summary->valid_checksums++;
            summary->last_byte_count = decoded.byte_count;
            summary->last_checksum_valid = decoded.checksum_valid;
            memcpy(summary->last_bytes, decoded.bytes, sizeof(decoded.bytes));
        }
        summary->dropped_frames = receiver.dropped;
        written = write_frame(file, summary->captured_frames, frame, &decoded);
        free(frame);
        if(written) storage_file_sync(file);
        report_summary(progress, summary, context);
    }

    infrared_worker_rx_stop(worker);
    infrared_worker_free(worker);
    TamaSnifferFrame* remaining = NULL;
    while(furi_message_queue_get(queue, &remaining, 0) == FuriStatusOk)
        free(remaining);
    furi_message_queue_free(queue);

    summary->dropped_frames = receiver.dropped;
    summary->write_failed = !written;
    if(written) {
        written = write_format(
            file,
            "\n[summary]\nframes: %lu\nlegacy_frames: %lu\n"
            "valid_checksums: %lu\ndropped_frames: %lu\n",
            (unsigned long)summary->captured_frames,
            (unsigned long)summary->legacy_frames,
            (unsigned long)summary->valid_checksums,
            (unsigned long)summary->dropped_frames);
    }
    summary->write_failed = !written;
    storage_file_sync(file);
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    report_summary(progress, summary, context);
    return written;
}
