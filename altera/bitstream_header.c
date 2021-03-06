/*
 * Copyright (C) 2006, 2007 Jean-Baptiste Note <jean-baptiste.note@m4x.org>
 *
 * This file is part of debit.
 *
 * Debit is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Debit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with debit.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <stdint.h>

#include "debitlog.h"
#include "bitstream.h"
#include "bitstream_header.h"
#include "crc-ccitt.h"

typedef enum option_code {
  NEUTRAL = 0,
  BUILDTOOL = 1,
  CHIPTYPE = 2,
  NAME = 3,
  CRC16 = 8,
  BITSTREAM = 17,
  PACKAGE = 18,
  UNKNOWN = 19,
  RAM_M4K = 21,
} option_code_t;

#define PACKED __attribute__((packed))

/* we need the packed as this is an external structure */
/* fuck altera. They know nothing about alignment issues. Probably some
   inter mistake again */
typedef struct _bitstream_option {
  uint16_t option; /* le */
  uint32_t length; /* le */
  char data[];
} PACKED bitstream_option_t;

/* Depending on alignment issues on specific processors, this could be
   replaced by a more complex function, on ARM for instance */
static inline
uint32_t get_32(const char *data) {
  return *((uint32_t *)data);
}

static inline
uint16_t get_16(const char *data) {
  return *((uint16_t *)data);
}

static inline const bitstream_option_t *
parse_option(altera_bitstream_t *altera,
	     option_code_t *rcode,
	     const bitstream_option_t *opt) {
  unsigned code = GUINT16_FROM_LE(opt->option);
  guint32 length = GUINT32_FROM_LE(opt->length);
  const char *data = opt->data;

  debit_log(L_BITSTREAM, "option code %i, length %i", code, length);

  switch(code) {
  case BUILDTOOL:
    debit_log(L_BITSTREAM, "This bitstream build courtesy of %.*s. We love you !", length, data);
    break;
  case CHIPTYPE:
    debit_log(L_BITSTREAM, "Chip is a %.*s", length, data);
    break;
  case NAME:
    debit_log(L_BITSTREAM, "This bitstream's joyfull nickname is %.*s", length, data);
    break;
  case BITSTREAM:
    debit_log(L_BITSTREAM, "Got the bitstream data. The meat. What you want. (length %i)", length);
    altera->bitdata = data;
    altera->bitlength = length;
    break;
  case RAM_M4K:
    altera->m4kdata = data;
    altera->m4klength = length;
    debit_log(L_BITSTREAM, "Got the RAM Data");
    break;
  case PACKAGE:
    {
      guint32 id = GUINT32_FROM_LE(get_32(data));
      debit_log(L_BITSTREAM, "Something, maybe the package is %04x", id);
      (void) id;
    }
    break;
  case CRC16:
    {
      guint16 crc = GUINT16_FROM_LE(get_16(data));
      debit_log(L_BITSTREAM, "CRC16 for the bitstream is %04x", crc);
      (void) crc;
    }
    break;
  default:
    debit_log(L_BITSTREAM, "Unknown option code. Cheers ! And do not forget to contact me.");
  }

  /* Writeback computation */
  *rcode = code;
  return (const void *)&data[length];
}

#define SOF_MAGIC 0x534f4600

int
parse_bitstream_structure(altera_bitstream_t *bitstream,
			  const gchar *buf, const size_t buf_len) {
  const bitstream_option_t *current;
  const bitstream_option_t *last_option = (void *) (buf + buf_len);
  const gchar *read = buf;
  option_code_t code = NEUTRAL;

  /* recognize SOF magic */
  if (GUINT32_FROM_BE(get_32(read)) != SOF_MAGIC) {
    g_warning("This does not look like a .sof file");
    return -1;
  }

  /* seek to the first option past SOF -- I need more bitstream examples
     to reverse-engeneer the very beginning of the file */

  read += 3 * sizeof(uint32_t);
  current = (void *)read;

  while (current < last_option) {
    current = parse_option(bitstream, &code, current);
    if (code == CRC16) {
      uint16_t crc_val = ~crc_ccitt(0xffff, (unsigned char*)buf, buf_len);
      if (crc_val == 0x0f47)
	debit_log(L_BITSTREAM, "CRC okay");
      else
	g_warning("Wrong CRC, syndrome is %04x", crc_val);
    }
  }

  /* yeah, there can be no error with such rock-solid code */
  return 0;
}
