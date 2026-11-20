enum { CHARSET_US = 0, CHARSET_GERMAN, CHARSET_COUNT };
void vt100_set_charset(int Charset);
int vt100_get_charset(void);
const char *vt100_charset_name(int Charset);
void vt100_screen_save(void);
void vt100_screen_restore(void);
void vt100_screen_forget(void);
