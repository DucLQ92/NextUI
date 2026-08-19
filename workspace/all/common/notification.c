#include "notification.h"
#include "defines.h"
#include "api.h"
#include "config.h"
#include "utils.h"
#include <string.h>
#include <stdio.h>

///////////////////////////////
// Layout constants (unscaled)
///////////////////////////////

#define NOTIF_PADDING_X      8   // Horizontal padding inside pill
#define NOTIF_PADDING_Y      4   // Vertical padding inside pill
#define NOTIF_MARGIN        12   // Margin from screen edge
#define NOTIF_STACK_GAP      6   // Gap between stacked notifications
#define NOTIF_ICON_GAP       4   // Gap between icon and text

// System indicator sizing (must match GFX_blitHardwareIndicator dimensions)
#define SYS_INDICATOR_EXTRA_PAD  4   // Extra padding for indicator pill

///////////////////////////////
// Performance HUD Overlay State
///////////////////////////////

static PerfHUDMode perf_hud_mode = PERF_HUD_OFF;
static float perf_hud_fps = 0.0f;
static uint32_t perf_hud_last_sample = 0;
static int perf_hud_dirty = 0;

typedef struct {
    int cpu_temp;       // °C
    int cpu_speed_mhz;  // MHz
    int cpu_usage_pct;  // %
    int ram_used_mb;    // MB
    int ram_total_mb;   // MB
    int bat_pct;        // %
    int bat_charging;   // 1 if charging
} PerfHUDMetrics;

static PerfHUDMetrics perf_hud_metrics = {0};
static unsigned long long last_cpu_active = 0;
static unsigned long long last_cpu_total = 0;

///////////////////////////////
// Internal state
///////////////////////////////

static Notification notifications[NOTIFICATION_MAX_QUEUE];
static int notification_count = 0;
static int initialized = 0;

// Persistent surface for GL rendering
static SDL_Surface* gl_notification_surface = NULL;
static int needs_clear_frame = 0;

// Screen dimensions for layer rendering
static int screen_width = 0;
static int screen_height = 0;

// Visual constants (will be set after init with proper scaling)
static int notif_padding_x;
static int notif_padding_y;
static int notif_margin;
static int notif_stack_gap;
static int notif_icon_gap;

// Track if we need to re-render (only when notifications change)
static int render_dirty = 1;
static int last_notification_count = 0;

///////////////////////////////
// System indicator state
///////////////////////////////

static SystemIndicatorType system_indicator_type = SYSTEM_INDICATOR_NONE;
static uint32_t system_indicator_start_time = 0;
static int system_indicator_dirty = 0;
static int last_system_indicator_type = SYSTEM_INDICATOR_NONE;

///////////////////////////////
// Progress indicator state
//
// Thread safety: progress_state is written by background threads
// (sync engine in ra_integration.c, badge download callbacks in
// ra_badges.c) and read by the main thread during rendering.
// All access is protected by progress_mutex.
///////////////////////////////

#define PROGRESS_TITLE_MAX 48
#define PROGRESS_STRING_MAX 16

typedef struct {
    char title[PROGRESS_TITLE_MAX];
    char progress[PROGRESS_STRING_MAX];
    SDL_Surface* icon;
    uint32_t start_time;
    int active;
    int dirty;
    int persistent;
} ProgressIndicatorState;

static ProgressIndicatorState progress_state = {0};
static SDL_mutex* progress_mutex = NULL;

///////////////////////////////
// Rounded rectangle drawing
///////////////////////////////

// Draw a filled rounded rectangle directly to RGBA pixel buffer.
// This is separate from GFX_blitPill* functions in api.c because:
// 1. Notifications render to an RGBA surface for GL overlay compositing
// 2. GFX_blitPill* use pre-made theme assets requiring screen format surfaces
// 3. Direct pixel manipulation avoids format conversion overhead during animation
// Which corners get rounded (see draw_rounded_rect_corners). An element flush
// against a screen edge wants the corners on that edge squared off, otherwise
// the rounding leaves a notch of game visible in the screen corner.
#define CORNER_TL 0x1
#define CORNER_TR 0x2
#define CORNER_BL 0x4
#define CORNER_BR 0x8
#define CORNER_ALL (CORNER_TL | CORNER_TR | CORNER_BL | CORNER_BR)

