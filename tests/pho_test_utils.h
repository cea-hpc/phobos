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
 * \brief  Testsuite helpers
 */

#ifndef _PHO_TEST_UTILS_H
#define _PHO_TEST_UTILS_H

#include "pho_dss.h"
#include "pho_types.h"

struct media_info;
struct lrs_dev;

typedef int (*pho_unit_test_t)(void *);

enum pho_test_result {
    PHO_TEST_SUCCESS,
    PHO_TEST_FAILURE,
};

void pho_run_test(const char *descr, pho_unit_test_t test, void *hint,
                  enum pho_test_result xres);

void test_env_initialize(void);

void get_serial_from_path(char *path, char **serial);

void create_device(struct lrs_dev *dev, char *path, char *model,
                   struct dss_handle *dss);

void cleanup_device(struct lrs_dev *dev);

void medium_set_tags(struct media_info *medium, char **tags, size_t n_tags);

void create_medium(struct media_info *medium, const char *name);

char *get_mount_path(struct lrs_dev *dev);

#endif
