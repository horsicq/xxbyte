/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"

#if XXBYTE_USE_AVM1

#include "xxbyte_avm1_internal.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

/*
 * SWF container walking (SWF19 chapters 2 and 3; Ruffle swf/src/read.rs for
 * the cases the specification leaves open).
 */

#define SWF_MAX_DATA ((size_t)512u * 1024u * 1024u)
#define SWF_MAX_SPRITE_DEPTH 16

#define TAG_END 0u
#define TAG_SHOW_FRAME 1u
#define TAG_DEFINE_BUTTON 7u
#define TAG_DO_ACTION 12u
#define TAG_PLACE_OBJECT2 26u
#define TAG_DEFINE_BUTTON2 34u
#define TAG_DEFINE_SPRITE 39u
#define TAG_DO_INIT_ACTION 59u
#define TAG_PLACE_OBJECT3 70u
#define TAG_PLACE_OBJECT4 94u

/* PlaceObject flags (UI8 for PlaceObject2, UI16 from PlaceObject3). */
#define PLACE_HAS_CHARACTER 0x0002u
#define PLACE_HAS_MATRIX 0x0004u
#define PLACE_HAS_COLOR_TRANSFORM 0x0008u
#define PLACE_HAS_RATIO 0x0010u
#define PLACE_HAS_NAME 0x0020u
#define PLACE_HAS_CLIP_DEPTH 0x0040u
#define PLACE_HAS_CLIP_ACTIONS 0x0080u
#define PLACE_HAS_FILTER_LIST 0x0100u
#define PLACE_HAS_BLEND_MODE 0x0200u
#define PLACE_HAS_CACHE_AS_BITMAP 0x0400u
#define PLACE_HAS_CLASS_NAME 0x0800u
#define PLACE_HAS_IMAGE 0x1000u
#define PLACE_HAS_VISIBLE 0x2000u
#define PLACE_OPAQUE_BACKGROUND 0x4000u

#define CLIP_EVENT_KEY_PRESS (1u << 17)
#define CLIP_EVENT_KNOWN 0x0007FFFFu

/* ---- byte and bit readers ---------------------------------------------- */

typedef struct swf_reader {
    const uint8_t *data;
    size_t pos;
    size_t end;
    /* bit reader state */
    uint32_t bit_buffer;
    unsigned bit_count;
} swf_reader;

static void sr_init(swf_reader *r, const uint8_t *data, size_t pos, size_t end)
{
    r->data = data;
    r->pos = pos;
    r->end = end;
    r->bit_buffer = 0;
    r->bit_count = 0;
}

static int sr_skip(swf_reader *r, size_t count)
{
    if (count > r->end - r->pos) {
        return 0;
    }
    r->pos += count;
    return 1;
}

static int sr_u8(swf_reader *r, uint32_t *value)
{
    if (r->end - r->pos < 1) {
        return 0;
    }
    *value = r->data[r->pos++];
    return 1;
}

static int sr_u16(swf_reader *r, uint32_t *value)
{
    if (r->end - r->pos < 2) {
        return 0;
    }
    *value = xxbyte_u16le(r->data + r->pos);
    r->pos += 2;
    return 1;
}

static int sr_u32(swf_reader *r, uint32_t *value)
{
    if (r->end - r->pos < 4) {
        return 0;
    }
    *value = xxbyte_u32le(r->data + r->pos);
    r->pos += 4;
    return 1;
}

static int sr_str(swf_reader *r, size_t *offset, size_t *length)
{
    const uint8_t *start = r->data + r->pos;
    const uint8_t *nul = (const uint8_t *)memchr(start, 0, r->end - r->pos);

    if (nul == NULL) {
        return 0;
    }
    *offset = r->pos;
    *length = (size_t)(nul - start);
    r->pos += *length + 1u;
    return 1;
}