static void draw_rounded_rect_corners(SDL_Surface* surface, int x, int y, int w, int h, int radius, int corners, Uint32 color) {
    if (!surface || w <= 0 || h <= 0) return;
    
    // Clamp radius to half the smallest dimension
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    
    Uint32* pixels = (Uint32*)surface->pixels;
    int pitch = surface->pitch / 4; // pitch in pixels (32-bit)
    
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int draw = 1;
            
            // Check corners (only the ones selected in the mask are rounded)
            if ((corners & CORNER_TL) && px < radius && py < radius) {
                // Top-left corner
                int dx = radius - px - 1;
                int dy = radius - py - 1;
                if (dx * dx + dy * dy > radius * radius) draw = 0;
            } else if ((corners & CORNER_TR) && px >= w - radius && py < radius) {
                // Top-right corner
                int dx = px - (w - radius);
                int dy = radius - py - 1;
                if (dx * dx + dy * dy > radius * radius) draw = 0;
            } else if ((corners & CORNER_BL) && px < radius && py >= h - radius) {
                // Bottom-left corner
                int dx = radius - px - 1;
                int dy = py - (h - radius);
                if (dx * dx + dy * dy > radius * radius) draw = 0;
            } else if ((corners & CORNER_BR) && px >= w - radius && py >= h - radius) {
                // Bottom-right corner
                int dx = px - (w - radius);
                int dy = py - (h - radius);
                if (dx * dx + dy * dy > radius * radius) draw = 0;
            }
            
            if (draw) {
                pixels[(y + py) * pitch + (x + px)] = color;
            }
        }
    }
}

static void draw_rounded_rect(SDL_Surface* surface, int x, int y, int w, int h, int radius, Uint32 color) {
    draw_rounded_rect_corners(surface, x, y, w, h, radius, CORNER_ALL, color);
}

///////////////////////////////
// Internal helpers
///////////////////////////////

static void remove_notification(int index) {
    if (index < 0 || index >= notification_count) return;
    
    // Shift remaining notifications down
    for (int i = index; i < notification_count - 1; i++) {
        notifications[i] = notifications[i + 1];
    }
    notification_count--;
    render_dirty = 1;
}

///////////////////////////////
// Public API
///////////////////////////////

void Notification_init(void) {
    notification_count = 0;
    memset(notifications, 0, sizeof(notifications));
    
    // Initialize scaled visual constants (compact pills)
    notif_padding_x = SCALE1(NOTIF_PADDING_X);
    notif_padding_y = SCALE1(NOTIF_PADDING_Y);
    notif_margin = SCALE1(NOTIF_MARGIN);
    notif_stack_gap = SCALE1(NOTIF_STACK_GAP);
    notif_icon_gap = SCALE1(NOTIF_ICON_GAP);
    
    // Store screen dimensions for layer rendering
    screen_width = FIXED_WIDTH;
    screen_height = FIXED_HEIGHT;
    
    // Create progress indicator mutex (background threads write progress_state)
    if (!progress_mutex) {
        progress_mutex = SDL_CreateMutex();
    }
    
    render_dirty = 1;
    last_notification_count = 0;
    initialized = 1;
}

void Notification_push(NotificationType type, const char* message, SDL_Surface* icon) {
    if (!initialized) {
        return;
    }
    
    // Check if notifications are enabled for this type
    if ((type == NOTIFICATION_ACHIEVEMENT || type == NOTIFICATION_OFFLINE_ACHIEVEMENT) && !CFG_getRAShowNotifications()) {
        return;
    }
    
    // If queue is full, remove oldest notification
    if (notification_count >= NOTIFICATION_MAX_QUEUE) {
        remove_notification(0);
    }
    
    // Add new notification at end of queue
    Notification* n = &notifications[notification_count];
    n->type = type;
    // translate once at push time: message is never routed through _() again before
    // being rendered, so untranslated static notifications (eg. "Screenshot saved")
    // would otherwise always show in English regardless of the active language.
    strncpy(n->message, _(message), NOTIFICATION_MAX_MESSAGE - 1);
    n->message[NOTIFICATION_MAX_MESSAGE - 1] = '\0';
    n->icon = icon;
    n->start_time = SDL_GetTicks();
    
    // Use RA-specific duration for achievement notifications
    if (type == NOTIFICATION_ACHIEVEMENT || type == NOTIFICATION_OFFLINE_ACHIEVEMENT) {
        n->duration_ms = CFG_getRANotificationDuration() * 1000;
    } else {
        n->duration_ms = CFG_getNotifyDuration() * 1000;
    }
    n->state = NOTIFICATION_STATE_VISIBLE;
    
    notification_count++;
    render_dirty = 1;
}

