#include "../acun_remote_i.h"
#include "../recording.h"

/* Bound memory and work for files selected from the SD card. */
#define FILE_SYNC_MAX_BYTES  (1024u * 1024u)
#define FILE_SYNC_LINE_BYTES 4096u

static bool file_sync_read(Storage* storage, const char* path, SeqFrame* frame) {
    File* file = storage_file_alloc(storage);
    AcunRecording* recording = malloc(sizeof(AcunRecording));
    char* line = malloc(FILE_SYNC_LINE_BYTES);
    acun_recording_init(recording);
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    uint64_t size = ok ? storage_file_size(file) : 0;
    ok = ok && size && size <= FILE_SYNC_MAX_BYTES;
    size_t used = 0;
    uint8_t chunk[256];
    while(ok && size) {
        uint16_t wanted = size > sizeof(chunk) ? sizeof(chunk) : size;
        uint16_t got = storage_file_read(file, chunk, wanted);
        if(got != wanted) {
            ok = false;
            break;
        }
        size -= got;
        for(unsigned i = 0; ok && i < got; ++i) {
            char ch = chunk[i];
            if(ch == '\r') continue;
            if(ch == '\n') {
                line[used] = '\0';
                ok = acun_recording_line(recording, line);
                used = 0;
            } else if(!ch || used == FILE_SYNC_LINE_BYTES - 1) {
                ok = false;
            } else {
                line[used++] = ch;
            }
        }
    }
    if(ok && used) {
        line[used] = '\0';
        ok = acun_recording_line(recording, line);
    }
    ok = ok && acun_recording_finish(recording, frame);
    storage_file_close(file);
    storage_file_free(file);
    free(line);
    free(recording);
    return ok;
}

static void file_sync_selected(void* context) {
    AcunApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, AcunEventFileSelected);
}

void acun_scene_file_sync_on_enter(void* context) {
    AcunApp* app = context;
    app->file_sync_ready = false;
    file_browser_configure(app->file_browser, ".sub", "/ext", true, true, NULL, true);
    file_browser_set_callback(app->file_browser, file_sync_selected, app);
    file_browser_start(app->file_browser, app->file_path);
    app->file_browser_running = true;
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewFileBrowser);
}

static void file_sync_preview(AcunApp* app) {
    /* Leave the browser view before stopping its worker. The callback queues
     * an event; no file I/O or view changes run under its input/model lock. */
    popup_reset(app->popup);
    popup_set_header(app->popup, "Reading file", 64, 30, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewPopup);
    file_browser_stop(app->file_browser);
    app->file_browser_running = false;
    bool valid = file_sync_read(app->storage, furi_string_get_cstr(app->file_path), &app->heard);
    if(!valid) {
        acun_popup_show(
            app,
            "Cannot use file",
            "Need one RAW/BinRAW press\nat 433.92 MHz / AM270.",
            AcunAfterEntryMenu);
        return;
    }
    app->pending = app->store.entries[app->selected].profile;
    if(!seq_sync(&app->pending, &app->heard, &app->sync_delta)) {
        acun_popup_show(
            app, "Different button", "File does not match\nthis saved button.", AcunAfterEntryMenu);
        return;
    }
    app->file_sync_ready = true;
    if(app->sync_delta < 0)
        snprintf(
            app->text2,
            sizeof(app->text2),
            "%s\nFile %ld presses behind.\nSync may be rejected.",
            acun_selected_label(app),
            (long)-app->sync_delta);
    else
        snprintf(
            app->text2,
            sizeof(app->text2),
            "%s\nFile %ld presses ahead.\nFile may be outdated.",
            acun_selected_label(app),
            (long)app->sync_delta);
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, "Sync from file", 64, 3, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, app->text2, 64, 29, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Sync");
    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(app->dialog, acun_dialog_callback);
    view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewDialog);
}

bool acun_scene_file_sync_on_event(void* context, SceneManagerEvent event) {
    AcunApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == AcunEventFileSelected) {
        if(app->file_browser_running) file_sync_preview(app);
        return true;
    }
    if(event.event == AcunEventPopupDone) {
        acun_popup_done(app);
        return true;
    }
    if(!app->file_sync_ready) return false;
    if(event.event == AcunEventDialogLeft) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    if(event.event != AcunEventDialogRight) return false;
    RemoteEntry* entry = &app->store.entries[app->selected];
    app->file_sync_ready = false;
    if(remote_store_write(app->storage, entry->name, entry->button, &app->pending)) {
        entry->profile = app->pending;
        acun_popup_show(app, "Synced from file", acun_selected_label(app), AcunAfterEntryMenu);
    } else {
        entry->damaged = true;
        acun_popup_show(app, "Save failed", "Entry marked damaged.", AcunAfterSavedList);
    }
    return true;
}

void acun_scene_file_sync_on_exit(void* context) {
    AcunApp* app = context;
    app->file_sync_ready = false;
    if(app->file_browser_running) {
        view_dispatcher_switch_to_view(app->view_dispatcher, AcunViewSubmenu);
        file_browser_stop(app->file_browser);
        app->file_browser_running = false;
    }
    dialog_ex_reset(app->dialog);
    popup_reset(app->popup);
}
