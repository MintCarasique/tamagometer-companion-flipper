/**
 * Tamagometer Enhanced companion for Flipper Zero.
 *
 * Connection IR support is derived from Zach Resmer's MIT-licensed
 * tamagometer-companion-flipper. Friends LF RFID timings and packets are
 * derived from Natalie Silvanovich's published Proxmark implementation.
 */

#include <furi.h>
#include <api_lock.h>
#include <cli/cli.h>
#include <furi_hal_rfid.h>
#include <gui/gui.h>
#include <gui/modules/text_box.h>
#include <gui/view_holder.h>
#include <infrared.h>
#include <infrared_transmit.h>
#include <infrared_worker.h>

#define MATCH_TIMING(x, v, delta) (((x) < ((v) + (delta))) && ((x) > ((v) - (delta))))
#define COMPANION_VERSION "1.2.0"
#define FRIENDS_REPEAT_COUNT 10

typedef struct {
    bool command_decoded;
    bool timed_out;
    FuriApiLock cli_lock;
} AppState;

static AppState app_state;

typedef struct {
    uint32_t header_mark;
    uint32_t header_mark_tolerance;
    uint32_t header_space;
    uint32_t header_space_tolerance;
    uint32_t data_mark;
    uint32_t data_mark_tolerance;
    uint32_t data_0_space;
    uint32_t data_0_space_tolerance;
    uint32_t data_1_space;
    uint32_t data_1_space_tolerance;
    uint32_t ending_mark;
} DecoderTimings;

static const DecoderTimings ir = {
    .header_mark = 9600,
    .header_mark_tolerance = 2000,
    .header_space = 5000,
    .header_space_tolerance = 1500,
    .data_mark = 550,
    .data_mark_tolerance = 300,
    .data_0_space = 600,
    .data_0_space_tolerance = 400,
    .data_1_space = 1500,
    .data_1_space_tolerance = 500,
    .ending_mark = 1100,
};

static void back_callback(void* context) {
    api_lock_unlock((FuriApiLock)context);
}
static bool decode_ir(InfraredWorkerSignal* signal, unsigned char* bits) {
    const uint32_t* timings;
    size_t count;
    infrared_worker_get_raw_signal(signal, &timings, &count);
    if(count < 323) return false;
    if(!MATCH_TIMING(timings[0], ir.header_mark, ir.header_mark_tolerance) ||
       !MATCH_TIMING(timings[1], ir.header_space, ir.header_space_tolerance)) {
        return false;
    }

    size_t timing = 2;
    for(size_t bit = 0; bit < 160; bit++, timing += 2) {
        if(!MATCH_TIMING(timings[timing], ir.data_mark, ir.data_mark_tolerance)) return false;
        if(MATCH_TIMING(timings[timing + 1], ir.data_0_space, ir.data_0_space_tolerance)) {
            bits[bit] = '0';
        } else if(MATCH_TIMING(
                      timings[timing + 1], ir.data_1_space, ir.data_1_space_tolerance)) {
            bits[bit] = '1';
        } else {
            return false;
        }
    }
    return true;
}

static void signal_received(void* pipe, InfraredWorkerSignal* signal) {
    unsigned char bits[160];
    if(decode_ir(signal, bits)) {
        pipe_send(pipe, (unsigned char*)"[PICO]", 6);
        pipe_send(pipe, bits, 160);
        pipe_send(pipe, (unsigned char*)"[END]", 5);
        app_state.command_decoded = true;
    }
}

static void listen_timeout(void* context) {
    UNUSED(context);
    app_state.timed_out = true;
}

static void listen_ir(PipeSide* pipe) {
    FuriTimer* timer = furi_timer_alloc(listen_timeout, FuriTimerTypeOnce, NULL);
    furi_timer_start(timer, furi_ms_to_ticks(1000));
    InfraredWorker* worker = infrared_worker_alloc();
    infrared_worker_rx_set_received_signal_callback(worker, signal_received, pipe);
    infrared_worker_rx_start(worker);
    furi_hal_infrared_async_rx_set_timeout(ir.header_space + ir.header_space_tolerance);

    while(!app_state.command_decoded && !app_state.timed_out &&
          !cli_is_pipe_broken_or_is_etx_next_char(pipe)) {
        furi_delay_ms(1);
    }
    if(app_state.timed_out) {
        static const unsigned char timeout_message[] = "[PICO]timed out[END]";
        pipe_send(pipe, timeout_message, sizeof(timeout_message) - 1);
    }
    infrared_worker_rx_stop(worker);
    infrared_worker_free(worker);
    furi_timer_stop(timer);
    furi_timer_free(timer);
}

static bool ir_bits_to_timings(const char* bitstring, uint32_t* timings) {
    if(strlen(bitstring) != 160) return false;
    timings[0] = ir.header_mark;
    timings[1] = ir.header_space;
    size_t output = 2;
    for(size_t bit = 0; bit < 160; bit++) {
        timings[output++] = ir.data_mark;
        if(bitstring[bit] == '0') {
            timings[output++] = ir.data_0_space;
        } else if(bitstring[bit] == '1') {
            timings[output++] = ir.data_1_space;
        } else {
            return false;
        }
    }
    timings[output] = ir.ending_mark;
    return true;
}

static void send_ir(const char* bitstring) {
    uint32_t timings[323];
    if(ir_bits_to_timings(bitstring, timings)) infrared_send_raw(timings, 323, true);
}

