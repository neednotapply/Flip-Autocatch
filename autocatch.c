/**
 * Flip Autocatch: emulate a Pokemon GO Plus and press its button automatically
 * when the phone shows a Pokemon or Pokestop.
 */
#include "pgp_handshake.h"
#include "pgp_led.h"
#include "pgp_profile.h"
#include "pgp_secrets.h"

#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_random.h>
#include <bt/bt_service/bt.h>
#include <gui/gui.h>
#include <gui/elements.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification_messages.h>

#include "autocatch_icons.h"

#define TAG "Autocatch"

#define AUTOCATCH_KEYS_PATH APP_DATA_PATH(".pgp.keys")

// pgpemu presses 1.0-2.5 s after the LED pattern starts
#define AUTOCATCH_PRESS_DELAY_MIN_MS    1000
#define AUTOCATCH_PRESS_DELAY_RANGE_MS  1500
#define AUTOCATCH_EVENT_QUEUE_SIZE      16

// Keypad lock as in Sub-GHz Read: Back three times within a second unlocks
#define AUTOCATCH_UNLOCK_PRESSES  3
#define AUTOCATCH_LOCK_MESSAGE_MS 1000
#define AUTOCATCH_UNLOCKED_MS     650

typedef enum {
    AutocatchEventTypeBtStatus,
    AutocatchEventTypeGatt,
} AutocatchEventType;

typedef struct {
    AutocatchEventType type;
    union {
        BtStatus bt_status;
        PgpGattEvent gatt;
    };
} AutocatchEvent;

typedef enum {
    AutocatchViewMain,
    AutocatchViewConfig,
} AutocatchView;

typedef enum {
    AutocatchConfigCatch,
    AutocatchConfigSpin,
    AutocatchConfigLock,
} AutocatchConfigItem;

typedef enum {
    AutocatchCustomEventLock,
} AutocatchCustomEvent;

static const char* const autocatch_switch_text[] = {"OFF", "ON"};

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    View* main_view;
    VariableItemList* config;
    NotificationApp* notifications;
    FuriMessageQueue* event_queue;
    FuriEventLoopTimer* press_timer;
    FuriEventLoopTimer* lock_timer;
    // Everything runs on the view dispatcher's thread except drawing, so the
    // mutex only keeps the draw callback from seeing half updated state
    FuriMutex* mutex;

    Bt* bt;
    FuriHalBleProfileBase* profile;
    BleProfilePgpParams profile_params;

    PgpSecrets secrets;
    PgpSecretsStatus secrets_status;
    PgpHandshake handshake;

    BtStatus bt_status;
    bool handshake_started;
    bool autocatch;
    bool autospin;

    bool locked;
    bool unlock_hint;
    uint8_t unlock_presses;

    PgpLedEvent last_event;
    uint32_t seen;
    uint32_t caught;
    uint32_t fled;
    uint32_t spins;
    uint32_t presses;
} AutocatchApp;

static void autocatch_random_fill(uint8_t* buffer, size_t len) {
    furi_hal_random_fill_buf(buffer, len);
}

static const char* autocatch_status_text(AutocatchApp* app) {
    if(!app->profile) {
        return "Bluetooth start failed";
    }
    if(pgp_handshake_is_done(&app->handshake)) {
        return "Connected";
    }
    // Pairing (and so BtStatusConnected) may finish after the handshake started
    if(app->handshake_started || app->bt_status == BtStatusConnected) {
        switch(app->handshake.state) {
        case PgpHandshakeStateChallenge:
            return "Handshake 1/3";
        case PgpHandshakeStateNextChallenge:
            return "Handshake 2/3";
        default:
            return "Handshake 3/3";
        }
    }
    if(app->bt_status == BtStatusAdvertising) {
        return "Waiting for phone...";
    }
    return "Bluetooth off";
}

static const char* autocatch_mode_text(const AutocatchApp* app) {
    if(app->autocatch && app->autospin) {
        return "Catch+Spin";
    }
    if(app->autocatch) {
        return "Catch only";
    }
    return app->autospin ? "Spin only" : "Auto off";
}