static void update_perf_hud_metrics(uint32_t now) {
    if (perf_hud_mode == PERF_HUD_OFF) return;
    if (now - perf_hud_last_sample < 500 && perf_hud_last_sample > 0) return;
    perf_hud_last_sample = now;

    // 1. CPU Temp
    int temp = getInt("/sys/devices/virtual/thermal/thermal_zone0/temp");
    if (temp <= 0) temp = getInt("/sys/class/thermal/thermal_zone0/temp");
    perf_hud_metrics.cpu_temp = (temp > 0) ? (temp / 1000) : 0;

    // 2. CPU Speed
    int freq = getInt("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    perf_hud_metrics.cpu_speed_mhz = (freq > 0) ? (freq / 1000) : 0;

    // 3. CPU Usage % from /proc/stat
    FILE* fp = fopen("/proc/stat", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
            unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
            if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) >= 4) {
                unsigned long long active = user + nice + system + irq + softirq + steal;
                unsigned long long total = active + idle + iowait;
                if (last_cpu_total > 0 && total > last_cpu_total) {
                    unsigned long long d_active = active - last_cpu_active;
                    unsigned long long d_total = total - last_cpu_total;
                    if (d_total > 0) {
                        perf_hud_metrics.cpu_usage_pct = (int)((d_active * 100) / d_total);
                        if (perf_hud_metrics.cpu_usage_pct > 100) perf_hud_metrics.cpu_usage_pct = 100;
                    }
                }
                last_cpu_active = active;
                last_cpu_total = total;
            }
        }
        fclose(fp);
    }

    // 4. RAM Usage from /proc/meminfo
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[128];
        unsigned long mem_total_kb = 0, mem_avail_kb = 0, mem_free_kb = 0, buffers_kb = 0, cached_kb = 0;
        while (fgets(line, sizeof(line), fp)) {
            if (sscanf(line, "MemTotal: %lu kB", &mem_total_kb) == 1) {}
            else if (sscanf(line, "MemAvailable: %lu kB", &mem_avail_kb) == 1) {}
            else if (sscanf(line, "MemFree: %lu kB", &mem_free_kb) == 1) {}
            else if (sscanf(line, "Buffers: %lu kB", &buffers_kb) == 1) {}
            else if (sscanf(line, "Cached: %lu kB", &cached_kb) == 1) {}
        }
        fclose(fp);
        if (mem_total_kb > 0) {
            perf_hud_metrics.ram_total_mb = (int)(mem_total_kb / 1024);
            if (mem_avail_kb > 0) {
                perf_hud_metrics.ram_used_mb = (int)((mem_total_kb - mem_avail_kb) / 1024);
            } else {
                perf_hud_metrics.ram_used_mb = (int)((mem_total_kb - (mem_free_kb + buffers_kb + cached_kb)) / 1024);
            }
        }
    }

    // 5. Battery % & Status
    int bat_val = getInt("/tmp/percBat");
    if (bat_val <= 0) bat_val = getInt("/sys/class/power_supply/axp2202-battery/capacity");
    if (bat_val < 0) bat_val = 0;
    if (bat_val > 100) bat_val = 100;
    perf_hud_metrics.bat_pct = bat_val;

    char status[32] = {0};
    getFile("/sys/class/power_supply/axp2202-battery/status", status, sizeof(status));
    perf_hud_metrics.bat_charging = (strstr(status, "Charging") != NULL || getInt("/tmp/is_charging") == 1);

    perf_hud_dirty = 1;
}

void Notification_update(uint32_t now) {
    if (!initialized) return;

    // Update Performance HUD
    update_perf_hud_metrics(now);
    
    // Update system indicator timeout
    if (system_indicator_type != SYSTEM_INDICATOR_NONE) {
        uint32_t elapsed = now - system_indicator_start_time;
        if (elapsed >= SYSTEM_INDICATOR_DURATION_MS) {
            system_indicator_type = SYSTEM_INDICATOR_NONE;
            system_indicator_dirty = 1;
        }
    }
    
	// Update progress indicator timeout (skip if persistent).
	// Read-check-write must be atomic to avoid a TOCTOU race: a background
	// thread can call showProgressIndicator() between our read and write,
	// resetting start_time.  If we then write active=0 based on the stale
	// snapshot, the new notification is silently killed.
	SDL_LockMutex(progress_mutex);
	if (progress_state.active && !progress_state.persistent) {
		uint32_t elapsed = now - progress_state.start_time;
		int duration_seconds = CFG_getRAProgressNotificationDuration();
		if (duration_seconds > 0 && elapsed >= (uint32_t)(duration_seconds * 1000)) {
			progress_state.active = 0;
			progress_state.dirty = 1;
		}
	}
	SDL_UnlockMutex(progress_mutex);
    
    // Check each notification for expiration
    for (int i = 0; i < notification_count; i++) {
        Notification* n = &notifications[i];
        uint32_t elapsed = now - n->start_time;
        
        if (n->state == NOTIFICATION_STATE_VISIBLE && elapsed >= n->duration_ms) {
            n->state = NOTIFICATION_STATE_DONE;
        }
    }
    
    // Remove completed notifications (iterate backwards to avoid index issues)
    for (int i = notification_count - 1; i >= 0; i--) {
        if (notifications[i].state == NOTIFICATION_STATE_DONE) {
            remove_notification(i);
        }
    }
}

