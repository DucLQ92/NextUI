#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "defines.h"
#include "utils.h"

#define HASH_TABLE_SIZE 1024

typedef struct HashNode {
    char *key;
    char *value;
    struct HashNode *next;
} HashNode;

static HashNode *table[HASH_TABLE_SIZE] = {0};
static char current_language[16] = I18N_DEFAULT_LANG;
static int is_initialized = 0;

static unsigned long hash_string(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash % HASH_TABLE_SIZE;
}

static void unescape_string(char *dst, const char *src) {
    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++;
            switch (*src) {
                case 'n': *dst++ = '\n'; break;
                case 't': *dst++ = '\t'; break;
                case 'r': *dst++ = '\r'; break;
                case '\\': *dst++ = '\\'; break;
                default:
                    *dst++ = '\\';
                    *dst++ = *src;
                    break;
            }
        } else {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

static void hash_insert(const char *key, const char *value) {
    unsigned long idx = hash_string(key);
    
    // Check if key already exists, replace value
    HashNode *node = table[idx];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            free(node->value);
            char *buf = malloc(strlen(value) + 1);
            unescape_string(buf, value);
            node->value = buf;
            return;
        }
        node = node->next;
    }

    // Allocate new node
    node = (HashNode *)malloc(sizeof(HashNode));
    node->key = strdup(key);
    char *buf = malloc(strlen(value) + 1);
    unescape_string(buf, value);
    node->value = buf;
    node->next = table[idx];
    table[idx] = node;
}

static const char *hash_lookup(const char *key) {
    unsigned long idx = hash_string(key);
    HashNode *node = table[idx];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            return node->value;
        }
        node = node->next;
    }
    return NULL;
}

static void hash_clear(void) {
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        HashNode *node = table[i];
        while (node) {
            HashNode *next = node->next;
            free(node->key);
            free(node->value);
            free(node);
            node = next;
        }
        table[i] = NULL;
    }
}

static int load_lang_file(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        // Strip trailing CR/LF
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n')) {
            line[--len] = '\0';
        }

        // Skip comments and empty lines
        if (len == 0 || line[0] == '#' || line[0] == ';') continue;

        char *sep = strchr(line, '=');
        if (!sep) continue;

        *sep = '\0';
        char *key = line;
        char *val = sep + 1;

        // Trim leading whitespace on key
        while (*key == ' ' || *key == '\t') key++;
        // Trim trailing whitespace on key
        char *end_key = sep - 1;
        while (end_key > key && (*end_key == ' ' || *end_key == '\t')) {
            *end_key = '\0';
            end_key--;
        }

        // Trim leading whitespace on val
        while (*val == ' ' || *val == '\t') val++;

        if (strlen(key) > 0) {
            hash_insert(key, val);
        }
    }

    fclose(f);
    return 1;
}