static void autocatch_draw_bottom_bar(Canvas* canvas, const AutocatchApp* app) {
    // OK presses the button by hand, so it gets no label
    if(!app->locked) {
        elements_button_left(canvas, "Config");
    } else if(app->unlock_presses >= AUTOCATCH_UNLOCK_PRESSES) {
        canvas_draw_icon(canvas, 0, 54, &I_unlock_7x8);
        canvas_draw_str(canvas, 10, 61, "Unlocked");
    } else {
        canvas_draw_icon(canvas, 0, 54, &I_lock_7x8);
        canvas_draw_str(canvas, 10, 61, "Locked");
    }
    canvas_draw_str_aligned(canvas, 128, 61, AlignRight, AlignBottom, autocatch_mode_text(app));
}

static void autocatch_draw_unlock_hint(Canvas* canvas) {
    // Leaves the bottom bar visible
    elements_bold_rounded_frame(canvas, 14, 5, 99, 45);
    canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignBottom, "To unlock press:");
    canvas_draw_icon(canvas, 44, 31, &I_back_10x8);
    canvas_draw_icon(canvas, 59, 31, &I_back_10x8);
    canvas_draw_icon(canvas, 74, 31, &I_back_10x8);
}

/* The model only holds the app pointer */
static void autocatch_draw_callback(Canvas* canvas, void* model) {
    AutocatchApp* app = *(AutocatchApp**)model;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(app->secrets_status != PgpSecretsStatusOk) {
        canvas_draw_str(canvas, 0, 10, "PoGo Autocatch");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(
            canvas,
            0,
            22,
            app->secrets_status == PgpSecretsStatusMissing ? "No PGP secrets found." :
                                                             "PGP secrets file invalid.");
        canvas_draw_str(canvas, 0, 34, "Copy your device dump to");
        canvas_draw_str(canvas, 0, 44, PGP_SECRETS_DISPLAY_PATH);
        canvas_draw_str(canvas, 0, 56, "See README for the format.");
    } else {
        FuriString* line = furi_string_alloc();

        canvas_draw_str(canvas, 0, 10, app->secrets.name);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 0, 20, autocatch_status_text(app));
        canvas_draw_str(canvas, 0, 30, pgp_led_event_name(app->last_event));
        furi_string_printf(
            line, "Seen %lu  Caught %lu  Fled %lu", app->seen, app->caught, app->fled);
        canvas_draw_str(canvas, 0, 40, furi_string_get_cstr(line));
        furi_string_printf(line, "Spins %lu  Presses %lu", app->spins, app->presses);
        canvas_draw_str(canvas, 0, 49, furi_string_get_cstr(line));
        furi_string_free(line);

        autocatch_draw_bottom_bar(canvas, app);
        if(app->unlock_hint) {
            autocatch_draw_unlock_hint(canvas);
        }
    }

    furi_mutex_release(app->mutex);
}

static void autocatch_redraw(AutocatchApp* app) {
    // The model never changes; committing it just redraws the view
    view_get_model(app->main_view);
    view_commit_model(app->main_view, true);
}

static void autocatch_bt_status_callback(BtStatus status, void* context) {
    AutocatchApp* app = context;
    AutocatchEvent event = {.type = AutocatchEventTypeBtStatus, .bt_status = status};
    furi_message_queue_put(app->event_queue, &event, FuriWaitForever);
}

/* Runs on the BLE event thread: just hand the event over */
static void autocatch_gatt_callback(const PgpGattEvent* gatt_event, void* context) {
    AutocatchApp* app = context;
    AutocatchEvent event = {.type = AutocatchEventTypeGatt, .gatt = *gatt_event};
    if(furi_message_queue_put(app->event_queue, &event, 0) != FuriStatusOk) {
        FURI_LOG_W(TAG, "Event queue full, dropped GATT event %d", gatt_event->type);
    }
}

static void autocatch_press_button(AutocatchApp* app) {
    uint8_t pattern[2];
    pgp_led_button_pattern(furi_hal_random_get(), pattern);
    ble_profile_pgp_send_button(app->profile, pattern);
    app->presses++;
    notification_message(app->notifications, &sequence_blink_blue_10);
}

static void autocatch_send_handshake(AutocatchApp* app, const PgpHandshakeOutput* output) {
    if(output->cert_len &&
       !ble_profile_pgp_set_cert(app->profile, app->handshake.cert_buffer, output->cert_len)) {
        FURI_LOG_E(TAG, "Failed to publish %u byte certificate", output->cert_len);
    }
    if(output->notify) {
        ble_profile_pgp_send_command(app->profile, output->command);
    }
    if(pgp_handshake_is_done(&app->handshake)) {
        FURI_LOG_I(TAG, "Handshake complete");
        notification_message(app->notifications, &sequence_blink_green_100);
    }
}