// Render system indicator (top-right)
// Width formula must match GFX_blitHardwareIndicator in api.c:
//   SCALE1(PILL_SIZE + SETTINGS_WIDTH + 10 + 4)
// The 10 is internal pill content padding (not the screen-edge PADDING macro).
static void render_system_indicator(void) {
    int indicator_width = SCALE1(PILL_SIZE + SETTINGS_WIDTH + 10 + SYS_INDICATOR_EXTRA_PAD);
    int indicator_height = SCALE1(PILL_SIZE);
    int indicator_x = screen_width - SCALE1(PADDING) - indicator_width;
    int indicator_y = SCALE1(PADDING);
    
    // Create a temporary surface with the SAME format as gfx.screen
    // This is critical because theme colors (THEME_COLOR2, etc.) were mapped
    // using SDL_MapRGB(gfx.screen->format, ...), so they only work correctly
    // on surfaces with that same pixel format.
    SDL_Surface* indicator_surface = GFX_createScreenFormatSurface(indicator_width, indicator_height);
    if (indicator_surface) {
        SDL_FillRect(indicator_surface, NULL, 0);
        GFX_blitHardwareIndicator(indicator_surface, 0, 0, (IndicatorType)system_indicator_type);
        
        // Convert to RGBA for the notification overlay
        SDL_Surface* converted = SDL_ConvertSurfaceFormat(indicator_surface, SDL_PIXELFORMAT_ABGR8888, 0);
        if (converted) {
            SDL_SetSurfaceBlendMode(converted, SDL_BLENDMODE_NONE);
            SDL_Rect dst_rect = {indicator_x, indicator_y, indicator_width, indicator_height};
            SDL_BlitSurface(converted, NULL, gl_notification_surface, &dst_rect);
            SDL_FreeSurface(converted);
        }
        SDL_FreeSurface(indicator_surface);
    }
}

// Render progress indicator pill (top-left)
// Takes a snapshot of progress state to avoid holding the mutex during rendering.
static void render_progress_indicator(const ProgressIndicatorState* snap) {
	SDL_Color text_color = uintToColour(THEME_COLOR1_255);
	SDL_Color bg_color_sdl = uintToColour(THEME_COLOR2_255);
	
	// Format: "Title: Progress" or just "Title"
	char progress_text[PROGRESS_TITLE_MAX + PROGRESS_STRING_MAX + 4];
	if (snap->progress[0] != '\0') {
		snprintf(progress_text, sizeof(progress_text), "%s: %s", 
		         snap->title, snap->progress);
	} else {
		snprintf(progress_text, sizeof(progress_text), "%s", snap->title);
	}
	
	int text_w = 0, text_h = 0;
	TTF_SizeUTF8(font.tiny, progress_text, &text_w, &text_h);
	
	// Calculate icon dimensions if present
	int icon_w = 0, icon_h = 0, icon_total_w = 0;
	if (snap->icon) {
		icon_h = text_h;
		icon_w = (snap->icon->w * icon_h) / snap->icon->h;
		icon_total_w = icon_w + notif_icon_gap;
	}
	
	int pill_w = icon_total_w + text_w + (notif_padding_x * 2);
	int pill_h = text_h + (notif_padding_y * 2);
	int corner_radius = pill_h / 2;
	int x = notif_margin;
	int y = notif_margin;
	
	SDL_Surface* progress_surface = SDL_CreateRGBSurfaceWithFormat(
		0, pill_w, pill_h, 32, SDL_PIXELFORMAT_ABGR8888
	);
	if (!progress_surface) return;
	
	SDL_FillRect(progress_surface, NULL, 0);
	Uint32 bg_color = SDL_MapRGBA(progress_surface->format, 
	                               bg_color_sdl.r, bg_color_sdl.g, bg_color_sdl.b, 255);
	draw_rounded_rect(progress_surface, 0, 0, pill_w, pill_h, corner_radius, bg_color);
	
	int content_x = notif_padding_x;
	
	if (snap->icon && icon_w > 0 && icon_h > 0) {
		SDL_Rect icon_dst = {content_x, notif_padding_y, icon_w, icon_h};
		SDL_SetSurfaceBlendMode(snap->icon, SDL_BLENDMODE_BLEND);
		SDL_BlitScaled(snap->icon, NULL, progress_surface, &icon_dst);
		content_x += icon_total_w;
	}
	
	SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font.tiny, progress_text, text_color);
	if (text_surf) {
		SDL_SetSurfaceBlendMode(text_surf, SDL_BLENDMODE_BLEND);
		SDL_Rect text_dst = {content_x, notif_padding_y, text_surf->w, text_surf->h};
		SDL_BlitSurface(text_surf, NULL, progress_surface, &text_dst);
		SDL_FreeSurface(text_surf);
	}
	
	SDL_SetSurfaceBlendMode(progress_surface, SDL_BLENDMODE_NONE);
	SDL_Rect dst_rect = {x, y, pill_w, pill_h};
	SDL_BlitSurface(progress_surface, NULL, gl_notification_surface, &dst_rect);
	SDL_FreeSurface(progress_surface);
}

