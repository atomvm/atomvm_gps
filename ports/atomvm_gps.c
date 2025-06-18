//
// Copyright (c) 2021 dushin.net
// All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <atom.h>
#include <bif.h>
#include <context.h>
#include <debug.h>
#include <esp32_sys.h>
#include <defaultatoms.h>
#include <globalcontext.h>
#include <interop.h>
#include <mailbox.h>
#include <module.h>
#include <port.h>
#include <scheduler.h>
#include <term.h>
#include <utils.h>

#include <esp_log.h>

// #define ENABLE_TRACE
#include <trace.h>

#include "atomvm_gps.h"
#include "nmea_parser.h"


#define TAG "atomvm_gps"

#define YEAR_BASE (2000) //date in GPS starts from 2000
#define NUM_ENTRIES 10

static const char *const stop_atom =                    ATOM_STR("\x4", "stop");
static const char *const receiver_atom =                ATOM_STR("\x8", "receiver");
static const char *const config_atom =                  ATOM_STR("\x6", "config");
static const char *const uart_port_atom =               ATOM_STR("\x9", "uart_port");
static const char *const uart_0_atom =                  ATOM_STR("\x6", "uart_0");
static const char *const uart_1_atom =                  ATOM_STR("\x6", "uart_1");
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)
  static const char *const uart_2_atom = ATOM_STR("\x6", "uart_2");
#endif
static const char *const rx_pin_atom =                  ATOM_STR("\x6", "rx_pin");
static const char *const baud_rate_atom =               ATOM_STR("\x9", "baud_rate");
static const char *const data_bits_atom =               ATOM_STR("\x9", "data_bits");
static const char *const data_bits_5_atom =             ATOM_STR("\xB", "data_bits_5");
static const char *const data_bits_6_atom =             ATOM_STR("\xB", "data_bits_6");
static const char *const data_bits_7_atom =             ATOM_STR("\xB", "data_bits_7");
static const char *const data_bits_8_atom =             ATOM_STR("\xB", "data_bits_8");
static const char *const stop_bits_atom =               ATOM_STR("\x9", "stop_bits");
static const char *const stop_bits_1_atom =             ATOM_STR("\xB", "stop_bits_1");
static const char *const stop_bits_1_5_atom =           ATOM_STR("\xD", "stop_bits_1_5");
static const char *const stop_bits_2_atom =             ATOM_STR("\xB", "stop_bits_2");
static const char *const parity_atom =                  ATOM_STR("\x6", "parity");
static const char *const disable_atom =                 ATOM_STR("\x7", "disable");
static const char *const even_atom =                    ATOM_STR("\x4", "even");
static const char *const odd_atom =                     ATOM_STR("\x3", "odd");
static const char *const event_queue_size_atom =        ATOM_STR("\x10", "event_queue_size");


static const char *const datetime_atom =                ATOM_STR("\x8", "datetime");
static const char *const gps_reading_atom =             ATOM_STR("\xB", "gps_reading");
static const char *const latitude_atom =                ATOM_STR("\x8", "latitude");
static const char *const longitude_atom =               ATOM_STR("\x9", "longitude");
static const char *const altitude_atom =                ATOM_STR("\x8", "altitude");
static const char *const speed_atom =                   ATOM_STR("\x5", "speed");
static const char *const sats_in_use_atom =             ATOM_STR("\xB", "sats_in_use");
static const char *const fix_atom =                     ATOM_STR("\x3", "fix");
static const char *const invalid_atom =                 ATOM_STR("\x7", "invalid");
static const char *const gps_atom =                     ATOM_STR("\x3", "gps");
static const char *const dgps_atom =                    ATOM_STR("\x4", "dgps");
static const char *const fix_mode_atom =                ATOM_STR("\x8", "fix_mode");
static const char *const mode_2d_atom =                 ATOM_STR("\x7", "mode_2d");
static const char *const mode_3d_atom =                 ATOM_STR("\x7", "mode_3d");
static const char *const valid_atom =                   ATOM_STR("\x5", "valid");
static const char *const sats_in_view_atom =            ATOM_STR("\xC", "sats_in_view");
static const char *const num_atom =                     ATOM_STR("\x3", "num");
static const char *const elevation_atom =               ATOM_STR("\x9", "elevation");
static const char *const azimuth_atom =                 ATOM_STR("\x7", "azimuth");
static const char *const snr_atom =                     ATOM_STR("\x3", "snr");
//                                                                       0123456789ABCDEF0123456789ABCDEF
//                                                                       0               1


struct platform_data {
    nmea_parser_handle_t parser;
    term receiver;
};

static term fix_to_term(GlobalContext *global, gps_fix_t fix)
{
    switch(fix) {
        case GPS_FIX_INVALID:
            return globalcontext_make_atom(global, invalid_atom);
        case GPS_FIX_GPS:
            return globalcontext_make_atom(global, gps_atom);
        case GPS_FIX_DGPS:
            return globalcontext_make_atom(global, dgps_atom);
    }
    // should never happen
    return term_invalid_term();
}