static void autocatch_handle_led(AutocatchApp* app, const uint8_t* data, size_t len) {
    PgpLedPattern pattern;
    if(!pgp_led_parse(data, len, &pattern)) {
        FURI_LOG_W(TAG, "Bad LED pattern, %u bytes", len);
        return;
    }
    FURI_LOG_I(
        TAG, "LED: %s, %lu ms", pgp_led_event_name(pattern.event), pattern.duration_ms);
    if(pattern.event == PgpLedEventNone) {
        return;
    }
    app->last_event = pattern.event;

    bool press = false;
    switch(pattern.event) {
    case PgpLedEventPokemon:
    case PgpLedEventNewPokemon:
        app->seen++;
        press = app->autocatch;
        break;
    case PgpLedEventPokestop:
        press = app->autospin;
        break;
    case PgpLedEventCaught:
        app->caught++;
        break;
    case PgpLedEventFled:
        app->fled++;
        break;
    case PgpLedEventItems:
        app->spins++;
        break;
    case PgpLedEventUnknown:
        press = app->autocatch || app->autospin;
        break;
    default:
        break;
    }

    if(press) {
        // The press only counts while the pattern is still showing
        uint32_t delay_ms = AUTOCATCH_PRESS_DELAY_MIN_MS +
                            furi_hal_random_get() % (AUTOCATCH_PRESS_DELAY_RANGE_MS + 1);
        if(delay_ms < pattern.duration_ms) {
            furi_event_loop_timer_start(app->press_timer, furi_ms_to_ticks(delay_ms));
        }
    }
}

static void autocatch_handle_gatt(AutocatchApp* app, const PgpGattEvent* event) {
    PgpHandshakeOutput output;
    switch(event->type) {
    case PgpGattEventCertNotify:
        app->handshake_started = event->data[0];
        if(pgp_handshake_on_notify(&app->handshake, event->data[0], &output)) {
            autocatch_send_handshake(app, &output);
        }
        break;
    case PgpGattEventCertWrite:
        if(pgp_handshake_on_write(&app->handshake, event->data, event->len, &output)) {
            autocatch_send_handshake(app, &output);
        } else {
            FURI_LOG_W(
                TAG,
                "Unexpected %u byte write in handshake state %d",
                event->len,
                app->handshake.state);
        }
        break;
    case PgpGattEventLedWrite:
        autocatch_handle_led(app, event->data, event->len);
        break;
    }
}

static void autocatch_event_queue_callback(FuriEventLoopObject* object, void* context) {
    AutocatchApp* app = context;
    AutocatchEvent event;
    furi_check(furi_message_queue_get(object, &event, 0) == FuriStatusOk);

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    switch(event.type) {
    case AutocatchEventTypeBtStatus:
        app->bt_status = event.bt_status;
        if(event.bt_status != BtStatusConnected) {
            pgp_handshake_reset(&app->handshake);
            app->handshake_started = false;
            furi_event_loop_timer_stop(app->press_timer);
        }
        break;
    case AutocatchEventTypeGatt:
        autocatch_handle_gatt(app, &event.gatt);
        break;
    }
    furi_mutex_release(app->mutex);
    autocatch_redraw(app);
}

static void autocatch_press_timer_callback(void* context) {
    AutocatchApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    autocatch_press_button(app);
    furi_mutex_release(app->mutex);
    autocatch_redraw(app);
}

/* Ends the lock message or unlock hint, or finishes unlocking */
static void autocatch_lock_timer_callback(void* context) {
    AutocatchApp* app = context;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->unlock_hint = false;
    if(app->unlock_presses >= AUTOCATCH_UNLOCK_PRESSES) {
        app->locked = false;
    } else {
        // As in Sub-GHz the screen goes dark; any key lights it up again
        notification_message(app->notifications, &sequence_display_backlight_off);
    }
    app->unlock_presses = 0;
    furi_mutex_release(app->mutex);
    autocatch_redraw(app);
}