// Render a single notification pill
static void render_notification_pill(Notification* n, int x, int y, SDL_Color text_color, SDL_Color bg_color_sdl) {
    int text_w = 0, text_h = 0;
    TTF_SizeUTF8(font.tiny, n->message, &text_w, &text_h);
    
    int icon_w = 0, icon_h = 0, icon_total_w = 0;
    if (n->icon) {
        icon_h = text_h;
        icon_w = (n->icon->w * icon_h) / n->icon->h;
        icon_total_w = icon_w + notif_icon_gap;
    }
    
    // Wifi-off indicator for offline achievement notifications
    int wifi_icon_w = 0;
    if (n->type == NOTIFICATION_OFFLINE_ACHIEVEMENT) {
        wifi_icon_w = SCALE1(12) + notif_icon_gap;  // icon natural size + gap
    }
    
    int pill_w = icon_total_w + wifi_icon_w + text_w + (notif_padding_x * 2);
    int pill_h = text_h + (notif_padding_y * 2);
    int corner_radius = pill_h / 2;
    
    SDL_Surface* notif_surface = SDL_CreateRGBSurfaceWithFormat(
        0, pill_w, pill_h, 32, SDL_PIXELFORMAT_ABGR8888
    );
    if (!notif_surface) return;
    
    SDL_FillRect(notif_surface, NULL, 0);
    Uint32 bg_color = SDL_MapRGBA(notif_surface->format, bg_color_sdl.r, bg_color_sdl.g, bg_color_sdl.b, 255);
    draw_rounded_rect(notif_surface, 0, 0, pill_w, pill_h, corner_radius, bg_color);
    
    int content_x = notif_padding_x;
    
    if (n->icon && icon_w > 0 && icon_h > 0) {
        SDL_Rect icon_dst = {content_x, notif_padding_y, icon_w, icon_h};
        SDL_SetSurfaceBlendMode(n->icon, SDL_BLENDMODE_BLEND);
        SDL_BlitScaled(n->icon, NULL, notif_surface, &icon_dst);
        content_x += icon_total_w;
    }
    
    // Blit wifi-off icon between badge and text for offline achievements
    if (n->type == NOTIFICATION_OFFLINE_ACHIEVEMENT) {
        int wifi_size = SCALE1(12);
        int wifi_y = notif_padding_y + (text_h - wifi_size) / 2;
        GFX_blitAssetColor(ASSET_WIFI_OFF, NULL, notif_surface,
                           &(SDL_Rect){content_x, wifi_y}, THEME_COLOR1_255);
        content_x += wifi_size + notif_icon_gap;
    }
    
    SDL_Surface* text_surf = TTF_RenderUTF8_Blended(font.tiny, n->message, text_color);
    if (text_surf) {
        SDL_SetSurfaceBlendMode(text_surf, SDL_BLENDMODE_BLEND);
        SDL_Rect text_dst = {content_x, notif_padding_y, text_surf->w, text_surf->h};
        SDL_BlitSurface(text_surf, NULL, notif_surface, &text_dst);
        SDL_FreeSurface(text_surf);
    }
    
    SDL_SetSurfaceBlendMode(notif_surface, SDL_BLENDMODE_NONE);
    SDL_Rect dst_rect = {x, y, pill_w, pill_h};
    SDL_BlitSurface(notif_surface, NULL, gl_notification_surface, &dst_rect);
    SDL_FreeSurface(notif_surface);
}

