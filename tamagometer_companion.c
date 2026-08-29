/** Tamagometer Enhanced standalone and desktop-compatible application. */

#include "tamagometer_catalog.h"
#include "tamagometer_cli.h"
#include "tamagometer_item_icons.h"
#include "tamagometer_protocol.h"
#include "tamagometer_settings.h"
#include "tamagometer_sniffer.h"
#include "tamagometer_transfer_view.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/popup.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <notification/notification_messages.h>
#include <stdio.h>
#include <string.h>

#define APP_VERSION "2.0.0-dev"
#define MENU_LABEL_LIMIT 65U
#define MENU_LABEL_LENGTH 40U

typedef enum {
  TamaViewMenu,
  TamaViewWidget,
  TamaViewTransfer,
  TamaViewPopup,
} TamaView;

typedef enum {
  TamaSceneWelcome,
  TamaSceneMain,
  TamaSceneSniffer,
  TamaSceneCategories,
  TamaSceneItems,
  TamaSceneItemDetail,
  TamaSceneTransfer,
  TamaSceneResult,
  TamaSceneSettings,
  TamaSceneAbout,
  TamaScenePopup,
  TamaSceneCount,
} TamaScene;

typedef enum {
  TamaEventWelcomeDone = 1,
  TamaEventMainConnection = 10,
  TamaEventMainFriends,
  TamaEventMainLegacy,
  TamaEventMainSniffer,
  TamaEventMainRepeat,
  TamaEventMainSettings,
  TamaEventCategoryBase = 100,
  TamaEventItemBase = 200,
  TamaEventDetailFavorite = 500,
  TamaEventDetailSend,
  TamaEventTransferProgress,
  TamaEventTransferFinished,
  TamaEventSnifferUpdated,
  TamaEventSnifferFinished,
  TamaEventSnifferStop,
  TamaEventSnifferMenu,
  TamaEventResultMenu,
  TamaEventResultRepeat,
  TamaEventSettingsVibration,
  TamaEventSettingsDiagnostics,
  TamaEventSettingsGuide,
  TamaEventSettingsAbout,
  TamaEventPopupDone,
} TamaEvent;

typedef struct {
  Gui *gui;
  ViewDispatcher *view_dispatcher;
  SceneManager *scene_manager;
  Submenu *submenu;
  Widget *widget;
  Popup *popup;
  TamagometerTransferView *transfer_view;
  NotificationApp *notifications;
  TamagometerCli *cli;
  FuriMutex *radio_mutex;
  FuriMutex *state_mutex;
  FuriThread *transfer_thread;
  FuriThread *sniffer_thread;
  volatile bool cancel_requested;
  bool sniffer_active;
  bool sniffer_succeeded;
  TamaSnifferSummary sniffer_summary;
  TamaSettings settings;
  TamaMode selected_mode;
  bool legacy_fallback;
  TamaLegacySummary legacy_summary;
  TamaCategory selected_category;
  uint8_t selected_item;
  TamaTransferStage transfer_stage;
  TamaTransferResult transfer_result;
  uint8_t transfer_current;
  uint8_t transfer_total;
  uint8_t animation_frame;
  char selected_name[MENU_LABEL_LENGTH];
  char menu_labels[MENU_LABEL_LIMIT][MENU_LABEL_LENGTH];
  uint8_t menu_label_count;
  char popup_header[24];
  char popup_text[72];
  char last_status[72];
} TamagometerApp;