/* Like Sub-GHz: any key shows how to unlock, Back three times unlocks */
static void autocatch_handle_locked_input(AutocatchApp* app, const InputEvent* input) {
    if(app->unlock_presses == 0) {
        furi_event_loop_timer_start(app->lock_timer, furi_ms_to_ticks(AUTOCATCH_LOCK_MESSAGE_MS));
    }
    if(input->key == InputKeyBack && input->type == InputTypeShort &&
       app->unlock_presses < AUTOCATCH_UNLOCK_PRESSES) {
        app->unlock_presses++;
    }
    app->unlock_hint = app->unlock_presses < AUTOCATCH_UNLOCK_PRESSES;
    if(!app->unlock_hint) {
        // Stay locked while "Unlocked" shows, so more Back presses don't exit
        furi_event_loop_timer_start(app->lock_timer, furi_ms_to_ticks(AUTOCATCH_UNLOCKED_MS));
    }
}

static bool autocatch_input_callback(InputEvent* input, void* context) {
    AutocatchApp* app = context;

    if(app->locked) {
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        autocatch_handle_locked_input(app, input);
        furi_mutex_release(app->mutex);
    } else if(input->type == InputTypeShort) {
        switch(input->key) {
        case InputKeyBack:
            view_dispatcher_stop(app->view_dispatcher);
            break;
        case InputKeyLeft:
            if(app->secrets_status == PgpSecretsStatusOk) {
                view_dispatcher_switch_to_view(app->view_dispatcher, AutocatchViewConfig);
            }
            break;
        case InputKeyOk:
            furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
            if(app->profile && pgp_handshake_is_done(&app->handshake)) {
                autocatch_press_button(app);
            }
            furi_mutex_release(app->mutex);
            break;
        default:
            break;
        }
    }
    autocatch_redraw(app);
    return true;
}

static void autocatch_config_set_switch(VariableItem* item, bool* value) {
    AutocatchApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, autocatch_switch_text[index]);
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    *value = index;
    furi_mutex_release(app->mutex);
}

static void autocatch_config_catch_callback(VariableItem* item) {
    AutocatchApp* app = variable_item_get_context(item);
    autocatch_config_set_switch(item, &app->autocatch);
}

static void autocatch_config_spin_callback(VariableItem* item) {
    AutocatchApp* app = variable_item_get_context(item);
    autocatch_config_set_switch(item, &app->autospin);
}

/* Runs with the list's model locked, so leave the view switch for later */
static void autocatch_config_enter_callback(void* context, uint32_t index) {
    AutocatchApp* app = context;
    if(index == AutocatchConfigLock) {
        view_dispatcher_send_custom_event(app->view_dispatcher, AutocatchCustomEventLock);
    }
}

static uint32_t autocatch_config_previous_callback(void* context) {
    UNUSED(context);
    return AutocatchViewMain;
}

static bool autocatch_custom_event_callback(void* context, uint32_t event) {
    AutocatchApp* app = context;
    if(event != AutocatchCustomEventLock) {
        return false;
    }
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    app->locked = true;
    app->unlock_hint = false;
    app->unlock_presses = 0;
    furi_mutex_release(app->mutex);
    furi_event_loop_timer_start(app->lock_timer, furi_ms_to_ticks(AUTOCATCH_LOCK_MESSAGE_MS));
    view_dispatcher_switch_to_view(app->view_dispatcher, AutocatchViewMain);
    return true;
}

static void autocatch_config_add_switch(
    AutocatchApp* app,
    const char* label,
    bool value,
    VariableItemChangeCallback callback) {
    VariableItem* item = variable_item_list_add(app->config, label, 2, callback, app);
    variable_item_set_current_value_index(item, value);
    variable_item_set_current_value_text(item, autocatch_switch_text[value]);
}

static void autocatch_bt_start(AutocatchApp* app) {
    app->bt = furi_record_open(RECORD_BT);
    bt_disconnect(app->bt);
    // Wait for the 2nd core to update its NVM storage
    furi_delay_ms(200);
    bt_keys_storage_set_storage_path(app->bt, AUTOCATCH_KEYS_PATH);

    app->profile_params.mac = app->secrets.mac;
    app->profile_params.callback = autocatch_gatt_callback;
    app->profile_params.context = app;
    app->profile = bt_profile_start(app->bt, ble_profile_pgp, &app->profile_params);
    if(!app->profile) {
        FURI_LOG_E(TAG, "Failed to start PGP profile");
        return;
    }

    bt_set_status_changed_callback(app->bt, autocatch_bt_status_callback, app);
    furi_hal_bt_start_advertising();
}