static const char *builtin_vi[][2] = {
    // Language Metadata
    {"LanguageName", "Tiếng Việt"},
    {"Language", "Ngôn ngữ"},
    {"Select interface language", "Chọn ngôn ngữ hiển thị giao diện"},
    {"Interface language", "Ngôn ngữ giao diện"},

    // Main Launcher Navigation & Categories
    {"Recently Played", "Đã chơi gần đây"},
    {"Recents", "Gần đây"},
    {"Consoles", "Hệ máy"},
    {"Tools", "Công cụ"},
    {"Collections", "Bộ sưu tập"},
    {"Games", "Trò chơi"},
    {"Empty", "Trống"},
    {"Empty folder", "Thư mục trống"},
    {"Searching...", "Đang tìm kiếm..."},
    {"No games found", "Không tìm thấy trò chơi"},
    {"No recent games", "Chưa có trò chơi gần đây"},
    {"No Recents", "Chưa có trò chơi gần đây"},
    {"No Preview", "Không có ảnh xem trước"},
    {"No ROMs found", "Không tìm thấy ROM"},
    {"No collections found", "Không có bộ sưu tập"},

    // Button Hints & Common Actions
    {"OPEN", "MỞ"},
    {"BACK", "QUAY LẠI"},
    {"OPTIONS", "TÙY CHỌN"},
    {"SELECT", "CHỌN"},
    {"CONFIRM", "XÁC NHẬN"},
    {"CANCEL", "HỦY"},
    {"DELETE", "XÓA"},
    {"INFO", "THÔNG TIN"},
    {"EXIT", "THOÁT"},
    {"SAVE", "LƯU"},
    {"LOAD", "TẢI"},
    {"CLOSE", "ĐÓNG"},
    {"TOGGLE", "BẬT/TẮT"},
    {"CHANGE", "THAY ĐỔI"},
    {"MUTE", "TẮT TIẾNG"},
    {"SCAN", "QUÉT"},
    {"CONNECT", "KẾT NỐI"},
    {"DISCONNECT", "NGẮT KẾT NỐI"},
    {"FORGET", "QUÊN"},
    {"SWITCH", "CHUYỂN"},
    {"RESET", "ĐẶT LẠI"},
    {"RENAME", "ĐỔI TÊN"},
    {"CLEAR", "DỌN DẸP"},
    {"OK", "ĐỒNG Ý"},
    {"OKAY", "ĐỒNG Ý"},
    {"SLEEP", "NGỦ"},
    {"MENU", "MENU"},
    {"POWER", "NGUỒN"},
    {"Power", "Nguồn"},
    {"SEARCH", "TÌM KIẾM"},
    {"PAGE", "TRANG"},
    {"RESUME", "TIẾP TỤC"},
    {"REMOVE", "GỠ BỎ"},
    {"SCROLL", "CUỘN"},
    {"ZOOM", "PHÓNG TO"},
    {"SET", "CÀI ĐẶT"},
    {"FINE", "CHỈNH TINH"},
    {"COARSE", "CHỈNH NHANH"},
    {"COPY", "SAO CHÉP"},
    {"APPLY", "ÁP DỤNG"},
    {"ENTER", "NHẬP"},
    {"BRIGHTNESS", "ĐỘ SÁNG"},
    {"COLOR TEMP", "NHIỆT ĐỘ MÀU"},
    {"MNU", "MENU"},
    {"BRGHT", "SÁNG"},
    {"SEL", "CHỌN"},
    {"CLTMP", "MÀU"},
    {"Select light", "Chọn đèn LED"},

    // Common Values & States
    {"On", "Bật"},
    {"Off", "Tắt"},
    {"on", "Bật"},
    {"off", "Tắt"},
    {"Enabled", "Đã bật"},
    {"Disabled", "Đã tắt"},
    {"Enable", "Bật"},
    {"Disable", "Tắt"},
    {"Auto", "Tự động"},
    {"Custom", "Tùy chỉnh"},
    {"Default", "Mặc định"},
    {"Unchanged", "Không đổi"},
    {"Muted", "Tắt tiếng"},
    {"Performance", "Hiệu năng"},
    {"Normal", "Bình thường"},
    {"Quiet", "Yên tĩnh"},
    {"Regular", "Thường"},
    {"Semi-bold", "Vừa"},
    {"Bold", "Đậm"},
    {"Single file", "Tệp đơn"},
    {"Directory", "Thư mục"},
    {"Single directory", "Thư mục đơn"},
    {"Subdirectories", "Thư mục con"},
    {"12-hour", "12 giờ"},
    {"24-hour", "24 giờ"},
    {"Content List", "Danh sách nội dung"},
    {"Game Switcher", "Trình chuyển đổi game"},
    {"Quick Menu", "Menu nhanh"},
    {"None", "Không gán"},
    {"Both", "Cả hai"},
    {"Dpad", "D-Pad"},
    {"Joystick", "Cần xoay (Joystick)"},
    {"MinUI (default)", "MinUI (mặc định)"},
    {"Retroarch (compressed)", "RetroArch (nén)"},
    {"Retroarch (uncompressed)", "RetroArch (không nén)"},
    {"Retroarch-ish (compressed)", "Kiểu RetroArch (nén)"},
    {"Retroarch-ish (uncompressed)", "Kiểu RetroArch (không nén)"},
    {"Generic", "Chung (Generic)"},

    // Main Settings Menu Categories
    {"Appearance", "Giao diện"},
    {"UI customization", "Tùy biến giao diện hiển thị"},
    {"Display", "Màn hình"},
    {"Display Settings", "Cài đặt màn hình"},
    {"System", "Hệ thống"},
    {"FN switch", "Công tắc FN"},
    {"FN switch settings", "Cài đặt công tắc chức năng FN"},
    {"Assignments", "Gán phím"},
    {"Customize button assignments", "Tùy chỉnh chức năng các phím"},
    {"In-Game", "Trong game"},
    {"In-game settings for MinArch", "Cài đặt khi chơi game cho MinArch"},
    {"Network", "Mạng Wi-Fi"},
    {"Bluetooth", "Bluetooth"},
    {"About", "Giới thiệu"},
    {"Notifications", "Thông báo"},
    {"Save state notifications", "Thông báo lưu trạng thái"},
    {"RetroAchievements", "Thành tựu RetroAchievements"},
    {"Achievement tracking settings", "Cài đặt theo dõi thành tựu"},

    // Settings - Appearance Menu
    {"Font", "Phông chữ"},
    {"The font to render all UI text.", "Phông chữ hiển thị văn bản giao diện."},
    {"Select UI font", "Chọn phông chữ cho giao diện"},
    {"Font style", "Kiểu phông chữ"},
    {"The style to render the UI font (e.g. bold)", "Độ đậm nhạt của phông chữ giao diện (ví dụ: in đậm)"},
    {"Select UI font weight", "Chọn độ đậm nhạt của phông chữ"},
    {"Palette", "Bảng màu giao diện"},
    {"Color Palette", "Bảng màu"},
    {"Pick a predefined color palette or edit the colors below.", "Chọn bảng màu có sẵn hoặc tùy chỉnh từng màu bên dưới."},
    {"Main Color", "Màu chính"},
    {"The color used to render main UI elements.", "Màu dùng để hiển thị các thành phần giao diện chính."},
    {"Primary Accent Color", "Màu điểm nhấn chính"},
    {"The color used to highlight important things in the user interface.", "Màu dùng để làm nổi bật các mục quan trọng."},
    {"Secondary Accent Color", "Màu điểm nhấn phụ"},
    {"A secondary highlight color.", "Màu làm nổi bật phụ."},
    {"List Text", "Chữ danh sách"},
    {"List text color", "Màu chữ hiển thị trong danh sách."},
    {"List Text Selected", "Chữ khi chọn"},
    {"List selected text color", "Màu chữ của mục đang được chọn."},
    {"Hint info Color", "Màu gợi ý và thông tin"},
    {"Color for button hints and info", "Màu sắc của nút gợi ý và thông tin."},
    {"Background Color", "Màu nền"},
    {"Background color used when no background image is set.", "Màu nền giao diện khi không đặt ảnh nền."},
    {"Background", "Màu nền"},
    {"Background color", "Màu nền chính của giao diện"},
    {"Pills", "Viên thuốc nổi"},
    {"Button pills color", "Màu sắc của các viên thuốc nổi"},
    {"Main text", "Chữ chính"},
    {"Main text color", "Màu chữ chính hiển thị"},
    {"Subtext", "Chữ phụ"},
    {"Subtext color", "Màu chữ phụ và chú thích"},
    {"Selection text", "Chữ khi chọn"},
    {"Selected text color", "Màu chữ của mục đang được chọn"},
    {"Selection pill", "Viên thuốc khi chọn"},
    {"Selected button pill color", "Màu viên thuốc của mục đang được chọn"},
    {"Dark text", "Chữ màu tối"},
    {"Dark text color", "Màu chữ tối hiển thị trên nền sáng"},
    {"Battery / Clock", "Pin / Đồng hồ"},
    {"Battery and clock color", "Màu sắc của biểu tượng pin và đồng hồ"},
    {"List preview", "Xem trước danh sách"},
    {"List preview background color", "Màu nền xem trước danh sách"},
    {"Show battery percentage", "Hiển thị phần trăm pin"},
    {"Show battery level as percent in the status pill", "Hiển thị % pin\nchính xác trên thanh trạng thái"},
    {"Show menu animations", "Hiệu ứng cuộn menu"},
    {"Enable or disable menu animations", "Bật hoặc tắt hiệu ứng cuộn mượt menu"},
    {"Animate items when opening, closing and scrolling", "Hiệu ứng mượt mà khi mở, đóng hoặc cuộn danh sách"},
    {"Menu transitions", "Hiệu ứng chuyển trang"},
    {"Style of animated transition when navigating menus", "Hiệu ứng chuyển cảnh khi điều hướng menu"},
    {"Game art corner radius", "Bo góc ảnh bìa game"},
    {"Set the radius for the rounded corners of game art", "Độ bo tròn các góc của ảnh bìa trò chơi"},
    {"Game art width", "Độ rộng ảnh bìa game"},
    {"Set the percentage of screen width used for game art.\nUI elements might overrule this to avoid clipping.", "Tỷ lệ phần trăm chiều rộng màn hình cho ảnh bìa game"},
    {"Show folder names at root", "Hiện tên thư mục ở trang chính"},
    {"Show folder names at root directory", "Hiển thị tên các thư mục ở thư mục gốc"},
    {"Show Recents", "Hiện mục Gần đây"},
    {"Show \"Recently Played\" menu entry in game list.", "Hiển thị mục \"Đã chơi gần đây\" trong danh sách game."},
    {"Show Tools", "Hiện mục Công cụ"},
    {"Show \"Tools\" menu entry in game list.", "Hiển thị mục \"Công cụ\" trong danh sách game."},
    {"Show game art", "Hiện ảnh bìa game"},
    {"Show game artwork in the main menu", "Hiển thị ảnh bìa trò chơi ở menu chính"},
    {"Use folder background for ROMs", "Dùng ảnh nền thư mục cho ROM"},
    {"If enabled, uses the emulator background image.\nOtherwise uses the default.", "Dùng ảnh nền riêng của từng hệ máy\nthay vì ảnh nền mặc định."},
    {"If enabled, used the emulator background image. Otherwise uses the default.", "Dùng ảnh nền riêng của từng hệ máy\nthay vì ảnh nền mặc định."},
    {"Show Quickswitcher UI", "Hiện giao diện Quickswitcher"},
    {"Show/hide Quickswitcher UI elements.\nWhen hidden, will only draw background images.", "Bật/tắt các thành phần giao diện của Trình chuyển game."},
    {"Game switcher curtain opacity", "Độ mờ màn che Quickswitcher"},
    {"Show/hide curtain overlay. Helps UI elements to \nstand out when using transparent backgrounds.", "Độ mờ lớp phủ giúp làm nổi bật giao diện trên nền ảnh."},
    {"Input prompt style", "Kiểu nút bấm gợi ý"},
    {"Select the style of input prompts.", "Chọn kiểu hiển thị nút gợi ý (Chữ / Biểu tượng)."},

    // Settings - Display Menu
    {"Brightness", "Độ sáng"},
    {"Display brightness (0 to 10)", "Độ sáng màn hình (từ 0 đến 10)"},
    {"Screen brightness (0-10)", "Độ sáng màn hình (0-10)"},
    {"Color temperature", "Nhiệt độ màu"},
    {"Color temperature (0 to 40)", "Độ ấm màu sắc của màn hình (0 đến 40)"},
    {"Screen color warmth (0-40)", "Độ ấm màu sắc của màn hình (0-40)"},
    {"Contrast", "Độ tương phản"},
    {"Contrast enhancement (-4 to 5)", "Tăng cường độ tương phản (-4 đến 5)"},
    {"Saturation", "Độ bão hòa"},
    {"Saturation enhancement (-5 to 5)", "Tăng cường độ bão hòa màu (-5 đến 5)"},
    {"Exposure", "Độ phơi sáng"},
    {"Exposure enhancement (-4 to 5)", "Tăng cường độ phơi sáng (-4 đến 5)"},
    {"White point correction", "Hiệu chỉnh điểm trắng"},
    {"Corrects the display white point to better match the \nsRGB standard, at the expense of some peak brightness.", "Hiệu chỉnh điểm trắng theo chuẩn sRGB để màu sắc chuẩn hơn."},
    {"Red gain", "Kênh màu đỏ (Red Gain)"},
    {"White point correction red channel gain (0 to 200)", "Tăng giảm kênh màu đỏ khi hiệu chỉnh (0 đến 200)"},
    {"Green gain", "Kênh màu xanh lá (Green Gain)"},
    {"White point correction green channel gain (0 to 200)", "Tăng giảm kênh màu xanh lá\nkhi hiệu chỉnh (0 đến 200)"},
    {"Blue gain", "Kênh màu xanh dương (Blue Gain)"},
    {"White point correction blue channel gain (0 to 200)", "Tăng giảm kênh màu xanh dương\nkhi hiệu chỉnh (0 đến 200)"},

    // Settings - System Menu
    {"Volume", "Âm lượng"},
    {"Speaker volume", "Âm lượng loa ngoài"},
    {"Speaker volume (0-20)", "Âm lượng loa ngoài (0-20)"},
    {"Screen timeout", "Thời gian tắt màn hình"},
    {"Period of inactivity before screen turns off (0-600s)", "Thời gian không hoạt động\ntrước khi tắt màn hình (0-600 giây)"},
    {"Suspend timeout", "Thời gian ngủ"},
    {"Time before device goes to sleep after screen is off (5-600s)", "Thời gian trước khi máy vào chế độ ngủ\nsau khi tắt màn hình (5-600 giây)"},
    {"Time before device goes to sleep\nafter screen is off (5-600s)", "Thời gian trước khi máy vào chế độ ngủ\nsau khi tắt màn hình (5-600 giây)"},
    {"Haptic feedback", "Rung phản hồi"},
    {"Enable or disable haptic feedback on certain actions in the OS", "Bật hoặc tắt rung phản hồi\ncho các thao tác trong hệ thống"},
    {"Enable or disable haptic feedback\non certain actions in the OS", "Bật hoặc tắt rung phản hồi\ncho các thao tác trong hệ thống"},
    {"Default view", "Giao diện khởi động"},
    {"The initial view to show on boot", "Giao diện hiển thị đầu tiên khi bật máy"},
    {"Show 24h time format", "Định dạng giờ 24h"},
    {"Show clock in the 24hrs time format", "Hiển thị đồng hồ theo định dạng 24 giờ"},
    {"Show clock", "Hiển thị đồng hồ"},
    {"Show clock in the status pill", "Hiển thị giờ ở góc trên thanh trạng thái"},
    {"Display time on the top-right pill in main menu", "Hiển thị giờ ở góc trên bên phải của menu chính"},
    {"Set time and date automatically", "Tự động đồng bộ ngày giờ"},
    {"Automatically adjust system time\nwith NTP (requires internet access)", "Tự động đồng bộ ngày giờ qua Internet (NTP)"},
    {"Time zone", "Múi giờ"},
    {"Your time zone", "Múi giờ hiện tại của bạn"},
    {"Time zone offset", "Chọn múi giờ của bạn"},
    {"NTP time sync", "Đồng bộ giờ qua mạng (NTP)"},
    {"Synchronize time over the internet using NTP", "Tự động đồng bộ ngày giờ qua Internet khi có Wi-Fi"},
    {"Save format", "Định dạng file Save"},
    {"Format for saved game files", "Định dạng cấu trúc lưu tệp tin save game"},
    {"The save format to use.\nMinUI: Game.gba.sav, Retroarch: Game.srm, Generic: Game.sav", "Định dạng tệp tin lưu tiến trình chơi (MinUI / RetroArch / Generic)"},
    {"State format", "Định dạng file State"},
    {"Save state format", "Định dạng file Save State"},
    {"Format for save state files", "Định dạng cấu trúc lưu tệp tin save state"},
    {"The save state format to use. MinUI: Game.st0, \nRetroarch-ish: Game.state.0, Retroarch: Game.state0", "Định dạng tệp tin lưu nhanh trạng thái game (Save State)"},
    {"Use extracted file name", "Dùng tên tệp đã giải nén"},
    {"Use the extracted file name instead of the archive name.\nOnly applies to cores that do not handle archives natively", "Sử dụng tên tệp sau khi giải nén thay vì tên file nén (.zip/.7z)"},
    {"Safe poweroff", "Tắt nguồn an toàn"},
    {"Bypasses the stock shutdown procedure to avoid the \"limbo bug\".\nInstructs the PMIC directly to soft disconnect the battery.", "Bỏ qua quy trình tắt nguồn gốc để tránh lỗi treo máy.\nRa lệnh trực tiếp cho chip nguồn PMIC ngắt nguồn pin an toàn."},
    {"Keep awake over USB", "Giữ sáng khi cắm USB"},
    {"Prevent screen-off and sleep while connected to a\ncomputer as a USB device (not just charging).", "Ngăn màn hình tắt hoặc ngủ khi đang kết nối máy tính qua USB."},
    {"Fan Speed", "Tốc độ quạt tản nhiệt"},
    {"Select the fan speed percentage (Quiet/Normal/Performance or 0-100%)", "Chọn mức tốc độ quạt\n(Yên tĩnh / Bình thường / Hiệu năng hoặc 0-100%)"},
    {"Select the fan speed percentage\n(Quiet/Normal/Performance or 0-100%)", "Chọn mức tốc độ quạt\n(Yên tĩnh / Bình thường / Hiệu năng hoặc 0-100%)"},
    {"Reset to defaults", "Khôi phục mặc định"},
    {"Resets all options in this menu to their default values.", "Đặt lại tất cả tùy chọn trong menu này\nvề giá trị mặc định ban đầu."},

    // Settings - FN Switch Menu
    {"Volume when toggled", "Âm lượng khi gạt FN"},
    {"FN switch disables LED", "Công tắc FN tắt đèn LED"},
    {"Switch will also disable LEDs", "Gạt công tắc FN sẽ đồng thời tắt toàn bộ đèn LED"},
    {"LED Theme Sync", "Đồng bộ LED theo bảng màu"},
    {"Sync LED color with the active palette's accent color. Also updates LedControl settings.", "Đồng bộ màu LED theo màu nhấn (accent) của bảng màu đang dùng. Cũng cập nhật cài đặt LedControl."},
    {"Brightness when toggled", "Độ sáng khi gạt FN"},
    {"Color temperature when toggled", "Nhiệt độ màu khi gạt FN"},
    {"Contrast when toggled", "Độ tương phản khi gạt FN"},
    {"Saturation when toggled", "Độ bão hòa khi gạt FN"},
    {"Exposure when toggled", "Độ phơi sáng khi gạt FN"},
    {"Turbo fire A", "Bắn tự động nút A (Turbo A)"},
    {"Enable turbo fire A", "Bật chế độ tự động bấm nhanh nút A"},
    {"Turbo fire B", "Bắn tự động nút B (Turbo B)"},
    {"Enable turbo fire B", "Bật chế độ tự động bấm nhanh nút B"},
    {"Turbo fire X", "Bắn tự động nút X (Turbo X)"},
    {"Enable turbo fire X", "Bật chế độ tự động bấm nhanh nút X"},
    {"Turbo fire Y", "Bắn tự động nút Y (Turbo Y)"},
    {"Enable turbo fire Y", "Bật chế độ tự động bấm nhanh nút Y"},
    {"Turbo fire L1", "Bắn tự động nút L1 (Turbo L1)"},
    {"Enable turbo fire L1", "Bật chế độ tự động bấm nhanh nút L1"},
    {"Turbo fire L2", "Bắn tự động nút L2 (Turbo L2)"},
    {"Enable turbo fire L2", "Bật chế độ tự động bấm nhanh nút L2"},
    {"Turbo fire R1", "Bắn tự động nút R1 (Turbo R1)"},
    {"Enable turbo fire R1", "Bật chế độ tự động bấm nhanh nút R1"},
    {"Turbo fire R2", "Bắn tự động nút R2 (Turbo R2)"},
    {"Enable turbo fire R2", "Bật chế độ tự động bấm nhanh nút R2"},
    {"Dpad mode when toggled", "Chế độ D-Pad khi gạt FN"},
    {"Dpad: default. Joystick: Dpad exclusively acts as analog stick.\nBoth: Dpad and Joystick inputs at the same time.", "Dpad: Mặc định. Joystick: D-Pad hoạt động như cần Analog. Cả hai: Nhận cả hai cùng lúc."},

    // Settings - Notifications Menu
    {"Save states", "Lưu trạng thái (Save State)"},
    {"Show notification when saving game state", "Hiện thông báo khi lưu trạng thái game"},
    {"Load states", "Tải trạng thái (Load State)"},
    {"Show notification when loading game state", "Hiện thông báo khi tải trạng thái game"},
    {"Screenshots", "Chụp ảnh màn hình"},
    {"Show notification when taking a screenshot", "Hiện thông báo khi chụp ảnh màn hình"},
    {"Vol / Display Adjustments", "Thanh điều chỉnh Âm lượng / Màn hình"},
    {"Show overlay for volume, brightness,\nand color temp adjustments", "Hiện thanh hiển thị khi tăng giảm âm lượng, độ sáng, màu sắc"},
    {"Duration", "Thời gian hiển thị"},
    {"How long notifications stay on screen", "Thời gian thông báo lưu lại trên màn hình"},

    // Settings - RetroAchievements Menu
    {"Enable Achievements", "Bật RetroAchievements"},
    {"Enable RetroAchievements integration", "Bật tích hợp thành tựu RetroAchievements"},
    {"Username", "Tên đăng nhập"},
    {"RetroAchievements username", "Tên tài khoản RetroAchievements"},
    {"Password", "Mật khẩu"},
    {"RetroAchievements password", "Mật khẩu tài khoản RetroAchievements"},
    {"Enter Username", "Nhập tên đăng nhập"},
    {"Enter Password", "Nhập mật khẩu"},
    {"Authenticate", "Đăng nhập / Xác thực"},
    {"Test credentials and retrieve API token", "Kiểm tra thông tin đăng nhập và lấy mã Token API"},
    {"Status", "Trạng thái"},
    {"Authentication status", "Trạng thái đăng nhập tài khoản"},
    {"Authenticated", "Đã đăng nhập"},
    {"Not authenticated", "Chưa đăng nhập"},
    {"Show Notifications", "Hiện thông báo thành tựu"},
    {"Show achievement unlock notifications", "Hiện thông báo khi mở khóa thành tựu mới"},
    {"Notification Duration", "Thời gian hiện thông báo"},
    {"How long achievement notifications stay on screen", "Thời lượng thông báo mở khóa thành tựu hiển thị"},
    {"Progress Duration", "Thời gian hiện tiến trình"},
    {"Duration for progress updates (top-left). Off to disable.", "Thời gian hiện tiến trình ở góc trên bên trái (Tắt để ẩn)"},
    {"Achievement Sort Order", "Sắp xếp thành tựu"},
    {"How achievements are sorted in the in-game menu", "Thứ tự sắp xếp danh sách thành tựu trong game"},
    {"Sync Offline Unlocks", "Đồng bộ thành tựu Offline"},
    {"No pending unlocks", "Không có thành tựu nào chờ đồng bộ"},
    {"Syncing achievements...", "Đang đồng bộ thành tựu..."},
    {"Achievements synced", "Đã đồng bộ thành tựu thành công"},

    // Settings - Assignments Menu
    {"The pak to launch when this button is pressed in the main menu.", "Công cụ sẽ mở khi bấm nút này ở menu chính."},
    {"FN button", "Nút FN"},
    {"FN1 button", "Nút FN1"},
    {"FN2 button", "Nút FN2"},
    {"FN3 button", "Nút FN3"},
    {"L3 button", "Nút L3"},
    {"L4 button", "Nút L4"},
    {"R3 button", "Nút R3"},
    {"R4 button", "Nút R4"},
    {"HOME button", "Nút HOME"},
    {"L3", "Nút L3"},
    {"L4", "Nút L4"},
    {"R3", "Nút R3"},
    {"R4", "Nút R4"},
    {"HOME", "Nút HOME"},

    // Firmware Update / Boot Messages
    {"Updating NextUI", "Đang cập nhật NextUI..."},
    {"Updating NextUI...", "Đang cập nhật NextUI..."},
    {"Installing NextUI", "Đang cài đặt NextUI..."},
    {"Installing NextUI...", "Đang cài đặt NextUI..."},
    {"Installing...", "Đang cài đặt..."},
    {"Extracting...", "Đang giải nén..."},
    {"Extracting", "Đang giải nén"},
    {"Installing", "Đang cài đặt"},

    // Settings - Network (WiFi) & Bluetooth Menu
    {"WiFi", "Wi-Fi"},
    {"Enable/disable WiFi", "Bật hoặc tắt kết nối Wi-Fi"},
    {"WiFi diagnostics", "Chẩn đoán Wi-Fi"},
    {"Enable/disable WiFi logging", "Bật hoặc tắt ghi log Wi-Fi"},
    {"Connect", "Kết nối"},
    {"Connect to this network.", "Kết nối vào mạng này."},
    {"Disconnect", "Ngắt kết nối"},
    {"Disconnect from this network.", "Ngắt kết nối khỏi mạng này."},
    {"Forget", "Quên mạng"},
    {"Removes credentials for this network.", "Xóa thông tin lưu mạng này."},
    {"Enter WiFi passcode", "Nhập mật khẩu Wi-Fi"},
    {"Enter Wifi passcode", "Nhập mật khẩu Wi-Fi"},
    {"Connecting...", "Đang kết nối..."},
    {"Enabling WiFi...", "Đang bật Wi-Fi..."},
    {"Disabling WiFi...", "Đang tắt Wi-Fi..."},
    {"Enable/disable Bluetooth", "Bật hoặc tắt kết nối Bluetooth"},
    {"Bluetooth diagnostics", "Chẩn đoán Bluetooth"},
    {"Enable/disable Bluetooth logging", "Bật hoặc tắt ghi log Bluetooth"},
    {"Maximum sampling rate", "Tần số lấy mẫu tối đa"},
    {"44100 Hz: better compatibility\n48000 Hz: better quality", "44100 Hz: tương thích tốt hơn\n48000 Hz: chất lượng cao hơn"},
    {"Pair", "Ghép đôi"},
    {"Pair this device.", "Ghép đôi với thiết bị này."},
    {"Connect to this device.", "Kết nối tới thiết bị này."},
    {"Disconnect from this device.", "Ngắt kết nối khỏi thiết bị này."},
    {"Unpair", "Hủy ghép đôi"},
    {"Unpair this device.", "Xóa ghép đôi thiết bị này."},
    {"Enabling Bluetooth...", "Đang bật Bluetooth..."},
    {"Disabling Bluetooth...", "Đang tắt Bluetooth..."},

    // Settings - About Menu
    {"NextUI version", "Phiên bản NextUI"},
    {"Platform", "Hệ máy phần cứng"},
    {"Stock OS version", "Phiên bản OS gốc"},
    {"Busybox version", "Phiên bản Busybox"},

    // Tools & Applications
    {"Settings", "Cài đặt"},
    {"Battery", "Pin"},
    {"Clock", "Đồng hồ"},
    {"Game Tracker", "Thời gian chơi"},
    {"LedControl", "Đèn LED"},
    {"Bootlogo", "Logo khởi động"},
    {"Input", "Kiểm tra phím"},
    {"Files", "Quản lý tệp"},
    {"Remove Loading", "Tắt màn hình chờ"},
    {"DotClean", "Dọn tệp rác MacOS (DotClean)"},
    {"Dot Clean", "Dọn tệp rác MacOS (DotClean)"},
    {"Dot_Clean", "Dọn tệp rác MacOS (DotClean)"},
    {"dot_clean", "Dọn tệp rác MacOS (DotClean)"},
    {"Updater", "Cập nhật"},
    {"EmuDrop", "EmuDrop (Tải game)"},
    {"Pak Store", "Pak Store (Tải app)"},
    {"Tính năng đang phát triển...", "Tính năng đang phát triển..."},
    {"Coming soon", "Tính năng đang phát triển..."},

    // Shutdown / Power Messages
    {"Quicksave created,\npowering off", "Đã lưu nhanh,\nđang tắt máy..."},
    {"Rebooting", "Đang khởi động lại..."},
    {"Powering off", "Đang tắt máy..."},
    {"Quicksave created,\npower off now", "Đã lưu nhanh,\nhãy tắt máy"},
    {"Power off now", "Hãy tắt máy"},

    // MinArch - In-Game Pause Menu
    {"Continue", "Tiếp tục"},
    {"Resume", "Tiếp tục"},
    {"Save", "Lưu nhanh (Save State)"},
    {"Load", "Tải lại (Load State)"},
    {"Options", "Tùy chọn"},
    {"Quit", "Thoát game"},
    {"Reset", "Khởi động lại"},
    {"Disc", "Đĩa"},
    {"none", "không"},

    // MinArch - Core Options (Tùy chọn giả lập)
    {"Boot mode", "Chế độ khởi động"},
    {"Boot Mode", "Chế độ khởi động"},
    {"Rumble support", "Hỗ trợ rung (Rumble)"},
    {"Rumble Support", "Hỗ trợ rung (Rumble)"},
    {"Rumble level", "Mức độ rung"},
    {"Use BIOS", "Sử dụng BIOS"},
    {"Use BIOS file if found", "Sử dụng tệp BIOS nếu có"},
    {"Skip BIOS intro", "Bỏ qua đoạn mở đầu BIOS"},
    {"Show BIOS boot logo", "Hiện logo khởi động BIOS"},
    {"Solar sensor level", "Cảm biến ánh sáng mặt trời"},
    {"Color correction", "Hiệu chỉnh màu sắc"},
    {"Color Correction", "Hiệu chỉnh màu sắc"},
    {"Interframe blending", "Hòa trộn khung hình"},
    {"Audio sample rate", "Tần số lấy mẫu âm thanh"},
    {"Audio buffer level", "Mức bộ đệm âm thanh"},
    {"Frameskip", "Bỏ qua khung hình (Frameskip)"},
    {"Frameskip threshold", "Ngưỡng bỏ qua khung hình"},
    {"Frameskip interval", "Khoảng cách bỏ qua khung hình"},
    {"PSX CPU Clock", "Xung nhịp CPU PSX"},
    {"Dynamic recompiler", "Trình biên dịch động (Dynarec)"},
    {"CD audio", "Âm thanh CD"},
    {"CD Audio", "Âm thanh CD"},
    {"Sound output", "Đầu ra âm thanh"},
    {"Sound Output", "Đầu ra âm thanh"},
    {"Sound quality", "Chất lượng âm thanh"},
    {"Sound Quality", "Chất lượng âm thanh"},
    {"Threaded rendering", "Render đa luồng"},
    {"Dithering", "Khử răng cưa (Dithering)"},
    {"Dithering pattern", "Mẫu khử răng cưa"},
    {"GB Colorization", "Tô màu Game Boy (Colorization)"},
    {"Internal Palette", "Bảng màu tích hợp"},
    {"Dark filter", "Bộ lọc màu tối"},
    {"Reduce slowdown", "Giảm giật lag (Reduce slowdown)"},
    {"Audio Interpolation", "Nội suy âm thanh"},
    {"Hi-Res Blending", "Hòa trộn độ phân giải cao"},
    {"System Region", "Phân vùng hệ máy"},
    {"Region", "Phân vùng"},
    {"FM Sound", "Âm thanh FM"},
    {"6-button pad", "Tay cầm 6 nút"},
    {"Low Pass Filter", "Bộ lọc thông thấp"},
    {"DIP Switches", "Công tắc DIP (DIP Switches)"},
    {"Diagnostic Mode", "Chế độ chẩn đoán"},
    {"Zapper Mode", "Chế độ súng Zapper"},
    {"Internal resolution", "Độ phân giải nội bộ"},
    {"Analog combo", "Tổ hợp bật cần Analog"},
    {"Multitap 1", "Bộ chia tay cầm 1 (Multitap)"},
    {"Multitap 2", "Bộ chia tay cầm 2 (Multitap)"},
    {"Lightgun", "Súng quang học (Lightgun)"},
    {"Memory card 1", "Thẻ nhớ 1 (Memory Card)"},
    {"Memory card 2", "Thẻ nhớ 2 (Memory Card)"},
    {"Show border", "Hiển thị viền màn hình"},
    {"Bilinear filtering", "Lọc mịn (Bilinear)"},

    // MinArch - Core Option Values & Common Words
    {"enabled", "Bật"},
    {"disabled", "Tắt"},
    {"Enabled", "Bật"},
    {"Disabled", "Tắt"},
    {"true", "Bật"},
    {"false", "Tắt"},
    {"True", "Bật"},
    {"False", "Tắt"},
    {"yes", "Có"},
    {"no", "Không"},
    {"Yes", "Có"},
    {"No", "Không"},
    {"auto", "Tự động"},
    {"default", "Mặc định"},
    {"Default", "Mặc định"},
    {"accurate", "Chính xác"},
    {"Accurate", "Chính xác"},
    {"fast", "Nhanh"},
    {"Fast", "Nhanh"},
    {"high", "Cao"},
    {"low", "Thấp"},
    {"medium", "Vừa"},
    {"software", "Phần mềm (Software)"},
    {"hardware", "Phần cứng (Hardware)"},
    {"Software", "Phần mềm (Software)"},
    {"Hardware", "Phần cứng (Hardware)"},
    {"mono", "Mono"},
    {"stereo", "Stereo"},
    {"Mono", "Mono"},
    {"Stereo", "Stereo"},
    {"Game Boy", "Game Boy"},
    {"Game Boy Color", "Game Boy Color"},
    {"Game Boy Advance", "Game Boy Advance"},
    {"Super Game Boy", "Super Game Boy"},
    {"NTSC", "NTSC"},
    {"PAL", "PAL"},
    {"World", "Quốc tế (World)"},
    {"Japan", "Nhật Bản (Japan)"},
    {"USA", "Mỹ (USA)"},
    {"Europe", "Châu Âu (Europe)"},

    // MinArch - Options Menu
    {"Frontend", "Giao diện & Hệ thống"},
    {"Emulator", "Tùy chọn giả lập"},
    {"Shaders", "Bộ lọc hình ảnh (Shaders)"},
    {"Cheats", "Mã gian lận (Cheats)"},
    {"Controls", "Điều khiển & Gán nút"},
    {"Shortcuts", "Phím tắt"},
    {"Achievements", "Thành tựu (RetroAchievements)"},
    {"Save Changes", "Lưu thay đổi"},

    // MinArch - Frontend Options Menu
    {"Screen Scaling", "Tỷ lệ màn hình"},
    {"Audio Resampling Quality", "Chất lượng âm thanh"},
    {"Resampling Quality", "Chất lượng âm thanh"},
    {"Ambient Mode", "Đèn viền Ambient"},
    {"Screen Effect", "Hiệu ứng màn hình"},
    {"Overlay", "Khung viền (Overlay)"},
    {"Offset screen X", "Lệch khung hình X"},
    {"Offset screen Y", "Lệch khung hình Y"},
    {"Screen Sharpness", "Độ sắc nét màn hình"},
    {"Sharpness", "Độ sắc nét"},
    {"Aspect Ratio", "Tỷ lệ khung hình"},
    {"Core Sync", "Đồng bộ khung hình (Core Sync)"},
    {"Sync Ref", "Nguồn đồng bộ (Sync)"},
    {"CPU Speed", "Tốc độ CPU"},
    {"Overclock", "Ép xung CPU"},
    {"Debug HUD", "Thông số Debug (HUD)"},
    {"Show Debug", "Hiển thị thông số Debug"},
    {"Performance HUD", "Hiển thị thông số (HUD)"},
    {"Max FF Speed", "Tốc độ tua nhanh tối đa"},
    {"Fast forward audio", "Âm thanh khi tua nhanh"},
    {"FF Audio", "Âm thanh khi tua nhanh"},
    {"Rewind", "Tua ngược thời gian"},
    {"Rewind Buffer", "Bộ nhớ đệm tua ngược"},
    {"Rewind Buffer (MB)", "Bộ nhớ đệm tua ngược (MB)"},
    {"Rewind Interval", "Tần suất lưu tua ngược"},
    {"Rewind Granularity", "Tần suất lưu tua ngược"},
    {"Rewind Compression", "Nén dữ liệu tua ngược"},
    {"Rewind Compression Speed", "Tốc độ nén LZ4"},
    {"Rewind audio", "Âm thanh khi tua ngược"},
    {"Rewind Audio", "Âm thanh khi tua ngược"},
    {"Rewind LZ4 Speed", "Tốc độ nén LZ4 tua ngược"},

    // MinArch - Frontend Option Values & Labels
    {"Native", "Gốc (Native)"},
    {"Aspect", "Tỷ lệ chuẩn (Aspect)"},
    {"Aspect Screen", "Tỷ lệ màn hình (Aspect Screen)"},
    {"Fullscreen", "Toàn màn hình"},
    {"Cropped", "Cắt viền (Cropped)"},
    {"Low", "Thấp"},
    {"Medium", "Vừa"},
    {"High", "Cao"},
    {"Max", "Tối đa"},
    {"Off", "Tắt"},
    {"On", "Bật"},
    {"Simple", "Đơn giản"},
    {"Detailed", "Chi tiết"},
    {"All", "Tất cả"},
    {"Top", "Phía trên"},
    {"FN", "Nút FN"},
    {"LR", "Nút L/R"},
    {"Top/LR", "Trên & L/R"},
    {"None", "Không"},
    {"Line", "Đường quét (Scanline)"},
    {"Grid", "Lưới điểm (LCD Grid)"},
    {"NEAREST", "Sắc nét (Nearest)"},
    {"LINEAR", "Mịn màng (Bilinear)"},
    {"Auto", "Tự động"},
    {"Screen", "Theo màn hình"},
    {"Performance", "Hiệu năng cao (Performance)"},
    {"Powersave", "Tiết kiệm pin (Powersave)"},
    {"Standard", "Tiêu chuẩn"},
    {"DualShock", "Tay cầm DualShock"},
    {"1 (best ratio)", "1 (Nén tốt nhất)"},
    {"2 (default)", "2 (Mặc định)"},
    {"4 (fast)", "4 (Nhanh)"},
    {"8 (faster)", "8 (Nhanh hơn)"},
    {"12 (fastest)", "12 (Nhanh nhất)"},
    {"16 ms (~60 fps)", "16 ms (~60 FPS)"},
    {"22 ms (~45 fps)", "22 ms (~45 FPS)"},
    {"25 ms (~40 fps)", "25 ms (~40 FPS)"},
    {"33 ms (~30 fps)", "33 ms (~30 FPS)"},
    {"50 ms (~20 fps)", "50 ms (~20 FPS)"},
    {"66 ms (~15 fps)", "66 ms (~15 FPS)"},
    {"100 ms (~10 fps)", "100 ms (~10 FPS)"},
    {"150 ms (~7 fps)", "150 ms (~7 FPS)"},
    {"200 ms (~5 fps)", "200 ms (~5 FPS)"},

    // MinArch - Frontend Option Descriptions
    {"Native uses integer scaling.\nAspect uses core reported aspect ratio.\nAspect screen uses screen aspect ratio\nFullscreen has non-square pixels.", "Native dùng tỷ lệ nguyên gốc.\nAspect dùng tỷ lệ chuẩn của hệ máy.\nAspect Screen dùng tỷ lệ của màn hình.\nFullscreen kéo dãn toàn màn hình."},
    {"Native uses integer scaling. Aspect uses core nreported aspect ratio.\nAspect screen uses screen aspect ratio\n Fullscreen has non-square\npixels. Cropped is integer scaled then cropped.", "Native dùng tỷ lệ nguyên gốc.\nAspect dùng tỷ lệ chuẩn của hệ máy.\nAspect Screen dùng tỷ lệ màn hình.\nFullscreen kéo dãn toàn màn hình.\nCropped phóng to và cắt viền."},
    {"Resampling quality higher takes more CPU", "Chất lượng lấy mẫu càng cao sẽ tốn nhiều CPU hơn."},
    {"Makes your leds follow on screen colors", "Đèn LED đổi màu theo hình ảnh trên màn hình."},
    {"Grid simulates an LCD grid.\nLine simulates CRT scanlines.\nEffects usually look best at native scaling.", "Grid mô phỏng lưới điểm LCD.\nLine mô phỏng đường quét CRT.\nHiệu ứng hiển thị đẹp nhất ở tỷ lệ Native."},
    {"Choose a custom overlay png from the Overlays folder", "Chọn khung viền ảnh png từ thư mục Overlays."},
    {"Offset X pixels", "Dịch chuyển vị trí X (pixels)."},
    {"Offset Y pixels", "Dịch chuyển vị trí Y (pixels)."},
    {"LINEAR smooths lines, but works better when final image is at higher resolution, so either core that outputs higher resolution or upscaling with shaders", "LINEAR làm mịn các nét răng cưa, hiển thị tốt nhất khi chạy ở độ phân giải cao hoặc kết hợp Shader."},
    {"Choose what should be used as a\nreference for the frame rate.\n\"Native\" uses the emulator frame rate,\n\"Screen\" uses the frame rate of the screen.", "Chọn nguồn tham chiếu cho tốc độ khung hình.\n\"Native\" đồng bộ theo giả lập gốc,\n\"Screen\" đồng bộ theo tần số quét màn hình."},
    {"Choose how the CPU scales.\nAuto is recommended for most users.", "Chọn chế độ điều chỉnh xung nhịp CPU.\nKhuyến nghị dùng Auto cho hầu hết game."},
    {"Show frames per second, cpu load,\nresolution, and scaler information.", "Hiển thị số khung hình/giây (FPS), tải CPU, độ phân giải và bộ chia tỷ lệ."},
    {"Display real-time FPS, CPU load/temp, RAM, and Battery on screen during gameplay.", "Hiển thị FPS, tải/nhiệt độ CPU, RAM và Pin theo thời gian thực khi chơi game."},
    {"Fast forward will not exceed the\nselected speed (but may be less\ndepending on game and emulator).", "Tốc độ tua nhanh sẽ không vượt quá mức chọn (tùy thuộc vào game và giả lập)."},
    {"Play or mute audio when fast forwarding.", "Phát hoặc tắt âm thanh khi đang tua nhanh."},
    {"Enable in-memory rewind buffer.\nMust set a shortcut to access rewind during gameplay.\nUses extra CPU and memory.", "Bật bộ nhớ đệm tua ngược thời gian trong RAM.\nCần gán phím tắt để sử dụng khi chơi.\nTính năng này sẽ dùng thêm CPU và RAM."},
    {"Memory reserved for rewind snapshots.\nIncrease for longer rewind times.", "Dung lượng RAM dành riêng cho tua ngược.\nTăng lên để tua ngược được thời gian dài hơn."},
    {"Interval between rewind snapshots.\nShorter intervals improve smoothness during rewind,\nbut increase CPU and memory usage.", "Khoảng thời gian giữa các bản lưu tua ngược.\nKhoảng thời gian càng ngắn thì tua càng mượt nhưng tốn thêm CPU/RAM."},
    {"Compress rewind snapshots to save memory at the cost of CPU.", "Nén dữ liệu tua ngược để tiết kiệm RAM (tiêu hao thêm CPU)."},
    {"LZ4 acceleration used for rewind snapshots.\nLower values compress more but use more CPU.", "Tốc độ tăng tốc LZ4 cho dữ liệu tua ngược.\nGiá trị thấp sẽ nén chặt hơn nhưng tốn nhiều CPU."},
    {"Play or mute audio when rewinding.", "Phát hoặc tắt âm thanh khi đang tua ngược."},

    // MinArch - Shaders Options
    {"Optional Shaders Settings", "Cài đặt bổ sung cho Shader"},
    {"If shaders have extra settings they will show up in this settings menu", "Nếu Shader có cài đặt nâng cao, chúng sẽ hiển thị tại đây."},
    {"Shader / Emulator Settings Preset", "Cấu hình mẫu Shader / Giả lập"},
    {"Load a premade shaders/emulators config.\nTo try out a preset, exit the game without saving settings!", "Nạp cấu hình Shader/Giả lập có sẵn.\nĐể thử preset, thoát game mà không lưu cài đặt!"},
    {"Number of Shaders", "Số lượng Shader"},
    {"Number of shaders 1 to 3", "Số lượng Shader chạy đồng thời (1 đến 3)."},
    {"Shader 1", "Shader 1"},
    {"Shader 2", "Shader 2"},
    {"Shader 3", "Shader 3"},
    {"Shader 1 Filter", "Bộ lọc Shader 1"},
    {"Shader 2 Filter", "Bộ lọc Shader 2"},
    {"Shader 3 Filter", "Bộ lọc Shader 3"},
    {"Shader 1 Source type", "Nguồn gốc Shader 1"},
    {"Shader 2 Source type", "Nguồn gốc Shader 2"},
    {"Shader 3 Source type", "Nguồn gốc Shader 3"},
    {"Shader 1 Texture Type", "Loại Texture Shader 1"},
    {"Shader 2 Texture Type", "Loại Texture Shader 2"},
    {"Shader 3 Texture Type", "Loại Texture Shader 3"},
    {"Shader 1 Scale", "Tỷ lệ phóng to Shader 1"},
    {"Shader 2 Scale", "Tỷ lệ phóng to Shader 2"},
    {"Shader 3 Scale", "Tỷ lệ phóng to Shader 3"},
    {"Method of upscaling, NEAREST or LINEAR", "Phương pháp phóng to: NEAREST hoặc LINEAR."},
    {"This will choose resolution source to scale from", "Chọn độ phân giải nguồn để phóng to hình ảnh."},
    {"This will scale images x times,\nscreen scales to screens resolution (can hit performance)", "Phóng to hình ảnh theo cấp số nhân,\nscreen sẽ phóng to theo độ phân giải màn hình."},
    {"source", "Nguồn gốc (Source)"},
    {"relative", "Tương đối (Relative)"},
    {"screen", "Toàn màn hình"},
    {"No shaders available\n/Shaders folder or shader files not found", "Không có Shader nào khả dụng\nKhông tìm thấy thư mục /Shaders hoặc tệp shader."},

    // MinArch - Controls & Shortcuts
    {"Controller", "Tay cầm điều khiển"},
    {"Select the type of controller.", "Chọn loại tay cầm điều khiển."},
    {"Press A to set and X to clear.\nSupports single button and MENU+button.", "Nhấn A để gán nút và X để xóa.\nHỗ trợ gán phím đơn hoặc MENU+phím."},
    {"Save State", "Lưu trạng thái (Save State)"},
    {"Load State", "Tải trạng thái (Load State)"},
    {"Reset Game", "Khởi động lại game"},
    {"Save & Quit", "Lưu & Thoát"},
    {"Cycle Scaling", "Đổi nhanh tỷ lệ màn hình"},
    {"Cycle Effect", "Đổi nhanh hiệu ứng màn hình"},
    {"Toggle FF", "Bật/Tắt tua nhanh"},
    {"Hold FF", "Giữ để tua nhanh"},
    {"Toggle Rewind", "Bật/Tắt tua ngược"},
    {"Hold Rewind", "Giữ để tua ngược"},
    {"Game Switcher", "Chuyển đổi game nhanh"},
    {"Screenshot", "Chụp ảnh màn hình"},
    {"Toggle HUD", "Bật/Tắt HUD"},
    {"Toggle Turbo A", "Bật/Tắt Turbo nút A"},
    {"Toggle Turbo B", "Bật/Tắt Turbo nút B"},
    {"Toggle Turbo X", "Bật/Tắt Turbo nút X"},
    {"Toggle Turbo Y", "Bật/Tắt Turbo nút Y"},
    {"Toggle Turbo L", "Bật/Tắt Turbo nút L"},
    {"Toggle Turbo L2", "Bật/Tắt Turbo nút L2"},
    {"Toggle Turbo R", "Bật/Tắt Turbo nút R"},
    {"Toggle Turbo R2", "Bật/Tắt Turbo nút R2"},
    {"A Button", "Nút A"},
    {"B Button", "Nút B"},
    {"X Button", "Nút X"},
    {"Y Button", "Nút Y"},
    {"L1 Button", "Nút L1"},
    {"R1 Button", "Nút R1"},
    {"L2 Button", "Nút L2"},
    {"R2 Button", "Nút R2"},
    {"L3 Button", "Nút L3"},
    {"R3 Button", "Nút R3"},
    {"Up", "Lên"},
    {"Down", "Xuống"},
    {"Left", "Trái"},
    {"Right", "Phải"},
    {"NONE", "KHÔNG"},
    {"DualShock Toggle Combo", "Tổ hợp bật/tắt DualShock"},
    {"Fast Forward", "Tua nhanh"},
    {"Volume +", "Tăng âm lượng"},
    {"Volume -", "Giảm âm lượng"},
    {"Brightness +", "Tăng độ sáng"},
    {"Brightness -", "Giảm độ sáng"},
    {"Color Temp +", "Tăng nhiệt độ màu"},
    {"Color Temp -", "Giảm nhiệt độ màu"},
    {"Contrast +", "Tăng độ tương phản"},
    {"Contrast -", "Giảm độ tương phản"},
    {"FPS Counter", "Hiển thị số FPS"},

    // MinArch - Save Changes Menu
    {"Save for console", "Lưu cho toàn bộ hệ máy"},
    {"Save for game", "Lưu riêng cho game này"},
    {"Restore defaults", "Khôi phục mặc định"},
    {"Saved for console.", "Đã lưu thiết lập cho toàn bộ hệ máy."},
    {"Saved for game.", "Đã lưu thiết lập cho game này."},
    {"Restored console defaults.", "Đã khôi phục mặc định hệ máy."},
    {"Restored defaults.", "Đã khôi phục cài đặt gốc."},
    {"Using defaults.", "Đang dùng thiết lập mặc định."},
    {"Using console config.", "Đang dùng cấu hình hệ máy."},
    {"Using game config.", "Đang dùng cấu hình riêng cho game."},

    // MinArch - Cheats & Shaders
    {"No cheat file loaded.\n\n", "Không tìm thấy file mã gian lận (Cheat).\n\n"},
    {"No extra settings found", "Không tìm thấy thiết lập bổ sung."},
    {"This category has no options.", "Danh mục này không có tùy chọn nào."},
    {"This core has no options.", "Giả lập này không có tùy chọn nào."},

    // MinArch - RetroAchievements
    {"SHOW ALL", "HIỆN TẤT CẢ"},
    {"SHOW LOCKED", "CHƯA MỞ KHÓA"},
    {"UNMUTE", "BẬT THÔNG BÁO"},
    {"MUTE", "TẮT THÔNG BÁO"},
    {"1 point", "1 điểm"},
    {"%u points", "%u điểm"},
    {"Progress: %s", "Tiến độ: %s"},
    {"Unlocked offline - pending sync", "Đã mở khóa offline - chờ đồng bộ"},
    {"%.2f%% unlock rate", "Tỷ lệ mở khóa: %.2f%%"},
    {"[Missable]", "[Có thể bỏ lỡ]"},
    {"[Progression]", "[Cốt truyện]"},
    {"[Win Condition]", "[Điều kiện thắng]"},
    {"Muted - progress notifications silenced", "Đã tắt thông báo tiến độ thành tựu"},
    {"Cheats disabled in Hardcore mode", "Mã Cheat bị vô hiệu hóa ở chế độ Hardcore"},

    {"Save for Game", "Lưu riêng cho game này"},
    {"Save for Console", "Lưu cho toàn bộ hệ máy"},
    {"Core Options", "Tùy chọn giả lập (Core)"},
    {"Frontend Options", "Tùy chọn giao diện"},
    {"Exit Game", "Thoát game"},
    {"Slot", "Vị trí"},
    {"Empty Slot", "Vị trí trống"},
    {"State Saved", "Đã lưu trạng thái thành công"},
    {"State Loaded", "Đã tải trạng thái thành công"},
    {"No Save State", "Chưa có tệp lưu trạng thái"},
    {"Cheats Enabled", "Đã kích hoạt Cheats"},
    {"Cheats Disabled", "Đã tắt Cheats"},
    {"No Cheats Found", "Không tìm thấy mã Cheat"},
    {"Connected", "Đã kết nối"},
    {"Disconnected", "Đã ngắt kết nối"},
    {"Scanning...", "Đang quét..."},
    {"Battery Low", "Pin yếu, vui lòng sạc!"},
    {"Charging", "Đang sạc pin"},
    {"Unmuted", "Đã bật tiếng"},
    {"Screenshot Saved", "Đã lưu ảnh chụp màn hình"},
    {"NTP Synced", "Đã đồng bộ thời gian NTP"},
    {"Failed to connect", "Kết nối thất bại, vui lòng thử lại"},
    {"Password required", "Cần nhập mật khẩu"},

    // Tools - Battery
    {"Battery usage: Last %s", "Mức sử dụng pin: %s qua"},
    {"Since Charge: %s", "Từ lúc sạc: %s"},
    {"Current: %s", "Hiện tại: %s"},
    {"Remaining: %s", "Còn lại: %s"},
    {"Longest: %s", "Lâu nhất: %s"},
    {"calculating", "Đang tính..."},
    {"16 hours", "16 giờ"},
    {"8 hours", "8 giờ"},
    {"4 hours", "4 giờ"},
    {"ZOOM", "PHÓNG TO"},
    {"SCROLL", "CUỘN"},

    // Tools - Clock
    {"12 HOUR", "12 GIỜ"},
    {"24 HOUR", "24 GIỜ"},
    {"SET", "LƯU / CÀI ĐẶT"},
    {"CANCEL", "HỦY"},

    // Tools - Game Tracker
    {"Time spent having fun: %s", "Tổng thời gian đã chơi: %s"},
    {"TOTAL ", "TỔNG "},
    {"  AVERAGE ", "  TRUNG BÌNH "},
    {"  # PLAYS ", "  SỐ LẦN CHƠI "},

    // Tools - Input
    {"QUIT", "THOÁT"},

    // Tools - LedControl
    {"Select light", "Chọn đèn LED"},
    {"Effect", "Hiệu ứng"},
    {"Color", "Màu sắc"},
    {"Speed", "Tốc độ"},
    {"Brightness (All Leds)", "Độ sáng (Tất cả LED)"},
    {"Info brightness (All Leds)", "Độ sáng thông báo (Tất cả LED)"},
    {"Info brightness", "Độ sáng thông báo"},
    {"Linear", "Tuyến tính"},
    {"Breathe", "Nhịp thở"},
    {"Interval Breathe", "Nhịp thở ngắt quãng"},
    {"Static", "Tĩnh"},
    {"Blink 1", "Nhấp nháy 1"},
    {"Blink 2", "Nhấp nháy 2"},
    {"Blink 3", "Nhấp nháy 3"},
    {"Rainbow", "Cầu vồng"},
    {"Twinkle", "Lấp lánh"},
    {"Fire", "Ngọn lửa"},
    {"Glitter", "Kim tuyến"},
    {"NeonGlow", "Đèn Neon"},
    {"Firefly", "Đom đóm"},
    {"Aurora", "Cực quang"},
    {"Reactive", "Phản hồi"},
    {"Topbar Rainbow", "Cầu vồng thanh trên"},
    {"Topbar night", "Ban đêm thanh trên"},
    {"LR Rainbow", "Cầu vồng Trái/Phải"},
    {"LR Reactive", "Phản hồi Trái/Phải"},
    {"F1 key", "Phím F1"},
    {"F2 key", "Phím F2"},
    {"Top bar", "Thanh trên"},
    {"L&R triggers", "Cò L & R"},
    {"Joysticks", "Cần Analog"},
    {"Triggers", "Các nút Cò"},
    {"Joystick L", "Cần Analog Trái"},
    {"Joystick R", "Cần Analog Phải"},
    {"Logo", "Đèn Logo"},

    // Tools - Remove Loading
    {"Done", "Hoàn tất"},
    {"Hoàn tất", "Hoàn tất"},

    {NULL, NULL}
};