static void send_event(TamagometerApp *app, uint32_t event) {
  view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

static void submenu_callback(void *context, uint32_t index) {
  send_event(context, index);
}

static void widget_callback(GuiButtonType button, InputType type,
                            void *context) {
  if (type != InputTypeShort)
    return;
  TamagometerApp *app = context;
  uint32_t scene = scene_manager_get_current_scene(app->scene_manager);
  if (scene == TamaSceneWelcome && button == GuiButtonTypeCenter) {
    send_event(app, TamaEventWelcomeDone);
  } else if (scene == TamaSceneItemDetail) {
    if (button == GuiButtonTypeLeft)
      send_event(app, TamaEventDetailFavorite);
    if (button == GuiButtonTypeCenter)
      send_event(app, TamaEventDetailSend);
  } else if (scene == TamaSceneResult) {
    if (button == GuiButtonTypeLeft)
      send_event(app, TamaEventResultMenu);
    if (button == GuiButtonTypeCenter)
      send_event(app, TamaEventResultRepeat);
  } else if (scene == TamaSceneSniffer) {
    if (button == GuiButtonTypeCenter && app->sniffer_active)
      send_event(app, TamaEventSnifferStop);
    if (button == GuiButtonTypeLeft && !app->sniffer_active)
      send_event(app, TamaEventSnifferMenu);
  }
}

static void popup_callback(void *context) {
  send_event(context, TamaEventPopupDone);
}

static bool custom_event_callback(void *context, uint32_t event) {
  TamagometerApp *app = context;
  return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool navigation_event_callback(void *context) {
  return scene_manager_handle_back_event(
      ((TamagometerApp *)context)->scene_manager);
}

static void tick_event_callback(void *context) {
  scene_manager_handle_tick_event(((TamagometerApp *)context)->scene_manager);
}

static void transfer_cancel_callback(void *context) {
  ((TamagometerApp *)context)->cancel_requested = true;
}

static bool transfer_is_cancelled(void *context) {
  return ((TamagometerApp *)context)->cancel_requested;
}

static void transfer_progress_callback(TamaTransferStage stage, uint8_t current,
                                       uint8_t total, void *context) {
  TamagometerApp *app = context;
  furi_mutex_acquire(app->state_mutex, FuriWaitForever);
  app->transfer_stage = stage;
  app->transfer_current = current;
  app->transfer_total = total;
  furi_mutex_release(app->state_mutex);
  send_event(app, TamaEventTransferProgress);
}

static int32_t transfer_worker(void *context) {
  TamagometerApp *app = context;
  furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
  TamaTransferResult result;
  if (app->cancel_requested) {
    result = TamaTransferResultCancelled;
  } else if (app->legacy_fallback) {
    result = tama_protocol_legacy_fallback(
        transfer_is_cancelled, transfer_progress_callback, app,
        &app->legacy_summary);
  } else if (app->selected_mode == TamaModeFriends) {
    result = tama_protocol_friends_transfer(
        app->selected_item, transfer_is_cancelled, transfer_progress_callback,
        app);
  } else {
    result = tama_protocol_connection_transfer(
        app->selected_item, transfer_is_cancelled, transfer_progress_callback,
        app);
  }
  furi_mutex_release(app->radio_mutex);
  furi_mutex_acquire(app->state_mutex, FuriWaitForever);
  app->transfer_result = result;
  furi_mutex_release(app->state_mutex);
  send_event(app, TamaEventTransferFinished);
  return 0;
}

static void sniffer_progress_callback(const TamaSnifferSummary *summary,
                                      void *context) {
  TamagometerApp *app = context;
  furi_mutex_acquire(app->state_mutex, FuriWaitForever);
  app->sniffer_summary = *summary;
  furi_mutex_release(app->state_mutex);
  send_event(app, TamaEventSnifferUpdated);
}

static int32_t sniffer_worker(void *context) {
  TamagometerApp *app = context;
  TamaSnifferSummary summary;
  furi_mutex_acquire(app->radio_mutex, FuriWaitForever);
  bool succeeded = tama_sniffer_capture(transfer_is_cancelled,
                                        sniffer_progress_callback, app,
                                        &summary);
  furi_mutex_release(app->radio_mutex);
  furi_mutex_acquire(app->state_mutex, FuriWaitForever);
  app->sniffer_summary = summary;
  app->sniffer_succeeded = succeeded;
  app->sniffer_active = false;
  furi_mutex_release(app->state_mutex);
  send_event(app, TamaEventSnifferFinished);
  return 0;
}

static void set_selected_item(TamagometerApp *app, TamaMode mode,
                              uint8_t item_id) {
  app->legacy_fallback = false;
  app->selected_mode = mode;
  app->selected_item = item_id;
  char generated[MENU_LABEL_LENGTH];
  const char *name =
      tama_catalog_item_name(mode, item_id, generated, sizeof(generated));
  strlcpy(app->selected_name, name, sizeof(app->selected_name));
}

static void show_popup(TamagometerApp *app, const char *header,
                       const char *text) {
  strlcpy(app->popup_header, header, sizeof(app->popup_header));
  strlcpy(app->popup_text, text, sizeof(app->popup_text));
  scene_manager_next_scene(app->scene_manager, TamaScenePopup);
}

static void fill_main_menu(TamagometerApp *app) {
  submenu_reset(app->submenu);
  submenu_set_header(app->submenu, "Tamagometer Enhanced");
  submenu_add_item(app->submenu, "Connection - IR", TamaEventMainConnection,
                   submenu_callback, app);
  submenu_add_item(app->submenu, "Friends - LF RFID", TamaEventMainFriends,
                   submenu_callback, app);
  submenu_add_item(app->submenu, "Original fallback", TamaEventMainLegacy,
                   submenu_callback, app);
  submenu_add_item(app->submenu, "Connection sniffer", TamaEventMainSniffer,
                   submenu_callback, app);
  if (app->settings.last_valid) {
    submenu_add_item(app->submenu, "Repeat last transfer", TamaEventMainRepeat,
                     submenu_callback, app);
  }
  submenu_add_item(app->submenu, "Settings & diagnostics",
                   TamaEventMainSettings, submenu_callback, app);
}

static void fill_settings_menu(TamagometerApp *app) {
  submenu_reset(app->submenu);
  submenu_set_header(app->submenu, "Settings");
  snprintf(app->menu_labels[0], MENU_LABEL_LENGTH, "Vibration: %s",
           app->settings.vibration ? "On" : "Off");
  submenu_add_item(app->submenu, app->menu_labels[0],
                   TamaEventSettingsVibration, submenu_callback, app);
  submenu_add_item(app->submenu, "Export diagnostic report",
                   TamaEventSettingsDiagnostics, submenu_callback, app);
  submenu_add_item(app->submenu, "Placement guide", TamaEventSettingsGuide,
                   submenu_callback, app);
  submenu_add_item(app->submenu, "About", TamaEventSettingsAbout,
                   submenu_callback, app);
}

static bool item_matches_selection(TamagometerApp *app, TamaMode mode,
                                   uint8_t item_id) {
  if (app->selected_category == TamaCategoryFavorites) {
    return tama_settings_is_favorite(&app->settings, mode, item_id);
  }
  if (app->selected_category == TamaCategoryRecent) {
    return tama_settings_is_recent(&app->settings, mode, item_id);
  }
  return tama_catalog_category_matches(mode, app->selected_category, item_id);
}

static void add_item_menu_entry(TamagometerApp *app, uint8_t item_id) {
  if (app->menu_label_count >= MENU_LABEL_LIMIT)
    return;
  char generated[MENU_LABEL_LENGTH];
  const char *name = tama_catalog_item_name(app->selected_mode, item_id,
                                            generated, sizeof(generated));
  strlcpy(app->menu_labels[app->menu_label_count], name, MENU_LABEL_LENGTH);
  submenu_add_item(app->submenu, app->menu_labels[app->menu_label_count],
                   TamaEventItemBase + item_id, submenu_callback, app);
  app->menu_label_count++;
}

static void fill_items_menu(TamagometerApp *app) {
  submenu_reset(app->submenu);
  submenu_set_header(app->submenu,
                     tama_catalog_category_name(app->selected_category));
  app->menu_label_count = 0;
  if (app->selected_category == TamaCategoryRecent) {
    for (uint8_t i = 0; i < app->settings.recent_count; i++) {
      TamaItemRef recent = app->settings.recent[i];
      if (recent.mode == app->selected_mode)
        add_item_menu_entry(app, recent.item_id);
    }
  } else {
    size_t count = tama_catalog_item_count(app->selected_mode);
    for (size_t index = 0; index < count; index++) {
      uint8_t item_id = tama_catalog_item_id(app->selected_mode, index);
      if (item_matches_selection(app, app->selected_mode, item_id))
        add_item_menu_entry(app, item_id);
    }
  }
  if (app->menu_label_count == 0) {
    strlcpy(app->menu_labels[0], "No items yet", MENU_LABEL_LENGTH);
    submenu_add_item(app->submenu, app->menu_labels[0], UINT32_MAX,
                     submenu_callback, app);
  }
}

static void fill_item_detail(TamagometerApp *app) {
  widget_reset(app->widget);
  char details[64];
  snprintf(details, sizeof(details), "%s\nID: %s%u\n%s",
           app->selected_mode == TamaModeFriends ? "Friends - LF RFID"
                                                 : "Connection - IR",
           app->selected_mode == TamaModeFriends ? "0x" : "",
           (unsigned int)app->selected_item,
           tama_catalog_category_name(tama_catalog_item_category(
               app->selected_mode, app->selected_item)));
  if (app->selected_mode == TamaModeFriends) {
    snprintf(details, sizeof(details), "Friends - LF RFID\nID: 0x%02X\n%s",
             app->selected_item,
             tama_catalog_category_name(tama_catalog_item_category(
                 app->selected_mode, app->selected_item)));
  }
  widget_add_string_element(app->widget, 64, 3, AlignCenter, AlignTop,
                            FontPrimary, app->selected_name);
  const Icon *icon = app->selected_mode == TamaModeConnection
                         ? tamagometer_item_icon(app->selected_item)
                         : NULL;
  if (icon) {
    widget_add_icon_element(app->widget, 4, 17, icon);
    widget_add_text_box_element(app->widget, 38, 17, 86, 32, AlignCenter,
                                AlignTop, details, false);
  } else {
    widget_add_text_box_element(app->widget, 4, 17, 120, 32, AlignCenter,
                                AlignTop, details, false);
  }
  widget_add_button_element(app->widget, GuiButtonTypeLeft,
                            tama_settings_is_favorite(&app->settings,
                                                      app->selected_mode,
                                                      app->selected_item)
                                ? "Unfavorite"
                                : "Favorite",
                            widget_callback, app);
  widget_add_button_element(app->widget, GuiButtonTypeCenter, "Send",
                            widget_callback, app);
}

static void start_transfer(TamagometerApp *app) {
  if (app->transfer_thread)
    return;
  app->cancel_requested = false;
  app->transfer_stage = TamaTransferStagePreparing;
  app->transfer_current = 0;
  app->transfer_total = app->selected_mode == TamaModeFriends ? 10 : 100;
  app->animation_frame = 0;
  app->transfer_thread =
      furi_thread_alloc_ex("TamaTransfer", 4096, transfer_worker, app);
  furi_thread_start(app->transfer_thread);
}

static void finish_transfer_thread(TamagometerApp *app) {
  if (app->transfer_thread) {
    furi_thread_join(app->transfer_thread);
    furi_thread_free(app->transfer_thread);
    app->transfer_thread = NULL;
  }
}

static void start_sniffer(TamagometerApp *app) {
  if (app->sniffer_thread)
    return;
  app->cancel_requested = false;
  app->sniffer_active = true;
  app->sniffer_succeeded = false;
  memset(&app->sniffer_summary, 0, sizeof(app->sniffer_summary));
  app->sniffer_thread =
      furi_thread_alloc_ex("TamaSniffer", 4096, sniffer_worker, app);
  furi_thread_start(app->sniffer_thread);
}

static void finish_sniffer_thread(TamagometerApp *app) {
  if (app->sniffer_thread) {
    furi_thread_join(app->sniffer_thread);
    furi_thread_free(app->sniffer_thread);
    app->sniffer_thread = NULL;
  }
}

static void update_sniffer_widget(TamagometerApp *app) {
  TamaSnifferSummary summary;
  bool active;
  bool succeeded;
  furi_mutex_acquire(app->state_mutex, FuriWaitForever);
  summary = app->sniffer_summary;
  active = app->sniffer_active;
  succeeded = app->sniffer_succeeded;
  furi_mutex_release(app->state_mutex);

  widget_reset(app->widget);
  widget_add_string_element(app->widget, 64, 2, AlignCenter, AlignTop,
                            FontPrimary,
                            active ? "Connection sniffer" : "Capture stopped");
  char text[192];
  if (active) {
    char last_frame[32];
    if (summary.last_byte_count >= 2U) {
      snprintf(last_frame, sizeof(last_frame), "Last: %uB  type 0x%02X",
               summary.last_byte_count, summary.last_bytes[1]);
    } else {
      strlcpy(last_frame, "Last: waiting", sizeof(last_frame));
    }
    snprintf(text, sizeof(text),
             "Frames: %lu  Valid: %lu\n%s\n"
             "V2: Ver.1  V3: Others",
             (unsigned long)summary.captured_frames,
             (unsigned long)summary.valid_checksums, last_frame);
    widget_add_button_element(app->widget, GuiButtonTypeCenter, "Stop",
                              widget_callback, app);
  } else if (succeeded) {
    snprintf(text, sizeof(text),
             "Saved: %s\nFrames: %lu  Valid: %lu\nLast type: 0x%02X",
             summary.file_name, (unsigned long)summary.captured_frames,
             (unsigned long)summary.valid_checksums,
             summary.last_byte_count >= 2U ? summary.last_bytes[1] : 0U);
    widget_add_button_element(app->widget, GuiButtonTypeLeft, "Menu",
                              widget_callback, app);
  } else {
    snprintf(text, sizeof(text),
             "Capture could not be saved.\nCheck the SD card and try again.");
    widget_add_button_element(app->widget, GuiButtonTypeLeft, "Menu",
                              widget_callback, app);
  }
  widget_add_text_box_element(app->widget, 3, 15, 122, 32, AlignCenter,
                              AlignTop, text, false);
}

static void update_transfer_view(TamagometerApp *app) {
  furi_mutex_acquire(app->state_mutex, FuriWaitForever);
  TamaTransferStage stage = app->transfer_stage;
  uint8_t current = app->transfer_current;
  uint8_t total = app->transfer_total;
  furi_mutex_release(app->state_mutex);
  tamagometer_transfer_view_update(app->transfer_view, app->selected_mode,
                                   app->selected_name, stage, current, total,
                                   app->animation_frame, true);
}

static void on_enter_welcome(void *context) {
  TamagometerApp *app = context;
  widget_reset(app->widget);
  widget_add_string_element(app->widget, 64, 3, AlignCenter, AlignTop,
                            FontPrimary, "Tamagometer Enhanced");
  widget_add_text_box_element(app->widget, 5, 16, 118, 34, AlignCenter,
                              AlignTop,
                              "Standalone gifts\nConnection: align IR "
                              "ports\nFriends: back-to-back on LF",
                              false);
  widget_add_button_element(app->widget, GuiButtonTypeCenter, "Start",
                            widget_callback, app);
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewWidget);
}

static bool on_event_welcome(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type == SceneManagerEventTypeBack &&
      !scene_manager_has_previous_scene(app->scene_manager,
                                        TamaSceneSettings)) {
    scene_manager_stop(app->scene_manager);
    view_dispatcher_stop(app->view_dispatcher);
    return true;
  }
  if (event.type == SceneManagerEventTypeCustom &&
      event.event == TamaEventWelcomeDone) {
    app->settings.onboarding_complete = true;
    tama_settings_save(&app->settings);
    if (!scene_manager_search_and_switch_to_previous_scene(app->scene_manager,
                                                           TamaSceneSettings))
      scene_manager_next_scene(app->scene_manager, TamaSceneMain);
    return true;
  }
  return false;
}