// Render notification stack (bottom-left, stacking upward)
static void render_notification_stack(void) {
    SDL_Color text_color = uintToColour(THEME_COLOR1_255);
    SDL_Color bg_color_sdl = uintToColour(THEME_COLOR2_255);
    
    int base_x = notif_margin;
    int base_y = screen_height - notif_margin;
    
    for (int i = 0; i < notification_count; i++) {
        Notification* n = &notifications[i];
        
        int text_h = 0;
        TTF_SizeUTF8(font.tiny, n->message, NULL, &text_h);
        int pill_h = text_h + (notif_padding_y * 2);
        
        // Calculate stack offset (how far up from base)
        int stack_offset = 0;
        for (int j = i + 1; j < notification_count; j++) {
            int other_text_h = 0;
            TTF_SizeUTF8(font.tiny, notifications[j].message, NULL, &other_text_h);
            int other_pill_h = other_text_h + (notif_padding_y * 2);
            stack_offset += other_pill_h + notif_stack_gap;
        }
        
        int x = base_x;
        int y = base_y - pill_h - stack_offset;
        
        render_notification_pill(n, x, y, text_color, bg_color_sdl);
    }
}

// Render Performance HUD (flush to the top-right screen corner, compact size)
static void render_perf_hud(void) {
    if (perf_hud_mode == PERF_HUD_OFF) return;

    TTF_Font* f = font.micro ? font.micro : font.tiny;
    char hud_buf[160];
    if (perf_hud_mode == PERF_HUD_SIMPLE) {
        snprintf(hud_buf, sizeof(hud_buf), "%.0f FPS  |  %d°C  |  CPU %d%%  |  RAM %dMB",
                 perf_hud_fps,
                 perf_hud_metrics.cpu_temp,
                 perf_hud_metrics.cpu_usage_pct,
                 perf_hud_metrics.ram_used_mb);
    } else {
        if (perf_hud_metrics.cpu_speed_mhz >= 1000) {
            float ghz = (float)perf_hud_metrics.cpu_speed_mhz / 1000.0f;
            snprintf(hud_buf, sizeof(hud_buf), "%.1f FPS  |  CPU %d%% (%.1fGHz %d°C)  |  RAM %d/%dMB  |  %d%%%s",
                     perf_hud_fps,
                     perf_hud_metrics.cpu_usage_pct,
                     ghz,
                     perf_hud_metrics.cpu_temp,
                     perf_hud_metrics.ram_used_mb,
                     perf_hud_metrics.ram_total_mb,
                     perf_hud_metrics.bat_pct,
                     perf_hud_metrics.bat_charging ? "⚡" : "");
        } else {
            snprintf(hud_buf, sizeof(hud_buf), "%.1f FPS  |  CPU %d%% (%dMHz %d°C)  |  RAM %d/%dMB  |  %d%%%s",
                     perf_hud_fps,
                     perf_hud_metrics.cpu_usage_pct,
                     perf_hud_metrics.cpu_speed_mhz,
                     perf_hud_metrics.cpu_temp,
                     perf_hud_metrics.ram_used_mb,
                     perf_hud_metrics.ram_total_mb,
                     perf_hud_metrics.bat_pct,
                     perf_hud_metrics.bat_charging ? "⚡" : "");
        }
    }

    int text_w = 0, text_h = 0;
    TTF_SizeUTF8(f, hud_buf, &text_w, &text_h);

    int pill_pad_x = SCALE1(6);
    int pill_pad_y = SCALE1(3);
    int pill_w = text_w + (pill_pad_x * 2);
    int pill_h = text_h + (pill_pad_y * 2);
    int corner_radius = SCALE1(4);

    // Flush against the top and right screen edges.
    int hud_x = screen_width - pill_w;
    int hud_y = 0;

    // The system indicator lives in the same corner (SCALE1(PADDING) inset,
    // PILL_SIZE tall), so drop below it when it is showing instead of overlapping.
    if (system_indicator_type != SYSTEM_INDICATOR_NONE) {
        hud_y = SCALE1(PADDING + PILL_SIZE);
    }

    // Square off whichever corners sit on a screen edge; the right edge always
    // does, the top edge only when the HUD has not been pushed down.
    int corners = CORNER_BL;
    if (hud_y > 0) corners |= CORNER_TL;

    SDL_Surface* hud_surf = SDL_CreateRGBSurfaceWithFormat(
        0, pill_w, pill_h, 32, SDL_PIXELFORMAT_ABGR8888
    );
    if (!hud_surf) return;

    SDL_FillRect(hud_surf, NULL, 0);
    Uint32 bg_color = SDL_MapRGBA(hud_surf->format, 15, 20, 26, 215);
    draw_rounded_rect_corners(hud_surf, 0, 0, pill_w, pill_h, corner_radius, corners, bg_color);

    SDL_Color text_color = {226, 232, 240, 255};
    SDL_Surface* text_surf = TTF_RenderUTF8_Blended(f, hud_buf, text_color);
    if (text_surf) {
        SDL_SetSurfaceBlendMode(text_surf, SDL_BLENDMODE_BLEND);
        SDL_Rect text_dst = {pill_pad_x, pill_pad_y, text_surf->w, text_surf->h};
        SDL_BlitSurface(text_surf, NULL, hud_surf, &text_dst);
        SDL_FreeSurface(text_surf);
    }

    SDL_SetSurfaceBlendMode(hud_surf, SDL_BLENDMODE_NONE);
    SDL_Rect dst_rect = {hud_x, hud_y, pill_w, pill_h};
    SDL_BlitSurface(hud_surf, NULL, gl_notification_surface, &dst_rect);
    SDL_FreeSurface(hud_surf);
}

