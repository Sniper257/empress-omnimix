#include "hid_report.h"
#include <limits.h>
#include <string.h>

typedef struct {
  int32_t minimum, maximum;
  uint16_t page, size, count;
  uint8_t id;
} globals;
typedef struct {
  uint32_t usage[MH_USAGES], minimum, maximum;
  unsigned count, have_min, have_max;
} locals;
static uint32_t read_unsigned(const uint8_t *p, unsigned n) {
  uint32_t v = 0;
  for (unsigned i = 0; i < n; i++)
    v |= (uint32_t)p[i] << (i * 8);
  return v;
}
static int32_t signed_value(uint32_t v, unsigned n) {
  if (n && n < 4 && (v & (1u << (n * 8 - 1))))
    v |= ~((1u << (n * 8)) - 1u);
  return (int32_t)v;
}
static uint32_t full_usage(uint32_t value, unsigned size, unsigned page) {
  return size == 4 ? value : ((uint32_t)page << 16) | (value & 65535u);
}
static int report_index(mh_model *m, unsigned id) {
  for (unsigned i = 0; i < m->report_count; i++)
    if (m->reports[i].id == id)
      return (int)i;
  if (m->report_count == MH_REPORTS)
    return MH_LIMIT;
  unsigned i = m->report_count++;
  m->reports[i].id = id;
  return (int)i;
}
static uint32_t usage_at(const locals *l, unsigned i) {
  if (l->count)
    return l->usage[i < l->count ? i : l->count - 1];
  if (l->have_min && l->have_max && l->maximum >= l->minimum)
    return l->minimum +
           (i < l->maximum - l->minimum ? i : l->maximum - l->minimum);
  return 0;
}
static int add_field(mh_model *m, globals *g, unsigned app, unsigned ri,
                     unsigned off, unsigned flags, uint32_t usage,
                     unsigned array, int32_t selector) {
  unsigned page = usage >> 16, u = usage & 65535u, kind = 0;
  if (page == 9 && u)
    kind = MH_BUTTON;
  else if (page == 1 && u == 0x39)
    kind = MH_HAT;
  else if (page == 1 && ((u >= 0x30 && u <= 0x38) || (u >= 0x40 && u <= 0x48)))
    kind = MH_AXIS;
  if (!kind || !app)
    return MH_OK;
  if (m->field_count == MH_FIELDS)
    return MH_LIMIT;
  mh_field *f = m->fields + m->field_count++;
  f->minimum = g->minimum;
  f->maximum = g->maximum;
  f->selector = selector;
  f->offset = off;
  f->size = g->size;
  f->count = array ? g->count : 1;
  f->page = page;
  f->usage = u;
  f->flags = flags;
  f->report = ri;
  f->collection = app;
  f->kind = kind;
  f->array = array;
  return MH_OK;
}
int mh_parse(mh_model *m, const uint8_t *p, size_t bytes) {
  globals g = {0}, stack[MH_STACK];
  locals l = {0};
  unsigned depth = 0, app = 0, collections[MH_STACK], cd = 0;
  int error = MH_BAD_DESCRIPTOR;
  if (!m)
    return MH_BAD_DESCRIPTOR;
  memset(m, 0, sizeof *m);
  if (!p || bytes > 65535)
    return MH_BAD_DESCRIPTOR;
  for (size_t at = 0; at < bytes;) {
    unsigned h = p[at++];
    if (h == 0xfe) { /* Reserved long items do not change standard short-item
                        state. */
      if (bytes - at < 2)
        goto fail;
      unsigned n = p[at];
      at += 2;
      if (n > bytes - at)
        goto fail;
      at += n;
      continue;
    }
    unsigned n = h & 3u;
    if (n == 3)
      n = 4;
    if (n > bytes - at)
      goto fail;
    uint32_t v = read_unsigned(p + at, n);
    at += n;
    unsigned type = (h >> 2) & 3u, tag = h >> 4;
    if (type == 1) {
      switch (tag) {
      case 0:
        if (v > 65535)
          goto fail;
        g.page = v;
        break;
      case 1:
        g.minimum = signed_value(v, n);
        break;
      case 2:
        if (g.minimum >= 0 && v > INT32_MAX) {
          error = MH_LIMIT;
          goto fail;
        }
        g.maximum = g.minimum < 0 ? signed_value(v, n) : (int32_t)v;
        break;
      case 7:
        if (v > 32) {
          error = MH_LIMIT;
          goto fail;
        }
        g.size = v;
        break;
      case 8:
        if (!v || v > 255)
          goto fail;
        g.id = v;
        m->report_ids = 1;
        break;
      case 9:
        if (v > MH_REPORT_BITS) {
          error = MH_LIMIT;
          goto fail;
        }
        g.count = v;
        break;
      case 10:
        if (depth == MH_STACK) {
          error = MH_LIMIT;
          goto fail;
        }
        stack[depth++] = g;
        break;
      case 11:
        if (!depth)
          goto fail;
        g = stack[--depth];
        break;
      default:
        break; /* Physical units do not alter extraction. */
      }
    } else if (type == 2) {
      if (tag == 0) {
        if (l.count == MH_USAGES) {
          error = MH_LIMIT;
          goto fail;
        }
        l.usage[l.count++] = full_usage(v, n, g.page);
      } else if (tag == 1) {
        l.minimum = full_usage(v, n, g.page);
        l.have_min = 1;
      } else if (tag == 2) {
        l.maximum = full_usage(v, n, g.page);
        l.have_max = 1;
      } else if (tag == 10 && v) {
        error = MH_LIMIT;
        goto fail;
      } /* Alternative usage sets need explicit support. */
    } else if (type == 0) {
      if (tag == 10) {
        if (cd == MH_STACK) {
          error = MH_LIMIT;
          goto fail;
        }
        collections[cd++] = app;
        if (v == 1) {
          uint32_t u = usage_at(&l, 0);
          if (u == 0x10004 || u == 0x10005 || u == 0x10008) {
            if (m->collection_count == 255) {
              error = MH_LIMIT;
              goto fail;
            }
            app = ++m->collection_count;
          } else
            app = 0;
        }
      } else if (tag == 12) {
        if (!cd)
          goto fail;
        app = collections[--cd];
      } else if (tag == 8 || tag == 9 || tag == 11) {
        if (!g.size || !g.count || g.maximum < g.minimum)
          goto fail;
        unsigned stream = tag == 8 ? 0 : tag == 9 ? 1 : 2;
        int ri = report_index(m, g.id);
        if (ri < 0) {
          error = ri;
          goto fail;
        }
        unsigned off = m->reports[ri].bits[stream];
        if (g.count > MH_REPORT_BITS / g.size ||
            off + g.count * g.size > MH_REPORT_BITS) {
          error = MH_LIMIT;
          goto fail;
        }
        m->reports[ri].bits[stream] = off + g.count * g.size;
        if (stream == 0 && !(v & 1u) && app) {
          if (v & 2u) {
            for (unsigned i = 0; i < g.count; i++) {
              error = add_field(m, &g, app, ri, off + i * g.size, v,
                                usage_at(&l, i), 0, 0);
              if (error)
                goto fail;
            }
          } else {
            unsigned count = l.count;
            if (!count && l.have_min && l.have_max && l.maximum >= l.minimum) {
              if (l.maximum - l.minimum >= MH_USAGES) {
                error = MH_LIMIT;
                goto fail;
              }
              count = l.maximum - l.minimum + 1;
            }
            if (!count)
              goto fail;
            for (unsigned i = 0; i < count; i++) {
              uint32_t u = usage_at(&l, i);
              if ((int64_t)g.minimum + i > g.maximum)
                break;
              error = add_field(m, &g, app, ri, off, v, u, 1,
                                g.minimum + (int32_t)i);
              if (error)
                goto fail;
            }
          }
        }
      }
      memset(&l, 0, sizeof l); /* Local items expire after EVERY main item. */
    }
  }
  if (depth || cd)
    goto fail;
  if (m->report_ids)
    for (unsigned i = 0; i < m->report_count; i++)
      if (!m->reports[i].id && m->reports[i].bits[0])
        goto fail;
  if (!m->field_count) {
    error = MH_NOT_GAMEPAD;
    goto fail;
  }
  return MH_OK;
fail:
  memset(m, 0, sizeof *m);
  return error == MH_OK ? MH_BAD_DESCRIPTOR : error;
}
static int32_t extract(const uint8_t *p, unsigned off, unsigned size,
                       int sign) {
  uint32_t v = 0;
  for (unsigned copied = 0; copied < size;) {
    unsigned shift = off & 7u, n = 8 - shift;
    if (n > size - copied)
      n = size - copied;
    v |= ((p[off >> 3] >> shift) & ((1u << n) - 1u)) << copied;
    off += n;
    copied += n;
  }
  if (sign && size < 32 && (v & (1u << (size - 1))))
    v |= ~((1u << size) - 1u);
  return (int32_t)v;
}
int mh_decode(const mh_model *m, const uint8_t *p, size_t bytes,
              mh_values *values) {
  if (!m || !p || !values || !bytes)
    return MH_BAD_REPORT;
  unsigned id = 0;
  if (m->report_ids) {
    id = *p++;
    bytes--;
  }
  int ri = -1;
  for (unsigned i = 0; i < m->report_count; i++)
    if (m->reports[i].id == id) {
      ri = i;
      break;
    }
  if (ri < 0 || !m->reports[ri].bits[0])
    return MH_BAD_REPORT;
  if (bytes < (m->reports[ri].bits[0] + 7u) / 8u)
    return MH_SHORT_REPORT;
  for (unsigned i = 0; i < m->field_count; i++) {
    const mh_field *f = m->fields + i;
    if (f->report != ri)
      continue;
    int32_t value;
    if (f->array) {
      value = 0;
      for (unsigned j = 0; j < f->count; j++)
        if (extract(p, f->offset + j * f->size, f->size, f->minimum < 0) ==
            f->selector) {
          value = 1;
          break;
        }
    } else
      value = extract(p, f->offset, f->size, f->minimum < 0);
    values->value[i] = value;
    values->valid[i] = 1;
  }
  return MH_OK;
}
const char *mh_error(int code) {
  switch (code) {
  case MH_OK:
    return "OK";
  case MH_BAD_DESCRIPTOR:
    return "Malformed HID descriptor";
  case MH_LIMIT:
    return "HID layout exceeds supported limits";
  case MH_NOT_GAMEPAD:
    return "No HID joystick/gamepad inputs";
  case MH_BAD_REPORT:
    return "Unknown input report";
  case MH_SHORT_REPORT:
    return "Truncated input report";
  default:
    return "Unknown HID error";
  }
}