static void on_exit_widget(void *context) {
  widget_reset(((TamagometerApp *)context)->widget);
}
static void on_exit_menu(void *context) {
  submenu_reset(((TamagometerApp *)context)->submenu);
}

static void on_enter_main(void *context) {
  TamagometerApp *app = context;
  fill_main_menu(app);
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewMenu);
}

static bool on_event_main(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type == SceneManagerEventTypeBack) {
    scene_manager_stop(app->scene_manager);
    view_dispatcher_stop(app->view_dispatcher);
    return true;
  }
  if (event.type != SceneManagerEventTypeCustom)
    return false;
  if (event.event == TamaEventMainConnection ||
      event.event == TamaEventMainFriends) {
    app->legacy_fallback = false;
    app->selected_mode = event.event == TamaEventMainFriends
                             ? TamaModeFriends
                             : TamaModeConnection;
    scene_manager_next_scene(app->scene_manager, TamaSceneCategories);
    return true;
  }
  if (event.event == TamaEventMainLegacy) {
    app->legacy_fallback = true;
    app->selected_mode = TamaModeConnection;
    memset(&app->legacy_summary, 0, sizeof(app->legacy_summary));
    strlcpy(app->selected_name, "Original V1 fallback",
            sizeof(app->selected_name));
    scene_manager_next_scene(app->scene_manager, TamaSceneTransfer);
    return true;
  }
  if (event.event == TamaEventMainSniffer) {
    scene_manager_next_scene(app->scene_manager, TamaSceneSniffer);
    return true;
  }
  if (event.event == TamaEventMainRepeat && app->settings.last_valid) {
    set_selected_item(app, (TamaMode)app->settings.last.mode,
                      app->settings.last.item_id);
    scene_manager_next_scene(app->scene_manager, TamaSceneTransfer);
    return true;
  }
  if (event.event == TamaEventMainSettings) {
    scene_manager_next_scene(app->scene_manager, TamaSceneSettings);
    return true;
  }
  return false;
}