/* Reads count (0-31) bits, most significant first. */
static int sr_bits(swf_reader *r, unsigned count, uint32_t *value)
{
    uint32_t result = 0;

    while (count-- != 0) {
        if (r->bit_count == 0) {
            if (r->pos >= r->end) {
                return 0;
            }
            r->bit_buffer = r->data[r->pos++];
            r->bit_count = 8;
        }
        --r->bit_count;
        result = (result << 1) | ((r->bit_buffer >> r->bit_count) & 1u);
    }
    *value = result;
    return 1;
}

/* Ends a bit field: the next field starts at the next byte. */
static void sr_align(swf_reader *r)
{
    r->bit_count = 0;
}

/* RECT, MATRIX and CXFORM(WITHALPHA) (SWF19 "Basic data types"). */
static int sr_rect(swf_reader *r)
{
    uint32_t bits;
    uint32_t value;

    if (!sr_bits(r, 5, &bits) || !sr_bits(r, bits, &value) || !sr_bits(r, bits, &value) || !sr_bits(r, bits, &value) || !sr_bits(r, bits, &value)) {
        return 0;
    }
    sr_align(r);
    return 1;
}

static int sr_matrix(swf_reader *r)
{
    uint32_t flag;
    uint32_t bits;
    uint32_t value;
    int i;

    for (i = 0; i < 2; ++i) { /* scale, then rotate/skew */
        if (!sr_bits(r, 1, &flag)) {
            return 0;
        }
        if (flag != 0 && (!sr_bits(r, 5, &bits) || !sr_bits(r, bits, &value) || !sr_bits(r, bits, &value))) {
            return 0;
        }
    }
    if (!sr_bits(r, 5, &bits) || !sr_bits(r, bits, &value) || !sr_bits(r, bits, &value)) {
        return 0;
    }
    sr_align(r);
    return 1;
}

static int sr_cxform(swf_reader *r, int alpha)
{
    uint32_t has_add;
    uint32_t has_mult;
    uint32_t bits;
    uint32_t value;
    unsigned count;
    unsigned i;

    if (!sr_bits(r, 1, &has_add) || !sr_bits(r, 1, &has_mult) || !sr_bits(r, 4, &bits)) {
        return 0;
    }
    count = (alpha ? 4u : 3u) * ((has_add != 0 ? 1u : 0u) + (has_mult != 0 ? 1u : 0u));
    for (i = 0; i < count; ++i) {
        if (!sr_bits(r, bits, &value)) {
            return 0;
        }
    }
    sr_align(r);
    return 1;
}

/* A FILTERLIST (count, then the filters), as Ruffle reads it. */
static int sr_filters(swf_reader *r)
{
    uint32_t count;
    uint32_t type;
    uint32_t n;
    uint32_t columns;
    uint32_t rows;

    if (!sr_u8(r, &count)) {
        return 0;
    }
    while (count-- != 0) {
        if (!sr_u8(r, &type)) {
            return 0;
        }
        switch (type) {
            case 0: /* DropShadow: RGBA, 4 FIXED, FIXED8, flags */
                if (!sr_skip(r, 23)) {
                    return 0;
                }
                break;
            case 1: /* Blur: 2 FIXED, flags */
                if (!sr_skip(r, 9)) {
                    return 0;
                }
                break;
            case 2: /* Glow: RGBA, 2 FIXED, FIXED8, flags */
                if (!sr_skip(r, 15)) {
                    return 0;
                }
                break;
            case 3: /* Bevel: 2 RGBA, 4 FIXED, FIXED8, flags */
                if (!sr_skip(r, 27)) {
                    return 0;
                }
                break;
            case 4: /* GradientGlow */
            case 7: /* GradientBevel: count, colors, ratios, 4 FIXED, FIXED8, flags */
                if (!sr_u8(r, &n) || !sr_skip(r, (size_t)n * 5u + 19u)) {
                    return 0;
                }
                break;
            case 5: /* Convolution: columns, rows, divisor, bias, matrix, RGBA, flags */
                if (!sr_u8(r, &columns) || !sr_u8(r, &rows) || !sr_skip(r, 8u + (size_t)columns * rows * 4u + 5u)) {
                    return 0;
                }
                break;
            case 6: /* ColorMatrix: 20 FLOAT */
                if (!sr_skip(r, 80)) {
                    return 0;
                }
                break;
            default: return 0;
        }
    }
    return 1;
}