void Notification_renderToLayer(int layer) {
    (void)layer; // unused now, kept for API compatibility
    
    if (!initialized) {
        PLAT_clearNotificationSurface();
        return;
    }
    
	int has_notifications = notification_count > 0;
	int has_system_indicator = system_indicator_type != SYSTEM_INDICATOR_NONE;
	int has_perf_hud = (perf_hud_mode != PERF_HUD_OFF);
	
	// Snapshot progress state under lock — sub-microsecond critical section.
	// Rendering uses the snapshot so we never hold the lock during SDL calls.
	ProgressIndicatorState progress_snap;
	SDL_LockMutex(progress_mutex);
	progress_snap = progress_state;
	SDL_UnlockMutex(progress_mutex);
	
	int has_progress_indicator = progress_snap.active;
	
	if (!has_notifications && !has_system_indicator && !has_progress_indicator && !has_perf_hud) {
		// When all notifications, indicators, and HUD are gone, render one final transparent frame
		if (gl_notification_surface) {
			if (needs_clear_frame) {
				SDL_FillRect(gl_notification_surface, NULL, 0);
				PLAT_setNotificationSurface(gl_notification_surface, 0, 0);
				needs_clear_frame = 0;
				render_dirty = 0;
				system_indicator_dirty = 0;
				perf_hud_dirty = 0;
				SDL_LockMutex(progress_mutex);
				progress_state.dirty = 0;
				SDL_UnlockMutex(progress_mutex);
				last_system_indicator_type = SYSTEM_INDICATOR_NONE;
				return;
			}
			PLAT_clearNotificationSurface();
			SDL_FreeSurface(gl_notification_surface);
			gl_notification_surface = NULL;
		}
		return;
	}
	
	// We have notifications, indicators, or HUD
	needs_clear_frame = 1;
	
	// Check if anything changed
	int notifications_changed = render_dirty || notification_count != last_notification_count;
	int indicator_changed = system_indicator_dirty || system_indicator_type != last_system_indicator_type;
	int progress_changed = progress_snap.dirty;
	int hud_changed = has_perf_hud && perf_hud_dirty;
    
    if (!notifications_changed && !indicator_changed && !progress_changed && !hud_changed) {
        return;
    }
    
    // Create surface if needed
    if (!gl_notification_surface) {
        gl_notification_surface = SDL_CreateRGBSurfaceWithFormat(
            0, screen_width, screen_height, 32, SDL_PIXELFORMAT_ABGR8888
        );
        if (!gl_notification_surface) {
            return;
        }
    }
    
    // Clear to transparent
    SDL_FillRect(gl_notification_surface, NULL, 0);
    
    // Render each element type
    if (has_perf_hud) {
        render_perf_hud();
    }
    if (has_system_indicator) {
        render_system_indicator();
    }
	if (has_progress_indicator) {
		render_progress_indicator(&progress_snap);
	}
    if (has_notifications) {
        render_notification_stack();
    }
    
    // Set the notification surface for GL rendering
    PLAT_setNotificationSurface(gl_notification_surface, 0, 0);
    
	render_dirty = 0;
	last_notification_count = notification_count;
	system_indicator_dirty = 0;
	perf_hud_dirty = 0;
	last_system_indicator_type = system_indicator_type;
	
	// Clear dirty flag under lock — a background thread may have set it again
	// since our snapshot, in which case we'll re-render next frame.
	SDL_LockMutex(progress_mutex);
	progress_state.dirty = 0;
	SDL_UnlockMutex(progress_mutex);
}

bool Notification_isActive(void) {
    return initialized && notification_count > 0;
}