static void on_enter_sniffer(void *context) {
  TamagometerApp *app = context;
  start_sniffer(app);
  update_sniffer_widget(app);
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewWidget);
}

static bool on_event_sniffer(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type == SceneManagerEventTypeBack) {
    if (app->sniffer_active) {
      app->cancel_requested = true;
      return true;
    }
    return false;
  }
  if (event.type != SceneManagerEventTypeCustom)
    return false;
  if (event.event == TamaEventSnifferUpdated) {
    update_sniffer_widget(app);
    return true;
  }
  if (event.event == TamaEventSnifferStop) {
    app->cancel_requested = true;
    return true;
  }
  if (event.event == TamaEventSnifferFinished) {
    finish_sniffer_thread(app);
    update_sniffer_widget(app);
    if (app->settings.vibration)
      notification_message(app->notifications, &sequence_single_vibro);
    return true;
  }
  if (event.event == TamaEventSnifferMenu) {
    scene_manager_previous_scene(app->scene_manager);
    return true;
  }
  return false;
}

static void on_enter_categories(void *context) {
  TamagometerApp *app = context;
  submenu_reset(app->submenu);
  submenu_set_header(app->submenu, tama_catalog_mode_name(app->selected_mode));
  size_t count;
  const TamaCategory *categories =
      tama_catalog_categories(app->selected_mode, &count);
  for (size_t index = 0; index < count; index++) {
    submenu_add_item(
        app->submenu, tama_catalog_category_name(categories[index]),
        TamaEventCategoryBase + categories[index], submenu_callback, app);
  }
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewMenu);
}