void i18n_init(const char *lang_code) {
    i18n_quit();

    if (!lang_code || strlen(lang_code) == 0) {
        lang_code = I18N_DEFAULT_LANG;
    }

    strncpy(current_language, lang_code, sizeof(current_language) - 1);
    current_language[sizeof(current_language) - 1] = '\0';

    // If English, we use default built-in strings directly without loading file
    if (strcmp(current_language, "en") == 0) {
        is_initialized = 1;
        return;
    }

    // Populate built-in fallback translations for Vietnamese
    if (strcmp(current_language, "vi") == 0) {
        for (int i = 0; builtin_vi[i][0] != NULL; i++) {
            hash_insert(builtin_vi[i][0], builtin_vi[i][1]);
        }
    }

    char filepath[MAX_PATH];
    int loaded = 0;

    // 1. Try user override in SHARED_USERDATA_PATH/lang/<lang>.lang
    snprintf(filepath, sizeof(filepath), "%s/lang/%s.lang", SHARED_USERDATA_PATH, current_language);
    if (exists(filepath)) {
        loaded = load_lang_file(filepath);
    }

    // 2. Try system default in RES_PATH/lang/<lang>.lang
    if (!loaded) {
        snprintf(filepath, sizeof(filepath), "%s/lang/%s.lang", RES_PATH, current_language);
        if (exists(filepath)) {
            loaded = load_lang_file(filepath);
        }
    }

    // 3. Fallback for desktop/debug relative path
    if (!loaded) {
        snprintf(filepath, sizeof(filepath), "./res/lang/%s.lang", current_language);
        if (exists(filepath)) {
            loaded = load_lang_file(filepath);
        }
    }
    if (!loaded) {
        snprintf(filepath, sizeof(filepath), "../../skeleton/SYSTEM/res/lang/%s.lang", current_language);
        if (exists(filepath)) {
            loaded = load_lang_file(filepath);
        }
    }

    is_initialized = 1;
}