static void autocatch_bt_stop(AutocatchApp* app) {
    bt_set_status_changed_callback(app->bt, NULL, NULL);
    bt_disconnect(app->bt);
    furi_delay_ms(200);
    bt_keys_storage_set_default_path(app->bt);
    furi_check(bt_profile_restore_default(app->bt));
    furi_record_close(RECORD_BT);
}

static AutocatchApp* autocatch_alloc(void) {
    AutocatchApp* app = malloc(sizeof(AutocatchApp));
    memset(app, 0, sizeof(AutocatchApp));

    app->autocatch = true;
    app->autospin = true;
    app->event_queue = furi_message_queue_alloc(AUTOCATCH_EVENT_QUEUE_SIZE, sizeof(AutocatchEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, autocatch_custom_event_callback);

    // Bluetooth events and timers run on the view dispatcher's thread too
    FuriEventLoop* event_loop = view_dispatcher_get_event_loop(app->view_dispatcher);
    furi_event_loop_subscribe_message_queue(
        event_loop, app->event_queue, FuriEventLoopEventIn, autocatch_event_queue_callback, app);
    app->press_timer = furi_event_loop_timer_alloc(
        event_loop, autocatch_press_timer_callback, FuriEventLoopTimerTypeOnce, app);
    app->lock_timer = furi_event_loop_timer_alloc(
        event_loop, autocatch_lock_timer_callback, FuriEventLoopTimerTypeOnce, app);

    app->main_view = view_alloc();
    view_allocate_model(app->main_view, ViewModelTypeLockFree, sizeof(AutocatchApp*));
    AutocatchApp** model = view_get_model(app->main_view);
    *model = app;
    view_commit_model(app->main_view, false);
    view_set_context(app->main_view, app);
    view_set_draw_callback(app->main_view, autocatch_draw_callback);
    view_set_input_callback(app->main_view, autocatch_input_callback);
    view_dispatcher_add_view(app->view_dispatcher, AutocatchViewMain, app->main_view);

    app->config = variable_item_list_alloc();
    autocatch_config_add_switch(
        app, "Auto catch", app->autocatch, autocatch_config_catch_callback);
    autocatch_config_add_switch(app, "Auto spin", app->autospin, autocatch_config_spin_callback);
    variable_item_list_add(app->config, "Lock Keyboard", 1, NULL, NULL);
    variable_item_list_set_enter_callback(app->config, autocatch_config_enter_callback, app);
    View* config_view = variable_item_list_get_view(app->config);
    view_set_previous_callback(config_view, autocatch_config_previous_callback);
    view_dispatcher_add_view(app->view_dispatcher, AutocatchViewConfig, config_view);

    app->gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void autocatch_free(AutocatchApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, AutocatchViewMain);
    view_dispatcher_remove_view(app->view_dispatcher, AutocatchViewConfig);
    view_free(app->main_view);
    variable_item_list_free(app->config);

    FuriEventLoop* event_loop = view_dispatcher_get_event_loop(app->view_dispatcher);
    furi_event_loop_unsubscribe(event_loop, app->event_queue);
    furi_event_loop_timer_free(app->press_timer);
    furi_event_loop_timer_free(app->lock_timer);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_mutex_free(app->mutex);
    furi_message_queue_free(app->event_queue);
    free(app);
}

int32_t autocatch_app(void* p) {
    UNUSED(p);
    AutocatchApp* app = autocatch_alloc();

    app->secrets_status = pgp_secrets_load(&app->secrets);
    if(app->secrets_status == PgpSecretsStatusOk) {
        pgp_handshake_init(
            &app->handshake,
            app->secrets.mac,
            app->secrets.device_key,
            app->secrets.blob,
            autocatch_random_fill);
        autocatch_bt_start(app);
    }

    // Only show the screen once the secrets and Bluetooth are set up
    view_dispatcher_switch_to_view(app->view_dispatcher, AutocatchViewMain);
    view_dispatcher_run(app->view_dispatcher);

    if(app->bt) {
        autocatch_bt_stop(app);
    }
    autocatch_free(app);
    return 0;
}