static bool on_event_categories(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type == SceneManagerEventTypeCustom &&
      event.event >= TamaEventCategoryBase && event.event < TamaEventItemBase) {
    app->selected_category =
        (TamaCategory)(event.event - TamaEventCategoryBase);
    scene_manager_next_scene(app->scene_manager, TamaSceneItems);
    return true;
  }
  return false;
}

static void on_enter_items(void *context) {
  TamagometerApp *app = context;
  fill_items_menu(app);
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewMenu);
}

static bool on_event_items(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type == SceneManagerEventTypeCustom &&
      event.event >= TamaEventItemBase &&
      event.event < TamaEventItemBase + 256U) {
    set_selected_item(app, app->selected_mode,
                      (uint8_t)(event.event - TamaEventItemBase));
    scene_manager_next_scene(app->scene_manager, TamaSceneItemDetail);
    return true;
  }
  return false;
}

static void on_enter_item_detail(void *context) {
  TamagometerApp *app = context;
  fill_item_detail(app);
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewWidget);
}

static bool on_event_item_detail(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type != SceneManagerEventTypeCustom)
    return false;
  if (event.event == TamaEventDetailFavorite) {
    tama_settings_toggle_favorite(&app->settings, app->selected_mode,
                                  app->selected_item);
    tama_settings_save(&app->settings);
    fill_item_detail(app);
    return true;
  }
  if (event.event == TamaEventDetailSend) {
    scene_manager_next_scene(app->scene_manager, TamaSceneTransfer);
    return true;
  }
  return false;
}

