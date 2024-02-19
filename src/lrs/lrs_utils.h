/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2022 CEA/DAM.
 *
 *  This file is part of Phobos.
 *
 *  Phobos is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  Phobos is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * \brief  LRS utility functions and data structures
 */
#ifndef _PHO_LRS_UTILS_H
#define _PHO_LRS_UTILS_H

#include <stddef.h>
#include <pho_srl_common.h>

struct req_container;

/* Contains every information needed for any component of the LRS to take and
 * update locks
 */
struct lock_handle {
    struct dss_handle *dss;           /**< Reference to the DSS handle of this
                                        *  LRS.
                                        */
    const char        *lock_hostname; /**< Lock hostname for this lrs_sched */
    int                lock_owner;    /**< Lock owner(pid) for this lrs_sched */
};

int lock_handle_init(struct lock_handle *lock_handle, struct dss_handle *dss);

struct media_info **reqc_get_medium_to_alloc(struct req_container *reqc,
                                             size_t index);

struct lrs_dev *search_in_use_medium(GPtrArray *devices,
                                     const char *name,
                                     bool *sched_ready);

struct lrs_dev *search_loaded_medium(GPtrArray *devices,
                                     const char *name);

struct lrs_dev *search_loaded_medium_keep_lock(GPtrArray *devices,
                                               const char *name);

void reqc_pho_id_from_index(struct req_container *reqc, size_t index,
                            struct pho_id *id);

/**
 * This structure holds a reference to req_container::req::ralloc::med_ids.
 * The list is ordered as follow:
 *
 * +-----------+------+-------------+-------+
 * | Allocated | Free | Unavailable | Error |
 * +-----------+------+-------------+-------+
 *
 * - The size of the list is rml_size
 * - The size of the Error section is rml_errors
 * - The size of the Allocated sections is rml_allocated
 * - The size of the Free sections is (rml_available - rml_allocated)
 * - The size of the Unavailable section is:
 *   rml_size - (rml_errors + rml_available + rml_allocated)
 *
 * The allocated section contains medium IDs that are already allocated to a
 * device for this request.
 *
 * The free section contains medium IDs that are free to be allocated to a new
 * device.
 *
 * The unavailable section contains medium IDs that are temporarily unavailable
 * (e.g. ongoing I/O, lock...). They cannot be used for the current allocation
 * but can be retried later if necessary.
 *
 * The error section contains medium IDs that cannot be used (e.g. failed, admin
 * lock...).
 *
 * During the first allocation of a request, media are selected from the free
 * section. If they can be sent to a device thread, they are put at the
 * beginning of the list (at the end of the allocated section). If they cannot
 * be allocated at all (e.g. administratively locked), they are put in the error
 * section. If they cannot be allocated right now but may be allocated later
 * (e.g. loaded on a busy drive), they are put in the unavailable section.
 *
 * If a device thread encounters an error while loading its allocated medium,
 * the request is pushed back into the scheduler thread. The unavailable section
 * is emptied with rml_reset. In this case, the medium at the index associated
 * with the sub request of the device may be failed. If so, the medium is put
 * into the error section, otherwise it can be reused for an allocation.
 *
 * In the case it is failed, the allocated section will contain one failed
 * medium. This medium will be swapped to the error section by the scheduler
 * thread when it manages the error.
 */
struct read_media_list {
    /** list of media to choose from (points to ralloc->med_ids) */
    pho_rsc_id_t **rml_media;
    /** size of rml_media */
    size_t rml_size;
    /** reference to the req_container of this request */
    struct req_container *rml_reqc;

    /** number of media currently available for allocation that the scheduler
     *  can choose from. Temporarily unavailable and failed media are not
     *  counted.
     */
    size_t rml_available;
    /** number of media currently allocated for the request */
    size_t rml_allocated;
    /** number of media that encountered an error during allocation */
    size_t rml_errors;
    /** set to true on the first call to rml_reset */
    bool rml_reset_done;
};

enum read_medium_allocation_status {
    /** The medium was allocated successfully */
    RMAS_OK,
    /** The medium is temporarily unavailable */
    RMAS_UNAVAILABLE,
    /** An error occurred during allocation */
    RMAS_ERROR,
};

/** Initialize \p list for request \p reqc */
void rml_init(struct read_media_list *list, struct req_container *reqc);

/**
 * Update the status of the medium at \p index by moving it to the right place.
 * /!\ this function modifies the order of list->rml_media.
 *
 * \return the number of available media after the update (allocated + free)
 */
size_t rml_medium_update(struct read_media_list *list, size_t index,
                         enum read_medium_allocation_status status);

/**
 * Swap the newly allocated medium at \p free_index with the failed medium at
 * \p failed_index. The medium at \p failed_index is moved in the failed section
 * of the list.
 */
void rml_medium_realloc_failed(struct read_media_list *list,
                               size_t free_index,
                               size_t failed_index);

/**
 * Swap the newly allocated medium at \p free_index with the already allocated
 * medium at \p allocated_index.
 */
void rml_medium_realloc(struct read_media_list *list,
                        size_t free_index,
                        size_t allocated_index);

/**
 * Return the number of media that can still be used for an allocation including
 * temporarily unavailable ones.
 */
size_t rml_nb_usable_media(struct read_media_list *list);

/**
 * Reset the state of temporarily unavailable media
 */
void rml_reset(struct read_media_list *list);

/**
 * Reset the state of the list when the request is requeued for later processing
 * by the scheduler
 */
void rml_requeue(struct read_media_list *list);

/**
 * Convert negative return code \p rc to read_medium_allocation_status
 *       0 -> RMAS_OK
 * -EAGAIN -> RMAS_UNAVAILABLE
 *    rest -> RMAS_ERROR
 */
enum read_medium_allocation_status rml_errno2status(int rc);

void rml_display(struct read_media_list *list);

#endif