static term fix_mode_to_term(GlobalContext *global, gps_fix_mode_t fix_mode)
{
    switch(fix_mode) {
        case GPS_MODE_INVALID:
            return globalcontext_make_atom(global, invalid_atom);
        case GPS_MODE_2D:
            return globalcontext_make_atom(global, mode_2d_atom);
        case GPS_MODE_3D:
            return globalcontext_make_atom(global, mode_3d_atom);
    }
    // should never happen
    return term_invalid_term();
}

static term satellite_to_term(GlobalContext *global, Heap *heap, gps_satellite_t *sat)
{
    term map = term_alloc_map(4, heap);

    term_set_map_assoc(map, 0, globalcontext_make_atom(global, num_atom), term_from_int(sat->num));
    term_set_map_assoc(map, 1, globalcontext_make_atom(global, elevation_atom), term_from_int(sat->elevation));
    term_set_map_assoc(map, 2, globalcontext_make_atom(global, azimuth_atom), term_from_int(sat->azimuth));
    term_set_map_assoc(map, 3, globalcontext_make_atom(global, snr_atom), term_from_int(sat->snr));

    return map;
}

static term sats_in_use_to_term(Heap *heap, uint8_t sats_in_use, uint8_t *sat_ids)
{
    term ret = term_nil();

    for (int i = sats_in_use - 1;  i >= 0;  --i) {
        ret = term_list_prepend(term_from_int(sat_ids[i]), ret, heap);
    }
    return ret;
}

static term sats_in_view_to_term(GlobalContext *global, Heap *heap, uint8_t sats_in_view, gps_satellite_t *sats)
{
    term ret = term_nil();

    for (int i = sats_in_view - 1;  i >= 0;  --i) {
        ret = term_list_prepend(satellite_to_term(global, heap, sats + i), ret, heap);
    }
    return ret;
}

static term bool_to_term(bool b)
{
    return b ? TRUE_ATOM : FALSE_ATOM;
}

static term gps_to_term(GlobalContext *global, Heap *heap, gps_t *gps)
{
    term date = port_heap_create_tuple3(
        heap,
        term_from_int(gps->date.year + YEAR_BASE),
        term_from_int(gps->date.month),
        term_from_int(gps->date.day)
    );
    term time = port_heap_create_tuple3(
        heap,
        term_from_int(gps->tim.hour),
        term_from_int(gps->tim.minute),
        term_from_int(gps->tim.second)
    );
    term datetime = port_heap_create_tuple2(heap, date, time);

    term map = term_alloc_map(NUM_ENTRIES, heap);
    term_set_map_assoc(map, 0, globalcontext_make_atom(global, datetime_atom), datetime);
    term_set_map_assoc(map, 1, globalcontext_make_atom(global, latitude_atom), term_from_float(gps->latitude, heap));
    term_set_map_assoc(map, 2, globalcontext_make_atom(global, longitude_atom), term_from_float(gps->longitude, heap));
    term_set_map_assoc(map, 3, globalcontext_make_atom(global, altitude_atom), term_from_float(gps->altitude, heap));
    term_set_map_assoc(map, 4, globalcontext_make_atom(global, speed_atom), term_from_float(gps->speed, heap));
    term_set_map_assoc(map, 5, globalcontext_make_atom(global, sats_in_use_atom), sats_in_use_to_term(heap, gps->sats_in_use, gps->sats_id_in_use));
    term_set_map_assoc(map, 6, globalcontext_make_atom(global, fix_atom), fix_to_term(global, gps->fix));
    term_set_map_assoc(map, 7, globalcontext_make_atom(global, fix_mode_atom), fix_mode_to_term(global, gps->fix_mode));
    term_set_map_assoc(map, 8, globalcontext_make_atom(global, valid_atom), bool_to_term(gps->valid));
    term_set_map_assoc(map, 9, globalcontext_make_atom(global, sats_in_view_atom), sats_in_view_to_term(global, heap, gps->sats_in_view, gps->sats_desc_in_view));

    return map;
}