static void on_enter_transfer(void *context) {
  TamagometerApp *app = context;
  update_transfer_view(app);
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewTransfer);
  start_transfer(app);
}

static bool on_event_transfer(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type == SceneManagerEventTypeTick) {
    app->animation_frame++;
    update_transfer_view(app);
    return true;
  }
  if (event.type != SceneManagerEventTypeCustom)
    return false;
  if (event.event == TamaEventTransferProgress) {
    update_transfer_view(app);
    return true;
  }
  if (event.event == TamaEventTransferFinished) {
    finish_transfer_thread(app);
    if (app->legacy_fallback) {
      const char *peer = app->legacy_summary.peer == TamaLegacyPeerV2
                             ? "V2"
                             : app->legacy_summary.peer == TamaLegacyPeerV3
                                   ? "V3"
                                   : "unknown";
      if (app->transfer_result == TamaTransferResultSuccess) {
        snprintf(app->last_status, sizeof(app->last_status), "%s; peer: %s",
                 tama_protocol_legacy_activity_text(
                     app->legacy_summary.activity),
                 peer);
      } else {
        snprintf(app->last_status, sizeof(app->last_status),
                 "%s; peer %s; ack %u; retry %u",
                 tama_protocol_result_text(app->transfer_result), peer,
                 (unsigned int)app->legacy_summary.identity_attempts,
                 (unsigned int)app->legacy_summary.initial_retries);
      }
    } else {
      strlcpy(app->last_status, tama_protocol_result_text(app->transfer_result),
              sizeof(app->last_status));
    }
    if (app->transfer_result == TamaTransferResultSuccess) {
      if (!app->legacy_fallback) {
        tama_settings_record_transfer(&app->settings, app->selected_mode,
                                      app->selected_item);
        tama_settings_save(&app->settings);
      }
      if (app->settings.vibration)
        notification_message(app->notifications, &sequence_single_vibro);
    } else if (app->transfer_result != TamaTransferResultCancelled &&
               app->settings.vibration) {
      notification_message(app->notifications, &sequence_double_vibro);
    }
    scene_manager_next_scene(app->scene_manager, TamaSceneResult);
    return true;
  }
  return false;
}

static void on_exit_transfer(void *context) {
  TamagometerApp *app = context;
  tamagometer_transfer_view_update(
      app->transfer_view, app->selected_mode, app->selected_name,
      TamaTransferStagePreparing, 0, 100, 0, false);
}

static void on_enter_result(void *context) {
  TamagometerApp *app = context;
  widget_reset(app->widget);
  bool success = app->transfer_result == TamaTransferResultSuccess;
  widget_add_string_element(app->widget, 64, 4, AlignCenter, AlignTop,
                            FontPrimary,
                            success ? "Transfer complete" : "Transfer stopped");
  char text[128];
  snprintf(text, sizeof(text), "%s\n%s", app->selected_name, app->last_status);
  widget_add_text_box_element(app->widget, 5, 20, 118, 28, AlignCenter,
                              AlignTop, text, false);
  widget_add_button_element(app->widget, GuiButtonTypeLeft, "Menu",
                            widget_callback, app);
  widget_add_button_element(app->widget, GuiButtonTypeCenter, "Repeat",
                            widget_callback, app);
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewWidget);
}

