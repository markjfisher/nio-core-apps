#ifndef FNSVC_H
#define FNSVC_H

#include <stdint.h>

#define FNSVC_MAX_URI   255
#define FNSVC_MAX_PATH  127

#ifdef CONFIG_NIO_BBC_LITE
#define FNSVC_MOUNT_URI_MAX 160
#define FNSVC_MOUNT_MODE_MAX 4
#else
#define FNSVC_MOUNT_URI_MAX FNSVC_MAX_URI
#define FNSVC_MOUNT_MODE_MAX 8
#endif

enum {
  FNSVC_STATUS_OK = 0
};

enum {
  FNSVC_ERR_NONE = 0,
  FNSVC_ERR_INVALID_ARG,
  FNSVC_ERR_REQUEST_TOO_LARGE,
  FNSVC_ERR_TRANSPORT,
  FNSVC_ERR_STATUS,
  FNSVC_ERR_BAD_VERSION,
  FNSVC_ERR_SHORT_RESPONSE,
  FNSVC_ERR_ENTRIES_BOUNDS,
  FNSVC_ERR_ENTRY_BOUNDS
};

typedef struct {
  uint8_t enabled;
  char uri[FNSVC_MOUNT_URI_MAX + 1];
  char mode[FNSVC_MOUNT_MODE_MAX];
} fnsvc_mount_t;

typedef void (*fnsvc_list_cb)(uint8_t is_dir,
                              const char *name,
                              uint32_t size,
                              uint32_t mtime,
                              void *ctx);

int fnsvc_list_directory(const char *uri, fnsvc_list_cb cb, void *ctx);
#ifdef CONFIG_NIO_BBC_LITE
int fnsvc_list_directory_page(const char *uri, uint16_t start,
                              uint16_t max_payload, uint8_t max_entries,
                              fnsvc_list_cb cb,
                              void *ctx, uint16_t *next_start,
                              uint8_t *more);
#endif
int fnsvc_resolve_path(const char *base_uri, const char *arg,
                       char *resolved_uri, uint16_t resolved_cap,
                       char *display_path, uint16_t display_cap,
                       uint8_t *flags_out);
int fnsvc_get_mount(uint8_t slot, fnsvc_mount_t *mount);
int fnsvc_set_mount(uint8_t slot, const char *uri, const char *mode, uint8_t enabled);
int fnsvc_disk_mount(uint8_t slot, const char *uri, uint8_t readonly);
int fnsvc_disk_unmount(uint8_t slot);
int fnsvc_disk_restore_boot(uint8_t slot);
uint8_t fnsvc_last_error(void);
uint8_t fnsvc_last_status(void);
uint8_t fnsvc_last_raw_error(void);
uint16_t fnsvc_last_response_len(void);

#endif
