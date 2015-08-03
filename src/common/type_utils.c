/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Handling of layout and extent structures.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pho_type_utils.h"
#include "pho_common.h"
#include <errno.h>
#include <jansson.h>
#include <assert.h>

/* tags:
 * - single contiguous part: no tag
 * - list of contiguous parts:
 *      p<k> (eg. p2 = part 2). Need to store at least
             the total nbr of parts in tape MD.
 * - stripes:
 *      s<k> (eg. s2 = stripe 2). Need to store stripe count and stripe size
        in tape MD.
 * - raid:
 *      r<k> (eg. r2 = raid element 2)  need to store raid info in tape MD.
 * - mirrors: need tags to identify multiple copies?
*/

int layout2tag(const struct layout_descr *layout,
               unsigned int layout_idx, char *tag)
{
    switch (layout->type) {
    case PHO_LYT_SIMPLE:
        tag[0] = '\0'; /* no tag */
        return 0;
    default:
        /* invalid / not implemented */
        return -EINVAL;
    }
}

/** helper to get a string from a json document and log errors
 *  @return string on success, NULL if the parsing fails.
 */
static const char *get_json_string(json_t *json, const char *name)
{
    json_t *val;

    val = json_object_get(json, name);
    if (val == NULL) {
        pho_error(EINVAL, "key '%s' not found in JSON object", name);
        return NULL;
    }
    return json_string_value(val);
}

/** callback function prototype for decoding a value from json document */
typedef int (*val_cb_t)(const char *value, void *dest, void *arg);

/** this callback duplicates a const string returned by json_unpack . */
static int string_cb(const char *value, void *dest, void *arg)
{
    char **target = dest;

    *target = strdup(value);
    return (*target == NULL) ? -ENOMEM : 0;
}

/** this callback check and set family from a string */
static int family_cb(const char *value, void *dest, void *arg)
{
    enum dev_family *val = dest;

    *val = str2dev_family(value);
    if (*val == PHO_DEV_INVAL) {
        pho_error(EINVAL, "Invalid family value: '%s'", value);
        return -EINVAL;
    }
    return 0;
}

/** this callback check and set a dev_opt_status from a string. */
static int op_status_cb(const char *value, void *dest, void *arg)
{
    enum dev_op_status *val = dest;

    *val = str2op_status(value);
    if (*val == PHO_DEV_OP_ST_INVAL) {
        pho_error(EINVAL, "Invalid op_status value: '%s'", value);
        return -EINVAL;
    }
    return 0;
}

/** this callback check and set media identifier (type and label).
 * @param arg pointer to device family */
static int media_cb(const char *value, void *dest, void *arg)
{
    struct media_id *mid = dest;
    enum dev_family *fam = arg;

    assert(fam != NULL);

    mid->type = *fam;
    return media_id_set(mid, value);
}


/** parsing item definition */
struct parse_item {
    const char *name;
    val_cb_t    val_cb;
    void       *ptr;
};

/**
 * Call a parsing callback of each item of
 * a json document.
 */
static int items_foreach(json_t *json, const struct parse_item *items,
                         void *arg)
{
    const struct parse_item *item;
    const char              *tmp_str;
    int                     rc;

    for (item = items; item->name != NULL; item++) {
        tmp_str = get_json_string(json, item->name);
        if (tmp_str == NULL)
            return -EINVAL;

        rc = item->val_cb(tmp_str, item->ptr, arg);
        if (rc)
            return rc;
    }
    return 0;
}

int device_state_from_json(const char *str, struct dev_state *dev_st)
{
    int          rc = 0;
    json_error_t       err;
    json_t            *json = json_loads(str, JSON_REJECT_DUPLICATES, &err);

    /** those values are always expected in the json */
    struct parse_item  base_items[] = {
        {"family",  family_cb,      &dev_st->family},
        {"model",   string_cb,      &dev_st->model},
        {"serial",  string_cb,      &dev_st->serial},
        {"status",  op_status_cb,   &dev_st->op_status},
        {NULL, NULL}
    };

    /** those values are set when a media is loaded in the device */
    struct parse_item  media_items[] = {
        {"media",    media_cb,      &dev_st->media_id},
        {NULL, NULL}
    };

    /** those values are only set when a filesytem is mounted */
    struct parse_item  fs_items[] = {
        {"mnt_path", string_cb,     &dev_st->mnt_path},
        {NULL, NULL}
    };


    if (json == NULL) {
        pho_error(EINVAL, "Failed to parse JSON string: %s",
                  err.text);
        pho_debug("Parsing error line %d, byte %d in <%s>", err.line,
                  err.position, str);
        return -EINVAL;
    }

    /* parse values that are always in json */
    rc = items_foreach(json, base_items, NULL);
    if (rc)
        GOTO(out, rc);

    /* only if there is a media in the device */
    if (dev_st->op_status != PHO_DEV_OP_ST_EMPTY
        && dev_st->op_status != PHO_DEV_OP_ST_FAILED) {
        rc = items_foreach(json, media_items, &dev_st->family);
        if (rc)
            GOTO(out, rc);
    }

    /* only if the filesystem is mounted */
    if (dev_st->op_status == PHO_DEV_OP_ST_MOUNTED) {
        rc = items_foreach(json, fs_items, &dev_st->family);
        if (rc)
            GOTO(out, rc);
    }

out:
    json_decref(json);
    return rc;
}

static inline char *strdup_safe(const char *str)
{
    if (str == NULL)
        return NULL;

    return strdup(str);
}

struct dev_info *dev_info_dup(const struct dev_info *dev)
{
    struct dev_info *dev_out;

    dev_out = malloc(sizeof(*dev_out));
    if (!dev_out)
        return NULL;

    dev_out->family = dev->family;
    dev_out->model = strdup_safe(dev->model);
    dev_out->path = strdup_safe(dev->path);
    dev_out->host = strdup_safe(dev->host);
    dev_out->serial = strdup_safe(dev->serial);
    dev_out->changer_idx = dev->changer_idx;
    dev_out->adm_status = dev->adm_status;

    return dev_out;
}

void dev_info_free(struct dev_info *dev)
{
    if (!dev)
        return;
    free(dev->model);
    free(dev->path);
    free(dev->host);
    free(dev->serial);
    free(dev);
}

struct media_info *media_info_dup(const struct media_info *media)
{
    struct media_info *media_out;

    media_out = malloc(sizeof(*media_out));
    if (!media_out)
        return NULL;

    media_out->id = media->id;
    media_out->fs_type = media->fs_type;
    media_out->addr_type = media->addr_type;
    media_out->model = strdup_safe(media->model);
    media_out->adm_status = media->adm_status;
    media_out->fs_status = media->fs_status;
    media_out->stats = media->stats;

    return media_out;
}

void media_info_free(struct media_info *mda)
{
    if (!mda)
        return;
    free(mda->model);
    free(mda);
}