static bool on_event_result(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type == SceneManagerEventTypeBack ||
      (event.type == SceneManagerEventTypeCustom &&
       event.event == TamaEventResultMenu)) {
    if (!scene_manager_search_and_switch_to_another_scene(app->scene_manager,
                                                          TamaSceneMain))
      scene_manager_next_scene(app->scene_manager, TamaSceneMain);
    return true;
  }
  if (event.type != SceneManagerEventTypeCustom)
    return false;
  if (event.event == TamaEventResultRepeat) {
    scene_manager_next_scene(app->scene_manager, TamaSceneTransfer);
    return true;
  }
  return false;
}

static void on_enter_settings(void *context) {
  TamagometerApp *app = context;
  fill_settings_menu(app);
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewMenu);
}

static bool on_event_settings(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type != SceneManagerEventTypeCustom)
    return false;
  if (event.event == TamaEventSettingsVibration) {
    app->settings.vibration = !app->settings.vibration;
    tama_settings_save(&app->settings);
    if (app->settings.vibration)
      notification_message(app->notifications, &sequence_single_vibro);
    fill_settings_menu(app);
    return true;
  }
  if (event.event == TamaEventSettingsDiagnostics) {
    char runtime_details[256] = "";
    if (app->legacy_summary.peer != TamaLegacyPeerUnknown) {
      snprintf(runtime_details, sizeof(runtime_details),
               "Legacy peer: %s\n"
               "Legacy initial bytes: %u\n"
               "Legacy identity attempts: %u\n"
               "Legacy initial retries: %u\n"
               "Legacy last RX: %u bytes, type 0x%02X\n",
               app->legacy_summary.peer == TamaLegacyPeerV2 ? "V2" : "V3",
               (unsigned int)app->legacy_summary.initial_bytes,
               (unsigned int)app->legacy_summary.identity_attempts,
               (unsigned int)app->legacy_summary.initial_retries,
               (unsigned int)app->legacy_summary.last_rx_bytes,
               (unsigned int)app->legacy_summary.last_rx_type);
    }
    bool saved = tama_settings_export_diagnostics(
        &app->settings, APP_VERSION, app->last_status, runtime_details);
    show_popup(app, saved ? "Report exported" : "Export failed",
               saved ? "Saved as diagnostics.txt in the app data folder"
                     : "Check that the SD card is available");
    return true;
  }
  if (event.event == TamaEventSettingsGuide) {
    scene_manager_next_scene(app->scene_manager, TamaSceneWelcome);
    return true;
  }
  if (event.event == TamaEventSettingsAbout) {
    scene_manager_next_scene(app->scene_manager, TamaSceneAbout);
    return true;
  }
  return false;
}

static void on_enter_about(void *context) {
  TamagometerApp *app = context;
  widget_reset(app->widget);
  widget_add_text_scroll_element(
      app->widget, 4, 2, 120, 60,
      "\e#Tamagometer Enhanced 2.0\nStandalone + Desktop CLI.\n\nEnhanced fork "
      "of the MIT-licensed Tamagometer project. Connection IR support derives "
      "from Zach Resmer's original companion. Original V2/V3 fallback derives "
      "from hardware captures. Friends research by Natalie Silvanovich and "
      "MrBlinky.");
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewWidget);
}

static bool on_event_none(void *context, SceneManagerEvent event) {
  UNUSED(context);
  UNUSED(event);
  return false;
}

static void on_enter_popup(void *context) {
  TamagometerApp *app = context;
  popup_reset(app->popup);
  popup_set_header(app->popup, app->popup_header, 64, 10, AlignCenter,
                   AlignTop);
  popup_set_text(app->popup, app->popup_text, 64, 30, AlignCenter, AlignCenter);
  popup_set_context(app->popup, app);
  popup_set_callback(app->popup, popup_callback);
  popup_set_timeout(app->popup, 2200);
  popup_enable_timeout(app->popup);
  view_dispatcher_switch_to_view(app->view_dispatcher, TamaViewPopup);
}

static bool on_event_popup(void *context, SceneManagerEvent event) {
  TamagometerApp *app = context;
  if (event.type == SceneManagerEventTypeCustom &&
      event.event == TamaEventPopupDone) {
    scene_manager_previous_scene(app->scene_manager);
    return true;
  }
  return false;
}

static void on_exit_popup(void *context) {
  popup_reset(((TamagometerApp *)context)->popup);
}