void Notification_clear(void) {
	notification_count = 0;
	
	SDL_LockMutex(progress_mutex);
	progress_state.active = 0;
	if (progress_state.icon) {
		SDL_FreeSurface(progress_state.icon);
		progress_state.icon = NULL;
	}
	progress_state.dirty = 1;
	SDL_UnlockMutex(progress_mutex);
	
	render_dirty = 1;
	PLAT_clearNotificationSurface();
    if (gl_notification_surface) {
        SDL_FreeSurface(gl_notification_surface);
        gl_notification_surface = NULL;
    }
}

void Notification_quit(void) {
    Notification_clear();
    system_indicator_type = SYSTEM_INDICATOR_NONE;
    initialized = 0;
    
    if (progress_mutex) {
        SDL_DestroyMutex(progress_mutex);
        progress_mutex = NULL;
    }
}

///////////////////////////////
// System Indicator Functions
///////////////////////////////

void Notification_showSystemIndicator(SystemIndicatorType type) {
    if (!initialized) return;
    if (type == SYSTEM_INDICATOR_NONE) return;
    
    // Update or start the indicator
    system_indicator_type = type;
    system_indicator_start_time = SDL_GetTicks();
    system_indicator_dirty = 1;
}

bool Notification_hasSystemIndicator(void) {
    return initialized && system_indicator_type != SYSTEM_INDICATOR_NONE;
}

int Notification_getSystemIndicatorWidth(void) {
    if (!initialized || system_indicator_type == SYSTEM_INDICATOR_NONE) {
        return 0;
    }
    // Must match GFX_blitHardwareIndicator width formula (10 = internal pill padding)
    return SCALE1(PILL_SIZE + SETTINGS_WIDTH + 10 + SYS_INDICATOR_EXTRA_PAD);
}

///////////////////////////////
// Progress Indicator Functions
///////////////////////////////

void Notification_showProgressIndicator(const char* title, const char* progress, SDL_Surface* icon) {
	if (!initialized) return;
	
	// Check if RA notifications are enabled
	if (!CFG_getRAShowNotifications()) return;
	
	// Called from background threads (sync engine, badge downloads)
	SDL_LockMutex(progress_mutex);
	
	// Copy the title and progress strings
	strncpy(progress_state.title, title, PROGRESS_TITLE_MAX - 1);
	progress_state.title[PROGRESS_TITLE_MAX - 1] = '\0';
	
	strncpy(progress_state.progress, progress, PROGRESS_STRING_MAX - 1);
	progress_state.progress[PROGRESS_STRING_MAX - 1] = '\0';
	
	// Duplicate the icon so progress_state owns its copy and the badge cache
	// can free its surfaces (e.g. on game unload) without causing a use-after-free.
	SDL_Surface* prev_icon = progress_state.icon;
	progress_state.icon = icon ? SDL_DuplicateSurface(icon) : NULL;
	if (prev_icon) SDL_FreeSurface(prev_icon);
	
	// Activate and reset timer
	progress_state.active = 1;
	progress_state.start_time = SDL_GetTicks();
	progress_state.dirty = 1;
	
	SDL_UnlockMutex(progress_mutex);
}

void Notification_hideProgressIndicator(void) {
	if (!initialized) return;
	
	// Called from background threads (sync engine, badge downloads)
	SDL_LockMutex(progress_mutex);
	if (progress_state.active) {
		progress_state.active = 0;
		progress_state.persistent = 0;
		if (progress_state.icon) {
			SDL_FreeSurface(progress_state.icon);
			progress_state.icon = NULL;
		}
		progress_state.dirty = 1;
	}
	SDL_UnlockMutex(progress_mutex);
}

void Notification_setProgressIndicatorPersistent(bool persistent) {
	// Called from background threads
	SDL_LockMutex(progress_mutex);
	progress_state.persistent = persistent ? 1 : 0;
	SDL_UnlockMutex(progress_mutex);
}

bool Notification_hasProgressIndicator(void) {
    if (!initialized) return false;
    SDL_LockMutex(progress_mutex);
    bool active = progress_state.active;
    SDL_UnlockMutex(progress_mutex);
    return active;
}

///////////////////////////////
// Performance HUD Functions
///////////////////////////////

void Notification_setPerfHUDMode(PerfHUDMode mode) {
    perf_hud_mode = mode;
    perf_hud_dirty = 1;
    render_dirty = 1;
}

PerfHUDMode Notification_getPerfHUDMode(void) {
    return perf_hud_mode;
}

void Notification_togglePerfHUDMode(void) {
    perf_hud_mode = (PerfHUDMode)((perf_hud_mode + 1) % 3);
    perf_hud_dirty = 1;
    render_dirty = 1;
}

void Notification_setFPS(float fps) {
    perf_hud_fps = fps;
}