static int sr_blend_mode(swf_reader *r)
{
    uint32_t mode;

    return sr_u8(r, &mode) && mode <= 14u;
}

/* ---- header ------------------------------------------------------------ */

void xxbyte_avm1_swf_close(xxbyte_avm1_swf *swf)
{
    if (swf != NULL) {
        xx_mem_free(swf->owned);
        memset(swf, 0, sizeof(*swf));
    }
}

xxbyte_status xxbyte_avm1_swf_open(const uint8_t *data, size_t size, xxbyte_avm1_swf *swf)
{
    xxbyte_avm1_swf parsed;
    swf_reader r;
    uint32_t value;

    if (swf == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(swf, 0, sizeof(*swf));
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < 3) {
        return size == 0 || memcmp("FWS", data, size) == 0 || memcmp("CWS", data, size) == 0 || memcmp("ZWS", data, size) == 0 ? XXBYTE_STATUS_TRUNCATED
                                                                                                                               : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if ((data[0] != 'F' && data[0] != 'C' && data[0] != 'Z') || data[1] != 'W' || data[2] != 'S') {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size < 8) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(&parsed, 0, sizeof(parsed));
    parsed.signature = data[0];
    parsed.version = data[3];
    parsed.file_length = xxbyte_u32le(data + 4);
    if (parsed.signature == 'Z') {
        swf->signature = parsed.signature;
        swf->version = parsed.version;
        swf->file_length = parsed.file_length;
        return XXBYTE_STATUS_UNSUPPORTED_FORMAT;
    }
    if (parsed.version == 0 || parsed.file_length < 8) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (parsed.signature == 'F') {
        parsed.data = data;
        parsed.size = size < parsed.file_length ? size : parsed.file_length;
    } else {
        size_t in_size = size - 8;
        size_t capacity = (size_t)parsed.file_length - 8u;
        size_t out_size = 0;
        xxbyte_status status;

        /* DEFLATE expands at most 1032:1. */
        if (in_size < SWF_MAX_DATA / 1032u && capacity > in_size * 1032u + 64u) {
            capacity = in_size * 1032u + 64u;
        }
        if (capacity > SWF_MAX_DATA) {
            capacity = SWF_MAX_DATA;
        }
        parsed.owned = (uint8_t *)xx_mem_alloc(capacity + 8u);
        if (parsed.owned == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        memcpy(parsed.owned, data, 8);
        status = xxbyte_inflate(data + 8, in_size, 1, parsed.owned + 8, capacity, &out_size, NULL);
        if (status != XXBYTE_STATUS_OK && out_size == 0) {
            xx_mem_free(parsed.owned);
            return status == XXBYTE_STATUS_OUT_OF_MEMORY ? XXBYTE_STATUS_TRUNCATED : status;
        }
        parsed.data = parsed.owned;
        parsed.size = out_size + 8u;
    }
    sr_init(&r, parsed.data, 8, parsed.size);
    if (!sr_rect(&r) || !sr_u16(&r, &value)) {
        xx_mem_free(parsed.owned);
        return XXBYTE_STATUS_TRUNCATED;
    }
    parsed.frame_rate = (uint16_t)value;
    if (!sr_u16(&r, &value)) {
        xx_mem_free(parsed.owned);
        return XXBYTE_STATUS_TRUNCATED;
    }
    parsed.frame_count = (uint16_t)value;
    parsed.tags_offset = (uint32_t)r.pos;
    *swf = parsed;
    return XXBYTE_STATUS_OK;
}

/* ---- tag walk ---------------------------------------------------------- */

typedef struct swf_walker {
    const xxbyte_avm1_swf *swf;
    xxbyte_avm1_block_callback callback;
    void *context;
    xxbyte_status stop; /* the callback's status once it stops the walk */
} swf_walker;

static int emit(swf_walker *w, const xxbyte_avm1_block *block)
{
    xxbyte_status status = w->callback(w->context, w->swf, block);

    if (status != XXBYTE_STATUS_OK) {
        w->stop = status;
        return 0;
    }
    return 1;
}

static int walk_button1(swf_walker *w, xxbyte_avm1_block *block, size_t body, size_t end)
{
    swf_reader r;
    uint32_t value;
    uint32_t flags;

    sr_init(&r, w->swf->data, body, end);
    if (!sr_u16(&r, &block->id)) {
        return 1;
    }
    for (;;) { /* BUTTONRECORDs up to the zero flags byte */
        if (!sr_u8(&r, &flags)) {
            return 1;
        }
        if (flags == 0) {
            break;
        }
        if (!sr_u16(&r, &value) || !sr_u16(&r, &value) || !sr_matrix(&r)) {
            return 1;
        }
        /* Ruffle reads these for DefineButton too. */
        if ((flags & 0x10u) != 0 && !sr_filters(&r)) {
            return 1;
        }
        if ((flags & 0x20u) != 0 && !sr_blend_mode(&r)) {
            return 1;
        }
    }
    block->conditions = 0x0008u; /* over_down_to_over_up (release) */
    block->offset = (uint32_t)r.pos;
    block->size = (uint32_t)(end - r.pos);
    return emit(w, block);
}

static int walk_button2(swf_walker *w, xxbyte_avm1_block *block, size_t body, size_t end)
{
    swf_reader r;
    uint32_t value;
    uint32_t action_offset;

    sr_init(&r, w->swf->data, body, end);
    if (!sr_u16(&r, &block->id) || !sr_u8(&r, &value) || !sr_u16(&r, &action_offset) || action_offset == 0) {
        return 1;
    }
    r.pos = body + 3u; /* ActionOffset counts from its own first byte */
    if (!sr_skip(&r, action_offset)) {
        return 1;
    }
    for (;;) {
        uint32_t record_size;
        size_t start;
        if (!sr_u16(&r, &record_size) || !sr_u16(&r, &block->conditions)) {
            return 1;
        }
        start = r.pos;
        if (record_size == 0) {
            block->offset = (uint32_t)start;
            block->size = (uint32_t)(end - start);
            return emit(w, block);
        }
        if (record_size < 4 || !sr_skip(&r, record_size - 4u)) {
            return 1;
        }
        block->offset = (uint32_t)start;
        block->size = record_size - 4u;
        if (!emit(w, block)) {
            return 0;
        }
    }
}

/* Clip event flags: UI16 before SWF 6, UI32 from SWF 6; a field cut short
 * reads as 0 (Ruffle). */
static uint32_t read_events(swf_reader *r, uint8_t version)
{
    uint32_t value = 0;

    if (version >= 6) {
        if (!sr_u32(r, &value)) {
            value = 0;
        }
    } else if (!sr_u16(r, &value)) {
        value = 0;
    }
    return value;
}

static int walk_place(swf_walker *w, xxbyte_avm1_block *block, size_t body, size_t end)
{
    swf_reader r;
    uint32_t flags;
    uint32_t value;
    size_t offset;
    size_t length;

    sr_init(&r, w->swf->data, body, end);
    if (block->tag_code == TAG_PLACE_OBJECT2 ? !sr_u8(&r, &flags) : !sr_u16(&r, &flags)) {
        return 1;
    }
    if (!sr_u16(&r, &block->id)) {
        return 1;
    }
    if ((flags & PLACE_HAS_CLIP_ACTIONS) == 0) {
        return 1;
    }
    if ((flags & PLACE_HAS_CLASS_NAME) != 0 || ((flags & PLACE_HAS_IMAGE) != 0 && (flags & PLACE_HAS_CHARACTER) == 0)) {
        if (!sr_str(&r, &offset, &length)) {
            return 1;
        }
    }
    if ((flags & PLACE_HAS_CHARACTER) != 0) {
        if (!sr_u16(&r, &block->character)) {
            return 1;
        }
        block->has_character = 1;
    }
    if ((flags & PLACE_HAS_MATRIX) != 0 && !sr_matrix(&r)) {
        return 1;
    }
    if ((flags & PLACE_HAS_COLOR_TRANSFORM) != 0 && !sr_cxform(&r, 1)) {
        return 1;
    }
    if ((flags & PLACE_HAS_RATIO) != 0 && !sr_u16(&r, &value)) {
        return 1;
    }
    if ((flags & PLACE_HAS_NAME) != 0) {
        if (!sr_str(&r, &offset, &length)) {
            return 1;
        }
        block->name_offset = (uint32_t)offset;
        block->name_length = (uint32_t)length;
    }
    if ((flags & PLACE_HAS_CLIP_DEPTH) != 0 && !sr_u16(&r, &value)) {
        return 1;
    }
    if ((flags & PLACE_HAS_FILTER_LIST) != 0 && !sr_filters(&r)) {
        return 1;
    }
    if ((flags & PLACE_HAS_BLEND_MODE) != 0 && !sr_blend_mode(&r)) {
        return 1;
    }
    if ((flags & PLACE_HAS_CACHE_AS_BITMAP) != 0 && r.pos < r.end) {
        r.pos += 1; /* some files end the tag without it (Ruffle) */
    }
    if ((flags & PLACE_HAS_VISIBLE) != 0 && !sr_u8(&r, &value)) {
        return 1;
    }
    if ((flags & PLACE_OPAQUE_BACKGROUND) != 0 && !sr_u32(&r, &value)) {
        return 1;
    }
    /* CLIPACTIONS: reserved UI16, AllEventFlags, CLIPACTIONRECORDs. */
    if (!sr_u16(&r, &value)) {
        return 1;
    }
    read_events(&r, w->swf->version);
    for (;;) {
        uint32_t events = read_events(&r, w->swf->version);
        uint32_t size;
        if ((events & CLIP_EVENT_KNOWN) == 0 || !sr_u32(&r, &size)) {
            return 1;
        }
        block->conditions = events;
        block->key_code = 0;
        if ((events & CLIP_EVENT_KEY_PRESS) != 0) {
            /* The record size includes the key code. */
            if (size == 0 || !sr_u8(&r, &block->key_code)) {
                return 1;
            }
            --size;
        }
        if (!sr_skip(&r, size)) {
            return 1;
        }
        block->offset = (uint32_t)(r.pos - size);
        block->size = size;
        if (!emit(w, block)) {
            return 0;
        }
    }
}

/*
 * Walks the tags in [start, end). Returns XXBYTE_STATUS_OK at an End tag or
 * at end, XXBYTE_STATUS_TRUNCATED when a tag runs past end, and the
 * callback's status when it stops the walk.
 */
static xxbyte_status walk_tags(swf_walker *w, size_t start, size_t end, uint32_t in_sprite, uint32_t sprite, int depth)
{
    const uint8_t *data = w->swf->data;
    size_t pos = start;
    uint32_t frame = 1;

    while (pos < end) {
        xxbyte_avm1_block block;
        uint32_t header;
        uint32_t code;
        size_t length;
        size_t body;
        int go_on = 1;

        if (end - pos < 2) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        header = xxbyte_u16le(data + pos);
        code = header >> 6;
        length = header & 0x3Fu;
        body = pos + 2u;
        if (length == 0x3Fu) {
            if (end - body < 4) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            length = xxbyte_u32le(data + body);
            body += 4u;
        }
        if (length > end - body) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (code == TAG_END) {
            return XXBYTE_STATUS_OK;
        }
        memset(&block, 0, sizeof(block));
        block.tag_code = code;
        block.tag_offset = (uint32_t)pos;
        block.frame = frame;
        block.in_sprite = in_sprite;
        block.sprite = sprite;
        switch (code) {
            case TAG_SHOW_FRAME: ++frame; break;
            case TAG_DO_ACTION:
                block.kind = XXBYTE_AVM1_BLOCK_DO_ACTION;
                block.offset = (uint32_t)body;
                block.size = (uint32_t)length;
                go_on = emit(w, &block);
                break;
            case TAG_DO_INIT_ACTION:
                if (length >= 2) {
                    block.kind = XXBYTE_AVM1_BLOCK_DO_INIT_ACTION;
                    block.id = xxbyte_u16le(data + body);
                    block.offset = (uint32_t)(body + 2u);
                    block.size = (uint32_t)(length - 2u);
                    go_on = emit(w, &block);
                }
                break;
            case TAG_DEFINE_BUTTON:
                block.kind = XXBYTE_AVM1_BLOCK_BUTTON;
                go_on = walk_button1(w, &block, body, body + length);
                break;
            case TAG_DEFINE_BUTTON2:
                block.kind = XXBYTE_AVM1_BLOCK_BUTTON2;
                go_on = walk_button2(w, &block, body, body + length);
                break;
            case TAG_PLACE_OBJECT2:
            case TAG_PLACE_OBJECT3:
            case TAG_PLACE_OBJECT4:
                block.kind = XXBYTE_AVM1_BLOCK_CLIP;
                go_on = walk_place(w, &block, body, body + length);
                break;
            case TAG_DEFINE_SPRITE:
                if (length >= 4 && depth < SWF_MAX_SPRITE_DEPTH) {
                    /* A nested tag cut short by the sprite is a malformed
                     * sprite: keep what was read and go on. */
                    walk_tags(w, body + 4u, body + length, 1, xxbyte_u16le(data + body), depth + 1);
                    go_on = w->stop == XXBYTE_STATUS_OK;
                }
                break;
            default: break;
        }
        if (!go_on) {
            return w->stop;
        }
        pos = body + length;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm1_swf_walk(const xxbyte_avm1_swf *swf, xxbyte_avm1_block_callback callback, void *context)
{
    swf_walker w;

    if (swf == NULL || swf->data == NULL || callback == NULL || swf->tags_offset > swf->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    w.swf = swf;
    w.callback = callback;
    w.context = context;
    w.stop = XXBYTE_STATUS_OK;
    return walk_tags(&w, swf->tags_offset, swf->size, 0, 0, 0);
}

/* ---- listing ----------------------------------------------------------- */

typedef struct list_context {
    xx_buf_t *out;
    uint32_t listing_flags;
} list_context;

static const char *const button_condition_names[] = {
    "idle_to_over_up",       "over_up_to_idle",  "over_up_to_over_down", "over_down_to_over_up", "over_down_to_out_down",
    "out_down_to_over_down", "out_down_to_idle", "idle_to_over_down",    "over_down_to_idle",
};

static const char *const clip_event_names[] = {
    "load",  "enter_frame", "unload",          "mouse_move", "mouse_down", "mouse_up",  "key_down", "key_up",    "data",      "initialize",
    "press", "release",     "release_outside", "roll_over",  "roll_out",   "drag_over", "drag_out", "key_press", "construct",
};

static int append_conditions(xx_buf_t *out, const xxbyte_avm1_block *block)
{
    const char *const *names = block->kind == XXBYTE_AVM1_BLOCK_CLIP ? clip_event_names : button_condition_names;
    unsigned count = block->kind == XXBYTE_AVM1_BLOCK_CLIP ? 19u : 9u;
    uint32_t flags = block->conditions;
    unsigned bit;
    int first = 1;

    if (!xx_buf_append_str(out, " on ")) {
        return 0;
    }
    for (bit = 0; bit < count; ++bit) {
        if ((flags & (1u << bit)) == 0) {
            continue;
        }
        flags &= ~(1u << bit);
        if ((!first && !xx_buf_append_char(out, '|')) || !xx_buf_append_str(out, names[bit])) {
            return 0;
        }
        if (block->kind == XXBYTE_AVM1_BLOCK_CLIP && bit == 17u && !xx_buf_appendf(out, " %u", (unsigned)block->key_code)) {
            return 0;
        }
        first = 0;
    }
    if (block->kind != XXBYTE_AVM1_BLOCK_CLIP && (flags & 0xFE00u) != 0) {
        if ((!first && !xx_buf_append_char(out, '|')) || !xx_buf_appendf(out, "key_press %u", (unsigned)(flags >> 9))) {
            return 0;
        }
        flags = 0;
        first = 0;
    }
    if (flags != 0) {
        if ((!first && !xx_buf_append_char(out, '|')) || !xx_buf_appendf(out, "0x%08x", (unsigned)flags)) {
            return 0;
        }
        first = 0;
    }
    return !first || xx_buf_append_str(out, "none");
}

static xxbyte_status list_block(void *context, const xxbyte_avm1_swf *swf, const xxbyte_avm1_block *block)
{
    list_context *list = (list_context *)context;
    xx_buf_t *out = list->out;
    int ok;

    ok = xx_buf_append_str(out, "\n; ") && (block->in_sprite ? xx_buf_appendf(out, "sprite %u frame %u: ", (unsigned)block->sprite, (unsigned)block->frame)
                                                             : xx_buf_appendf(out, "frame %u: ", (unsigned)block->frame));
    if (ok) {
        switch (block->kind) {
            case XXBYTE_AVM1_BLOCK_DO_ACTION: ok = xx_buf_append_str(out, "DoAction"); break;
            case XXBYTE_AVM1_BLOCK_DO_INIT_ACTION: ok = xx_buf_appendf(out, "DoInitAction sprite %u", (unsigned)block->id); break;
            case XXBYTE_AVM1_BLOCK_BUTTON:
            case XXBYTE_AVM1_BLOCK_BUTTON2:
                ok = xx_buf_appendf(out, "%s %u", block->kind == XXBYTE_AVM1_BLOCK_BUTTON ? "DefineButton" : "DefineButton2", (unsigned)block->id) &&
                     append_conditions(out, block);
                break;
            default:
                ok = xx_buf_appendf(out, "PlaceObject%u depth %u",
                                    block->tag_code == TAG_PLACE_OBJECT2   ? 2u
                                    : block->tag_code == TAG_PLACE_OBJECT3 ? 3u
                                                                           : 4u,
                                    (unsigned)block->id) &&
                     (!block->has_character || xx_buf_appendf(out, " character %u", (unsigned)block->character)) &&
                     (block->name_offset == 0 ||
                      (xx_buf_append_str(out, " name ") && xxbyte_avm1_append_string(out, swf->data + block->name_offset, block->name_length))) &&
                     append_conditions(out, block);
                break;
        }
    }
    ok = ok && xx_buf_appendf(out, " (0x%08x, %u bytes)\n", (unsigned)block->offset, (unsigned)block->size);
    if (!ok) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return xxbyte_avm1_disasm_actions(swf->data + block->offset, block->size, 0, list->listing_flags, out);
}

xxbyte_status xxbyte_avm1_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_avm1_swf swf;
    list_context list;
    xxbyte_status status;

    (void)mode;
    status = xxbyte_avm1_swf_open(data, size, &swf);
    if (status == XXBYTE_STATUS_UNSUPPORTED_FORMAT) {
        return xx_buf_appendf(out, "; SWF version %u, ZWS (LZMA) is not supported\n", (unsigned)swf.version) ? status : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!xx_buf_appendf(out, "; SWF version %u, %cWS, length %u, %u frames\n", (unsigned)swf.version, swf.signature, (unsigned)swf.file_length,
                        (unsigned)swf.frame_count)) {
        xxbyte_avm1_swf_close(&swf);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    list.out = out;
    list.listing_flags = listing_flags;
    status = xxbyte_avm1_swf_walk(&swf, list_block, &list);
    xxbyte_avm1_swf_close(&swf);
    return status;
}

#else
typedef int xxbyte_avm1_swf_disabled;
#endif