static const AppSceneOnEnterCallback on_enter_handlers[] = {
    on_enter_welcome,     on_enter_main,        on_enter_sniffer,
    on_enter_categories,  on_enter_items,       on_enter_item_detail,
    on_enter_transfer,    on_enter_result,      on_enter_settings,
    on_enter_about,       on_enter_popup,
};
static const AppSceneOnEventCallback on_event_handlers[] = {
    on_event_welcome,     on_event_main,        on_event_sniffer,
    on_event_categories,  on_event_items,       on_event_item_detail,
    on_event_transfer,    on_event_result,      on_event_settings,
    on_event_none,        on_event_popup,
};
static const AppSceneOnExitCallback on_exit_handlers[] = {
    on_exit_widget,   on_exit_menu,   on_exit_widget, on_exit_menu,
    on_exit_menu,     on_exit_widget, on_exit_transfer,
    on_exit_widget,   on_exit_menu,   on_exit_widget,
    on_exit_popup,
};
static const SceneManagerHandlers scene_handlers = {
    .on_enter_handlers = on_enter_handlers,
    .on_event_handlers = on_event_handlers,
    .on_exit_handlers = on_exit_handlers,
    .scene_num = TamaSceneCount,
};

static TamagometerApp *app_alloc(void) {
  TamagometerApp *app = malloc(sizeof(TamagometerApp));
  memset(app, 0, sizeof(*app));
  app->selected_mode = TamaModeConnection;
  app->selected_category = TamaCategoryFood;
  strlcpy(app->last_status, "No transfers in this session",
          sizeof(app->last_status));
  tama_settings_load(&app->settings);
  app->radio_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
  app->state_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
  app->gui = furi_record_open(RECORD_GUI);
  app->notifications = furi_record_open(RECORD_NOTIFICATION);
  app->view_dispatcher = view_dispatcher_alloc();
  app->scene_manager = scene_manager_alloc(&scene_handlers, app);
  app->submenu = submenu_alloc();
  app->widget = widget_alloc();
  app->popup = popup_alloc();
  app->transfer_view = tamagometer_transfer_view_alloc();
  view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
  view_dispatcher_set_custom_event_callback(app->view_dispatcher,
                                            custom_event_callback);
  view_dispatcher_set_navigation_event_callback(app->view_dispatcher,
                                                navigation_event_callback);
  view_dispatcher_set_tick_event_callback(app->view_dispatcher,
                                          tick_event_callback, 250);
  view_dispatcher_add_view(app->view_dispatcher, TamaViewMenu,
                           submenu_get_view(app->submenu));
  view_dispatcher_add_view(app->view_dispatcher, TamaViewWidget,
                           widget_get_view(app->widget));
  view_dispatcher_add_view(
      app->view_dispatcher, TamaViewTransfer,
      tamagometer_transfer_view_get_view(app->transfer_view));
  view_dispatcher_add_view(app->view_dispatcher, TamaViewPopup,
                           popup_get_view(app->popup));
  tamagometer_transfer_view_set_cancel_callback(app->transfer_view,
                                                transfer_cancel_callback, app);
  view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui,
                                ViewDispatcherTypeFullscreen);
  app->cli = tamagometer_cli_alloc(app->radio_mutex);
  tamagometer_cli_register(app->cli);
  return app;
}

static void app_free(TamagometerApp *app) {
  app->cancel_requested = true;
  finish_transfer_thread(app);
  finish_sniffer_thread(app);
  tamagometer_cli_unregister_and_free(app->cli);
  view_dispatcher_remove_view(app->view_dispatcher, TamaViewPopup);
  view_dispatcher_remove_view(app->view_dispatcher, TamaViewTransfer);
  view_dispatcher_remove_view(app->view_dispatcher, TamaViewWidget);
  view_dispatcher_remove_view(app->view_dispatcher, TamaViewMenu);
  popup_free(app->popup);
  tamagometer_transfer_view_free(app->transfer_view);
  widget_free(app->widget);
  submenu_free(app->submenu);
  scene_manager_free(app->scene_manager);
  view_dispatcher_free(app->view_dispatcher);
  furi_record_close(RECORD_NOTIFICATION);
  furi_record_close(RECORD_GUI);
  furi_mutex_free(app->state_mutex);
  furi_mutex_free(app->radio_mutex);
  free(app);
}

int32_t tamagometer_companion(void *arg) {
  UNUSED(arg);
  TamagometerApp *app = app_alloc();
  scene_manager_next_scene(app->scene_manager, app->settings.onboarding_complete
                                                   ? TamaSceneMain
                                                   : TamaSceneWelcome);
  view_dispatcher_run(app->view_dispatcher);
  app_free(app);
  return 0;
}