static void friends_send_byte(uint8_t value) {
    /* Field-on/off timings from the published working Proxmark transmitter. */
    furi_hal_rfid_tim_read_continue();
    furi_delay_us(540);
    furi_hal_rfid_tim_read_pause();
    for(int8_t bit = 7; bit >= 0; bit--) {
        furi_hal_rfid_tim_read_pause();
        furi_delay_us((value & (1U << bit)) ? 650 : 270);
        furi_hal_rfid_tim_read_continue();
        furi_delay_us(150);
    }
    furi_hal_rfid_tim_read_pause();
    furi_delay_us(210);
}

static void friends_send_packet(const uint8_t* packet, size_t length) {
    for(size_t i = 0; i < length; i++) friends_send_byte(packet[i]);
}

static bool friends_broadcast(PipeSide* pipe, uint8_t outcome) {
    static const uint8_t connect_ack[] = {
        0xF0, 0x01, 0x0F, 0x01, 0x01, 0x0F, 0x0B, 0x00, 0x06, 0x80,
        0x02, 0x08, 0x01, 0x08, 0x1A, 0x1A, 0x1A, 0x1A, 0x2D,
    };
    uint8_t reward[] = {0xF0, 0x07, 0x05, 0x01, 0x07, 0x0F, 0x0B, outcome, 0x00};
    reward[8] = (uint8_t)(0x2E + outcome);

    furi_hal_rfid_tim_read_start(134800.0f, 0.5f);
    furi_hal_rfid_pin_pull_release();
    bool completed = true;
    for(uint8_t repeat = 0; repeat < FRIENDS_REPEAT_COUNT; repeat++) {
        if(cli_is_pipe_broken_or_is_etx_next_char(pipe)) {
            completed = false;
            break;
        }
        friends_send_packet(connect_ack, sizeof(connect_ack));
        if(cli_is_pipe_broken_or_is_etx_next_char(pipe)) {
            completed = false;
            break;
        }
        furi_delay_ms(100);
        friends_send_packet(reward, sizeof(reward));
        char progress[48];
        snprintf(
            progress,
            sizeof(progress),
            "[TAMAFRIENDS]progress=%u/%u[END]",
            (unsigned int)(repeat + 1),
            (unsigned int)FRIENDS_REPEAT_COUNT);
        pipe_send(pipe, (unsigned char*)progress, strlen(progress));
        if(repeat + 1 != FRIENDS_REPEAT_COUNT) furi_delay_ms(1000);
    }
    furi_hal_rfid_tim_read_stop();
    furi_hal_rfid_pins_reset();
    return completed;
}

static void cli_command(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(context);
    api_lock_relock(app_state.cli_lock);
    app_state.command_decoded = false;
    app_state.timed_out = false;

    const char* value = furi_string_get_cstr(args);
    char bitstring[161];
    unsigned long outcome;
    if(strcmp(value, "info") == 0) {
        static const unsigned char info_message[] =
            "[TAMAGOMETER]version=" COMPANION_VERSION
            ";protocol=1;capabilities=connection_ir,friends_lf,friends_progress[END]";
        pipe_send(pipe, info_message, sizeof(info_message) - 1);
    } else if(sscanf(value, "send%160s", bitstring) == 1) {
        send_ir(bitstring);
    } else if(strcmp(value, "listen") == 0) {
        listen_ir(pipe);
    } else if(sscanf(value, "friends%lu", &outcome) == 1 && outcome <= 255) {
        if(friends_broadcast(pipe, (uint8_t)outcome)) {
            static const unsigned char ok_message[] = "[TAMAFRIENDS]ok[END]";
            pipe_send(pipe, ok_message, sizeof(ok_message) - 1);
        } else {
            static const unsigned char cancelled_message[] = "[TAMAFRIENDS]cancelled[END]";
            pipe_send(pipe, cancelled_message, sizeof(cancelled_message) - 1);
        }
    } else {
        printf("Invalid argument(s). Use info, listen, send<bits>, or friends<0-255>.\r\n");
    }
    api_lock_unlock(app_state.cli_lock);
}

int32_t tamagometer_companion(void* arg) {
    UNUSED(arg);
    app_state.cli_lock = api_lock_alloc_locked();
    api_lock_unlock(app_state.cli_lock);

    CliRegistry* cli = furi_record_open(RECORD_CLI);
    cli_registry_add_command(cli, "tamagometer", CliCommandFlagParallelSafe, cli_command, NULL);
    furi_record_close(RECORD_CLI);

    Gui* gui = furi_record_open(RECORD_GUI);
    TextBox* text_box = text_box_alloc();
    text_box_set_text(
        text_box,
        "Tamagometer Enhanced\n\n"
        "Companion " COMPANION_VERSION "\n"
        "Connection: IR\n"
        "Friends: LF RFID\n\n"
        "Connect USB and open\n"
        "Tamagometer Desktop.\n\n"
        "Press Back to exit.");
    ViewHolder* holder = view_holder_alloc();
    view_holder_attach_to_gui(holder, gui);
    view_holder_set_view(holder, text_box_get_view(text_box));
    FuriApiLock exit_lock = api_lock_alloc_locked();
    view_holder_set_back_callback(holder, back_callback, exit_lock);
    api_lock_wait_unlock_and_free(exit_lock);

    cli = furi_record_open(RECORD_CLI);
    cli_registry_delete_command(cli, "tamagometer");
    furi_record_close(RECORD_CLI);
    api_lock_wait_unlock_and_free(app_state.cli_lock);
    view_holder_set_view(holder, NULL);
    view_holder_free(holder);
    text_box_free(text_box);
    furi_record_close(RECORD_GUI);
    return 0;
}
