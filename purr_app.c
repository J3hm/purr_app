/* 
 * F0 Purring cat - Clean Indent Edition
 * Version: 1.6.7
 * Author: J3hm
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <stdlib.h>

#define MIN_PWR 22
#define MAX_PWR 34
#define MIN_RYTM_MS 500
#define MAX_RYTM_MS 2000

typedef enum { StateOff, StateStarting, StateRunning, StateStopping, StateTrill } PurrState;

typedef struct {
    int power;
    int speed_ms;
    bool is_expiring;
    bool running;
    volatile PurrState state;
    FuriMutex* mutex;
    FuriMessageQueue* input_queue;
} PurrAppData;

// --- FONCTIONS SONORES ---

static void cat_meow_safe() {
    furi_hal_vibro_on(false);
    if(furi_hal_speaker_acquire(20)) { 
        uint32_t chance = furi_hal_random_get() % 10;
        if(chance <= 5) {
            for(int i = 0; i < 5; i++) {
                furi_hal_speaker_start(600.0f + (i * 60), 0.5f); 
                furi_delay_ms(15); furi_hal_speaker_stop(); furi_delay_ms(12);
            }
        } else if(chance <= 8) {
            for(int i = 0; i < 5; i++) {
                furi_hal_speaker_start(850.0f - (i * 60), 0.5f); 
                furi_delay_ms(15); furi_hal_speaker_stop(); furi_delay_ms(12);
            }
        } else {
            for(int i = 0; i < 8; i++) {
                furi_hal_speaker_start(700.0f, 0.4f); 
                furi_delay_ms(12); furi_hal_speaker_stop(); furi_delay_ms(15);
            }
        }
        furi_hal_speaker_release();
    }
}

// --- DESSIN ---

static void draw_cat_paw(Canvas* canvas, int x, int y, bool open) {
    canvas_draw_disc(canvas, x + 15, y + 20, 10);
    if (open) {
        canvas_draw_disc(canvas, x + 5, y + 10, 4);
        canvas_draw_disc(canvas, x + 12, y + 5, 4);
        canvas_draw_disc(canvas, x + 19, y + 5, 4);
        canvas_draw_disc(canvas, x + 26, y + 10, 4);
    } else {
        canvas_draw_disc(canvas, x + 7, y + 12, 3);
        canvas_draw_disc(canvas, x + 13, y + 9, 3);
        canvas_draw_disc(canvas, x + 18, y + 9, 3);
        canvas_draw_disc(canvas, x + 24, y + 12, 3);
    }
}

static void draw_callback(Canvas* canvas, void* context) {
    PurrAppData* app_data = context;
    if(furi_mutex_acquire(app_data->mutex, 20) != FuriStatusOk) return;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    // Titre d'état en haut
    canvas_set_font(canvas, FontPrimary);
    const char* status = (app_data->state == StateOff) ? "Zzz..." : 
                         (app_data->state == StateTrill) ? "Meow! <3" : "Purring...";
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignCenter, status);

    // Dessin de la patte (centrée au milieu)
    draw_cat_paw(canvas, 48, 16, (app_data->state == StateOff) ? false : app_data->is_expiring);
    
    canvas_set_font(canvas, FontSecondary);

    // --- BARRE PWR (Gauche) + Label Vertical ---
    canvas_draw_str(canvas, 4, 21, "P");
    canvas_draw_str(canvas, 3, 30, "W");
    canvas_draw_str(canvas, 4, 39, "R");
    canvas_draw_frame(canvas, 12, 20, 6, 22);
    int p_bar = ((app_data->power - MIN_PWR) * 20) / (MAX_PWR - MIN_PWR);
    if(p_bar < 0) {
        p_bar = 0;
    }
    if(p_bar > 20) {
        p_bar = 20;
    }
    canvas_draw_box(canvas, 13, 41 - p_bar, 4, p_bar);

    // --- BARRE RYTM (Droite) + Label Vertical ---
    canvas_draw_str(canvas, 120, 18, "R");
    canvas_draw_str(canvas, 120, 27, "Y");
    canvas_draw_str(canvas, 120, 36, "T");
    canvas_draw_str(canvas, 119, 45, "M");
    canvas_draw_frame(canvas, 110, 20, 6, 22);
    int r_bar = ((MAX_RYTM_MS - app_data->speed_ms) * 20) / (MAX_RYTM_MS - MIN_RYTM_MS);
    if(r_bar < 0) {
        r_bar = 0;
    }
    if(r_bar > 20) {
        r_bar = 20;
    }
    canvas_draw_box(canvas, 111, 41 - r_bar, 4, r_bar);

    // --- PIED DE PAGE ---
    if(app_data->state == StateOff) {
        canvas_draw_str_aligned(canvas, 64, 60, AlignCenter, AlignBottom, "By J3hm - Press OK to Start");
    } else {
        canvas_draw_str_aligned(canvas, 64, 60, AlignCenter, AlignBottom, "OK: Meow | Hold OK: Stop");
    }

    furi_mutex_release(app_data->mutex);
}

static void input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* input_queue = context;
    furi_message_queue_put(input_queue, input_event, 0);
}

// --- LOGIQUE PHYSIQUE ---

static void vibro_pulse(int p) {
    if(p < 5) p = 5;
    furi_hal_vibro_on(true);
    furi_delay_us(p * 100);
    furi_hal_vibro_on(false);
    furi_delay_us((100 - p + 1) * 100);
}

static void check_inputs(PurrAppData* app_data) {
    InputEvent ev;
    while(furi_message_queue_get(app_data->input_queue, &ev, 0) == FuriStatusOk) {
        if(ev.key == InputKeyBack) app_data->running = false;
        if(ev.key == InputKeyOk) {
            if(ev.type == InputTypeShort) {
                if(app_data->state == StateOff) app_data->state = StateStarting;
                else if(app_data->state == StateRunning) app_data->state = StateTrill;
            } else if(ev.type == InputTypeLong) {
                if(app_data->state == StateRunning) app_data->state = StateStopping;
            }
        }
        if(ev.type == InputTypePress || ev.type == InputTypeRepeat) {
            if(ev.key == InputKeyUp) app_data->power = (app_data->power >= MAX_PWR) ? MAX_PWR : app_data->power + 1;
            if(ev.key == InputKeyDown) app_data->power = (app_data->power <= MIN_PWR) ? MIN_PWR : app_data->power - 1;
            if(ev.key == InputKeyLeft) app_data->speed_ms = (app_data->speed_ms >= MAX_RYTM_MS) ? MAX_RYTM_MS : app_data->speed_ms + 100;
            if(ev.key == InputKeyRight) app_data->speed_ms = (app_data->speed_ms <= MIN_RYTM_MS) ? MIN_RYTM_MS : app_data->speed_ms - 100;
        }
    }
}

// --- BOUCLE PRINCIPALE ---

int32_t purr_app_main(void* p) {
    UNUSED(p);
    PurrAppData* app_data = malloc(sizeof(PurrAppData));
    app_data->power = 26; app_data->speed_ms = 1200;
    app_data->state = StateOff; app_data->running = true; app_data->is_expiring = false;
    app_data->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app_data->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw_callback, app_data);
    view_port_input_callback_set(vp, input_callback, app_data->input_queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    while(app_data->running) {
        check_inputs(app_data);

        if(app_data->state == StateOff) {
            furi_delay_ms(100);
            view_port_update(vp);
        } else if(app_data->state == StateStarting || app_data->state == StateStopping) {
            bool starting = (app_data->state == StateStarting);
            for(int i = 0; i < 15 && app_data->running; i++) {
                int v = starting ? (18 + i) : (30 - i);
                for(int j = 0; j < 4; j++) vibro_pulse(v);
                furi_delay_ms(10);
            }
            app_data->state = starting ? StateRunning : StateOff;
        } else if(app_data->state == StateTrill) {
            cat_meow_safe();
            for(int k = 0; k < 4; k++) {
                uint32_t part_t = furi_get_tick();
                while(furi_get_tick() - part_t < 80) {
                    vibro_pulse(app_data->power + 6);
                    furi_delay_ms(1);
                    check_inputs(app_data);
                }
                furi_hal_vibro_on(false);
                furi_delay_ms(60);
            }
            app_data->state = StateRunning;
        } else if(app_data->state == StateRunning) {
            uint32_t dice = furi_hal_random_get() % 40;
            uint32_t temp_speed = app_data->speed_ms;
            int temp_p = app_data->power;

            if(dice == 0) { 
                furi_hal_vibro_on(false);
                furi_delay_ms(400); 
            } else if(dice >= 36) { 
                temp_speed = app_data->speed_ms * 0.75;
                temp_p = (temp_p + 2 > MAX_PWR) ? MAX_PWR : temp_p + 2;
            }

            app_data->is_expiring = !app_data->is_expiring;
            view_port_update(vp);
            
            int drift = (furi_hal_random_get() % 160) - 80;
            uint32_t target_ms = temp_speed + drift;
            if(target_ms < 250) target_ms = 250;

            uint32_t start_t = furi_get_tick();
            int current_p = app_data->is_expiring ? temp_p : (temp_p * 75 / 100);

            while(app_data->running && app_data->state == StateRunning) {
                if(furi_get_tick() - start_t >= target_ms) break;
                vibro_pulse(current_p);
                furi_delay_ms(1); 
                check_inputs(app_data);
            }
        }
    }
    furi_hal_vibro_on(false);
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_mutex_free(app_data->mutex);
    furi_message_queue_free(app_data->input_queue);
    free(app_data);
    return 0;
}