static void gps_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // TODO ctx cannot be passed in
    Context *ctx = (Context *) event_handler_arg;
    struct platform_data *plfdat = (struct platform_data *) ctx->platform_data;
    GlobalContext *global = ctx->global;

    int receiver = plfdat->receiver;

    Context *target = globalcontext_get_process_lock(global, term_to_local_process_id(receiver));

    gps_t *gps = NULL;
    switch (event_id) {

        case GPS_UPDATE:
            gps = (gps_t *) event_data;

            // date_time: pair of 3-ary tuples = 4*2 + 3 = 11
            // latitude: pair = 3
            // longitude: pair = 3
            // altitude: pair = 3
            // speed: pair = 3
            // sats_in_use: list[int (max=16)] = 12*2 = 24
            // sats_in_view: list[4-tuple of int (max=16)] = 16*10 = 160
            // fix: atom = 0
            // fix_mode: atom = 0
            // valid: atom = 0
            // {gps_reading, GPSReading} = 3
            // total: 210
            // TODO: make a more accurate calculation of size based on actual sats_in_view

            size_t requested_size = term_map_size_in_terms(NUM_ENTRIES) +
                TUPLE_SIZE(2) + 2 * TUPLE_SIZE(3) + // date_time
                FLOAT_SIZE + // latitude
                FLOAT_SIZE + // longitude
                FLOAT_SIZE + // altitude
                FLOAT_SIZE + // speed
                24 + // sats_in_use
                160 + // sats_in_view
                TUPLE_SIZE(2); // gps reading

            Heap heap;
            if (UNLIKELY(memory_init_heap(&heap, requested_size) != MEMORY_GC_OK)) {
                ESP_LOGW(TAG, "Failed to allocate memory: %s:%i.\n", __FILE__, __LINE__);

                // TODO what do do here?

            } else {

                term gps_reading = gps_to_term(global, &heap, gps);
                term msg = port_heap_create_tuple2(
                    &heap,
                    globalcontext_make_atom(global, gps_reading_atom),
                    gps_reading
                );

                port_send_message_nolock(global, receiver, msg);
                memory_destroy_heap(&heap, global);
            }

            globalcontext_get_process_unlock(global, target);

            break;

        case GPS_UNKNOWN:
            ESP_LOGW(TAG, "Unknown statement:%s", (char *)event_data);

            globalcontext_get_process_unlock(global, target);

            break;

        default:
            break;
    }
}


static void do_stop(Context *ctx)
{
    struct platform_data *plfdat = (struct platform_data *) ctx->platform_data;
    nmea_parser_handle_t parser = plfdat->parser;

    TRACE(TAG ": do_stop\n");
    nmea_parser_deinit(parser);
}


static NativeHandlerResult consume_mailbox(Context *ctx)
{
    Message *message = mailbox_first(&ctx->mailbox);
    term msg = message->message;

    GenMessage gen_message;
    if (UNLIKELY(port_parse_gen_message(msg, &gen_message) != GenCallMessage)) {
        ESP_LOGW(TAG, "Received invalid message.");
        mailbox_remove_message(&ctx->mailbox, &ctx->heap);
        return NativeContinue;
    }
    term pid = gen_message.pid;
    term ref = gen_message.ref;
    term req = gen_message.req;
    uint64_t ref_ticks = term_to_ref_ticks(ref);
    int process_id = term_to_local_process_id(pid);

    if (term_is_atom(req)) {
        if (globalcontext_is_term_equal_to_atom_string(ctx->global, req, stop_atom)) {
            do_stop(ctx);
            return NativeTerminate;
        }
    }

    // {Ref :: reference(), ok}
    term ret_msg = term_invalid_term();
    if (UNLIKELY(memory_ensure_free(ctx, TUPLE_SIZE(2) + REF_SIZE) != MEMORY_GC_OK)) {
       ret_msg = OUT_OF_MEMORY_ATOM;
    } else {
        ret_msg = port_create_tuple2(ctx, term_from_ref_ticks(ref_ticks, &ctx->heap), OK_ATOM);
    }

    globalcontext_send_message(ctx->global, process_id, ret_msg);
    mailbox_remove_message(&ctx->mailbox, &ctx->heap);

    return NativeContinue;
}


static uart_port_t get_uart_port(Context *ctx, term config)
{
    term uart_port = interop_kv_get_value(
        config, uart_port_atom, ctx->global
    );
    if (term_is_invalid_term(uart_port)) {
        return UART_NUM_1;
    }
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, uart_port, uart_0_atom)) {
        return UART_NUM_0;
    }
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, uart_port, uart_1_atom)) {
        return UART_NUM_1;
    }
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3)
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, uart_port, uart_2_atom)) {
        return UART_NUM_2;
    }
#endif
    ESP_LOGE(TAG, "Invalid uart_port.");
    return UART_NUM_MAX;
}


static int get_integer_value(Context *ctx, term config, AtomString key, int default_value)
{
    term value = interop_kv_get_value(config, key, ctx->global);
    if (term_is_invalid_term(value)) {
        return default_value;
    } else if (term_is_integer(value)) {
        return term_to_int(value);
    }
    ESP_LOGE(TAG, "Invalid integer value.");
    return -1;
}


