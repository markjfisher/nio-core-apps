#ifdef __MSDOS__
#include "fn_msdos.h"
#endif
#include "fnsvc.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
  unsigned count;
} list_ctx_t;

static char uri_buf[FNSVC_MAX_URI + 1];
#ifdef __ATARI__
static char input_buf[FNSVC_MAX_PATH + 1];
#endif

static void usage(void)
{
  puts("Usage: FLS [path]");
}

static void format_size(char *out, uint32_t size)
{
  static const char suffix[] = { ' ', 'k', 'M', 'G' };
  unsigned unit = 0;
  unsigned long divisor = 1UL;
  unsigned long rem;
  unsigned whole;
  unsigned tenths;

  while (size / divisor >= 1000UL && unit < 3) {
    divisor *= 1024UL;
    unit++;
  }

  if (unit == 0) {
    sprintf(out, "%6lu", size);
    return;
  }

  whole = (unsigned) (size / divisor);
  rem = size % divisor;
  while (rem > 429496729UL && divisor > 1UL) {
    rem = (rem + 5UL) / 10UL;
    divisor = (divisor + 5UL) / 10UL;
  }
  tenths = (unsigned) ((rem * 10UL + (divisor / 2UL)) / divisor);
  if (tenths >= 10) {
    whole++;
    tenths = 0;
  }

  if (whole < 10 && tenths != 0)
    sprintf(out, "%3u.%u%c", whole, tenths, suffix[unit]);
  else
    sprintf(out, "%5u%c", whole, suffix[unit]);
}

static void format_date(char *out, uint32_t mtime)
{
  static const char *months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  time_t t;
  time_t now;
  struct tm *tmv;
  struct tm *now_tm;
  int year = 0;
  int current_year = 0;

  if (mtime == 0) {
    strcpy(out, "?? ??? ??:??");
    return;
  }

  t = (time_t) mtime;
  tmv = localtime(&t);
  if (!tmv || tmv->tm_mon < 0 || tmv->tm_mon > 11) {
    strcpy(out, "?? ??? ??:??");
    return;
  }

  year = tmv->tm_year + 1900;
  now = time(NULL);
  if (now != (time_t) -1 && now != (time_t) 0) {
    now_tm = localtime(&now);
    if (now_tm)
      current_year = now_tm->tm_year + 1900;
  }

  if (current_year != 0 && current_year == year)
    sprintf(out, "%2d %s %02d:%02d", tmv->tm_mday, months[tmv->tm_mon],
            tmv->tm_hour, tmv->tm_min);
  else
    sprintf(out, "%2d %s %4d", tmv->tm_mday, months[tmv->tm_mon], year);
}

static void print_entry(uint8_t is_dir, const char *name, uint32_t size,
                        uint32_t mtime, void *ctx)
{
  list_ctx_t *lc = (list_ctx_t *) ctx;
  char size_buf[7];
  char date_buf[13];

  format_size(size_buf, is_dir ? 0 : size);
  format_date(date_buf, mtime);

  if (is_dir)
    printf("d %6s %s %s/\n", "-", date_buf, name);
  else
    printf("- %s %s %s\n", size_buf, date_buf, name);
  lc->count++;
}

#ifdef __MSDOS__
static const char *ioctl_detail_name(uint8_t detail)
{
  switch (detail) {
  case FN_MSDOS_IOCTL_DETAIL_NONE:
    return "none";
  case FN_MSDOS_IOCTL_DETAIL_DOS_ERROR:
    return "dos";
  case FN_MSDOS_IOCTL_DETAIL_BAD_SIGNATURE:
    return "sig";
  case FN_MSDOS_IOCTL_DETAIL_BAD_RESPONSE_LEN:
    return "len";
  case FN_MSDOS_IOCTL_DETAIL_NIO_STATUS:
    return "nio";
  default:
    return "?";
  }
}

static void print_ioctl_diag(void)
{
  uint8_t detail = fn_msdos_ioctl_last_detail();

  printf("IOCTL detail=%u(%s) dos=%u dev=%02X cmd=%02X nio=%u rlen=%u\n",
         (unsigned) detail,
         ioctl_detail_name(detail),
         fn_msdos_ioctl_last_error(),
         (unsigned) fn_msdos_ioctl_last_device(),
         (unsigned) fn_msdos_ioctl_last_command(),
         (unsigned) fn_msdos_ioctl_last_nio_status(),
         fn_msdos_ioctl_last_response_len());
  printf("DRV diag err=%u st=%u rx=%u exp=%u lsr=%02X\n",
         (unsigned) fn_msdos_ioctl_last_diag_error(),
         (unsigned) fn_msdos_ioctl_last_diag_status(),
         fn_msdos_ioctl_last_diag_rx_len(),
         fn_msdos_ioctl_last_diag_expected_len(),
         (unsigned) fn_msdos_ioctl_last_diag_lsr());
}
#else
static void print_ioctl_diag(void)
{
}
#endif

static int target_spec(const char *arg, char *uri, uint16_t uri_cap)
{
  uint16_t len = (uint16_t) strlen(arg ? arg : "");
  if (len >= uri_cap)
    return 0;
  if (len)
    memcpy(uri, arg, len);
  uri[len] = 0;
  return 1;
}

#ifdef __ATARI__
static const char *prompt_path(void)
{
  char *nl;

  printf("Path (blank=current): ");
  fflush(stdout);
  if (!fgets(input_buf, sizeof(input_buf), stdin))
    input_buf[0] = 0;

  nl = strchr(input_buf, '\n');
  if (nl)
    *nl = 0;
  nl = strchr(input_buf, '\r');
  if (nl)
    *nl = 0;

  return input_buf;
}
#endif

int main(int argc, char **argv)
{
  list_ctx_t ctx;
  const char *path_arg;

  if (argc > 2 || (argc > 1 && argv[1][0] == '?')) {
    usage();
    return 1;
  }

#ifdef __ATARI__
  path_arg = argc > 1 ? argv[1] : prompt_path();
#else
  path_arg = argc > 1 ? argv[1] : "";
#endif

  if (!target_spec(path_arg, uri_buf, sizeof(uri_buf))) {
    puts("Bad path");
    print_ioctl_diag();
    return 2;
  }

  ctx.count = 0;
  if (!fnsvc_list_directory(uri_buf, print_entry, &ctx)) {
    printf("Unable to list directory (err %u raw %u status %u len %u)\n",
           (unsigned) fnsvc_last_error(),
           (unsigned) fnsvc_last_raw_error(),
           (unsigned) fnsvc_last_status(),
           fnsvc_last_response_len());
    print_ioctl_diag();
    return 2;
  }

  printf("%u entr%s\n", ctx.count, ctx.count == 1 ? "y" : "ies");
  return 0;
}
