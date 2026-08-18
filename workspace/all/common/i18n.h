#ifndef __I18N_H__
#define __I18N_H__

#ifdef __cplusplus
extern "C" {
#endif

#define I18N_DEFAULT_LANG "vi"

/**
 * Initialize i18n subsystem with a specific language code (e.g., "en", "vi").
 * Searches for language files in:
 * 1. SDCARD_PATH "/.userdata/shared/lang/<lang>.lang" (user override)
 * 2. RES_PATH "/lang/<lang>.lang" (system default)
 */
void i18n_init(const char *lang_code);

/**
 * Cleanup and free translation memory.
 */
void i18n_quit(void);

/**
 * Translate a string key. Returns translated UTF-8 string or original key if not found.
 */
const char *tr(const char *key);

/**
 * Get the currently active language code (e.g., "en", "vi").
 */
const char *i18n_get_lang(void);

/**
 * Get human-readable name of a language code (e.g., "en" -> "English", "vi" -> "Tiếng Việt").
 */
const char *i18n_get_lang_name(const char *lang_code);

/**
 * Get list of available language codes and names.
 * Returns number of languages found.
 */
int i18n_get_available_languages(char codes[][16], char names[][32], int max_langs);

/**
 * Standard localization macro.
 */
#ifndef _
#define _(str) tr(str)
#endif

#ifdef __cplusplus
}
#endif

#endif // __I18N_H__