void i18n_quit(void) {
    hash_clear();
    is_initialized = 0;
}

const char *tr(const char *key) {
    if (!key || !is_initialized) return key;
    if (strcmp(current_language, "en") == 0) return key;

    const char *translated = hash_lookup(key);
    return translated ? translated : key;
}

const char *i18n_get_lang(void) {
    return current_language;
}

const char *i18n_get_lang_name(const char *lang_code) {
    if (!lang_code) return "English";
    if (strcmp(lang_code, "en") == 0) return "English";
    if (strcmp(lang_code, "vi") == 0) return "Tiếng Việt";
    if (strcmp(lang_code, "zh") == 0 || strcmp(lang_code, "zh-CN") == 0) return "简体中文";
    if (strcmp(lang_code, "zh-TW") == 0) return "繁體中文";
    if (strcmp(lang_code, "ja") == 0) return "日本語";
    if (strcmp(lang_code, "ko") == 0) return "한국어";
    if (strcmp(lang_code, "fr") == 0) return "Français";
    if (strcmp(lang_code, "es") == 0) return "Español";
    if (strcmp(lang_code, "de") == 0) return "Deutsch";
    return lang_code;
}

int i18n_get_available_languages(char codes[][16], char names[][32], int max_langs) {
    int count = 0;
    
    // Always provide Vietnamese as default option
    if (count < max_langs) {
        strncpy(codes[count], "vi", 16);
        strncpy(names[count], "Tiếng Việt", 32);
        count++;
    }

    // Always provide English
    if (count < max_langs) {
        strncpy(codes[count], "en", 16);
        strncpy(names[count], "English", 32);
        count++;
    }

    // Check directories for additional language files
    const char *paths[] = {
        RES_PATH "/lang",
        SHARED_USERDATA_PATH "/lang",
        "./res/lang",
        "../../skeleton/SYSTEM/res/lang",
        NULL
    };

    for (int p = 0; paths[p] != NULL; p++) {
        DIR *dir = opendir(paths[p]);
        if (!dir) continue;

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            char *ext = strstr(ent->d_name, ".lang");
            if (ext && strcmp(ext, ".lang") == 0) {
                char code[16] = {0};
                size_t code_len = ext - ent->d_name;
                if (code_len > 0 && code_len < sizeof(code)) {
                    strncpy(code, ent->d_name, code_len);
                    code[code_len] = '\0';

                    // Check if already in list
                    int exists = 0;
                    for (int i = 0; i < count; i++) {
                        if (strcmp(codes[i], code) == 0) {
                            exists = 1;
                            break;
                        }
                    }

                    if (!exists && count < max_langs) {
                        strncpy(codes[count], code, 16);
                        strncpy(names[count], i18n_get_lang_name(code), 32);
                        count++;
                    }
                }
            }
        }
        closedir(dir);
    }

    return count;
}