static int get_rx_pin(Context *ctx, term config)
{
    term rx_pin = interop_kv_get_value(config, rx_pin_atom, ctx->global);
    if (term_is_invalid_term(rx_pin)) {
        return UART_PIN_NO_CHANGE;
    } else if (term_is_integer(rx_pin)) {
        return term_to_int(rx_pin);
    }
    ESP_LOGE(TAG, "Invalid rx_pin.");
    return UART_PIN_NO_CHANGE;
}


static int get_baud_rate(Context *ctx, term config)
{
    return get_integer_value(ctx, config, baud_rate_atom, 9600);
}


static uart_word_length_t get_data_bits(Context *ctx, term config)
{
    term data_bits = interop_kv_get_value_default(
        config, data_bits_atom, globalcontext_make_atom(ctx->global, data_bits_8_atom), ctx->global
    );
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, data_bits, data_bits_5_atom)) {
        return UART_DATA_5_BITS;
    }
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, data_bits, data_bits_6_atom)) {
        return UART_DATA_6_BITS;
    }
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, data_bits, data_bits_7_atom)) {
        return UART_DATA_7_BITS;
    }
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, data_bits, data_bits_8_atom)) {
        return UART_DATA_8_BITS;
    }
    ESP_LOGE(TAG, "Invalid data_bits.");
    return UART_DATA_BITS_MAX;
}


static uart_stop_bits_t get_stop_bits(Context *ctx, term config)
{
    term stop_bits = interop_kv_get_value_default(
        config, stop_bits_atom, globalcontext_make_atom(ctx->global, stop_bits_1_atom), ctx->global
    );
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, stop_bits, stop_bits_1_atom)) {
        return UART_STOP_BITS_1;
    }
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, stop_bits, stop_bits_1_5_atom)) {
        return UART_STOP_BITS_1_5;
    }
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, stop_bits, stop_bits_2_atom)) {
        return UART_STOP_BITS_2;
    }
    ESP_LOGE(TAG, "Invalid stop_bits.");
    return UART_STOP_BITS_MAX;
}


static uart_parity_t get_parity(Context *ctx, term config)
{
    term parity = interop_kv_get_value_default(
        config, parity_atom, globalcontext_make_atom(ctx->global, disable_atom), ctx->global
    );
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, parity, disable_atom)) {
        return UART_PARITY_DISABLE;
    }
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, parity, even_atom)) {
        return UART_PARITY_EVEN;
    }
    if (globalcontext_is_term_equal_to_atom_string(ctx->global, parity, odd_atom)) {
        return UART_PARITY_ODD;
    }
    ESP_LOGE(TAG, "Invalid parity.");
    return UART_PARITY_DISABLE;
}


static int get_event_queue_size(Context *ctx, term config)
{
    return get_integer_value(ctx, config, event_queue_size_atom, 16);
}


//
// Entrypoints
//

void atomvm_gps_init(GlobalContext *global)
{
    esp_log_level_set(TAG, ESP_LOG_VERBOSE);
    ESP_LOGI(TAG, "AtomVM GPS driver initialized.");
}

Context *atomvm_gps_create_port(GlobalContext *global, term opts)
{
    term receiver = interop_kv_get_value(opts, receiver_atom, global);
    term config = interop_kv_get_value(opts, config_atom, global);

    Context *ctx = context_new(global);
    ctx->native_handler = consume_mailbox;

    nmea_parser_config_t parser_config = // NMEA_PARSER_CONFIG_DEFAULT();
    {
        .uart = {
            .uart_port = get_uart_port(ctx, config),
            .rx_pin = get_rx_pin(ctx, config),
            .baud_rate = get_baud_rate(ctx, config),
            .data_bits = get_data_bits(ctx, config),
            .parity = get_parity(ctx, config),
            .stop_bits = get_stop_bits(ctx, config),
            .event_queue_size = get_event_queue_size(ctx, config)
        }
    };

    nmea_parser_handle_t parser = nmea_parser_init(&parser_config);
    if (UNLIKELY(IS_NULL_PTR(parser))) {
        context_destroy(ctx);
        ESP_LOGE(TAG, "Error: Unable to initialize nmea parser.\n");
        return NULL;
    }

    esp_err_t err = nmea_parser_add_handler(parser, gps_event_handler, ctx);
    if (err != ESP_OK) {
        context_destroy(ctx);
        nmea_parser_deinit(parser);
        ESP_LOGE(TAG, "Error: Unable to add nmea handler.  Error: %i.\n", err);
        return NULL;
    }

    struct platform_data *plfdat = malloc(sizeof(struct platform_data));
    plfdat->parser = parser;
    plfdat->receiver = receiver;
    ctx->platform_data = plfdat;

    ESP_LOGI(TAG, "atomvm_gps started.");
    return ctx;
}

#include <sdkconfig.h>
#ifdef CONFIG_AVM_GPS_ENABLE
REGISTER_PORT_DRIVER(atomvm_gps, atomvm_gps_init, NULL, atomvm_gps_create_port)
#endif
