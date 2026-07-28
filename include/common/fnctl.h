#ifndef FNCTL_H
#define FNCTL_H

#include <stdint.h>

#define FNCTL_SIGNATURE "FUJI"
#define FNCTL_VERSION   1
#define FNCTL_MAX_DATA  512
#define FNCTL_MAX_URI   255
#define FNCTL_MAX_PATH  127
#define FNCTL_MAX_UNITS 8

enum {
  FNCTL_QUERY = 0,
  FNCTL_GET_STATE = 1,
  FNCTL_SET_STATE = 2,
  FNCTL_GET_UNIT_MAP = 3,
  FNCTL_SET_UNIT_MAP = 4,
  FNCTL_NIO_CALL = 5
};

typedef struct {
  uint8_t command;
  char signature[4];
  uint8_t unit;
  uint8_t version;
  uint8_t max_units;
} fnctl_query_t;

typedef struct {
  uint8_t command;
  char signature[4];
  uint8_t unit;
  uint8_t version;
  uint8_t max_units;
  uint16_t current_uri_len;
  uint16_t display_path_len;
  char current_uri[FNCTL_MAX_URI + 1];
  char display_path[FNCTL_MAX_PATH + 1];
} fnctl_state_t;

typedef struct {
  uint8_t command;
  char signature[4];
  uint8_t unit;
  uint8_t version;
  uint8_t max_units;
  uint8_t slot;
} fnctl_unit_map_t;

typedef struct {
  uint8_t command;
  char signature[4];
  uint8_t unit;
  uint8_t version;
  uint8_t device;
  uint8_t nio_command;
  uint8_t nio_status;
  uint16_t request_len;
  uint16_t response_len;
  uint8_t data[FNCTL_MAX_DATA];
} fnctl_nio_call_t;

int fnctl_find_drive(void);
int fnctl_find_drive_for_unit(uint8_t unit);
int fnctl_get_state(fnctl_state_t *state);
int fnctl_set_state(const char *uri, const char *display_path);
int fnctl_get_unit_slot(uint8_t unit, uint8_t *slot);
int fnctl_set_unit_slot(uint8_t unit, uint8_t slot);
int fnctl_nio_call(uint8_t device, uint8_t command,
                   const void *request, uint16_t request_len,
                   void *response, uint16_t response_capacity,
                   uint8_t *nio_status, uint16_t *response_len);
uint16_t fnctl_last_dos_error(void);
const char *fnctl_status_name(uint8_t status);

#endif
