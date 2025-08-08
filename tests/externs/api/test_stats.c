/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 *  All rights reserved (c) 2014-2025 CEA/DAM.
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
 * \brief test stats
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "phobos_admin.h"
#include "pho_test_utils.h"

int main(int argc, char **argv)
{
    struct admin_handle adm;
    char *test_tlc;
    char *output;
    int rc;

    test_env_initialize();
    if (phobos_admin_init(&adm, true, NULL))
        exit(EXIT_FAILURE);

    /* Try various arguments for phobos_admin_stats */
    if (phobos_admin_stats(&adm, NULL, NULL, &output))
        exit(EXIT_FAILURE);
    printf("output=%s\n", output);
    free(output);

    if (phobos_admin_stats(&adm, "", NULL, &output))
        exit(EXIT_FAILURE);
    printf("output=%s\n", output);
    free(output);

    if (phobos_admin_stats(&adm, NULL, "", &output))
        exit(EXIT_FAILURE);
    printf("output=%s\n", output);
    free(output);

    if (phobos_admin_stats(&adm, "req", "", &output))
        exit(EXIT_FAILURE);
    printf("output=%s\n", output);
    free(output);

    if (phobos_admin_stats(&adm, "req.count", "", &output))
        exit(EXIT_FAILURE);
    printf("output=%s\n", output);
    free(output);

    if (phobos_admin_stats(&adm, "req.count", "request=read", &output))
        exit(EXIT_FAILURE);
    printf("output=%s\n", output);
    free(output);

    test_tlc = getenv("TEST_TLC_STATS");
    if (test_tlc != NULL && strcmp(test_tlc, "1") == 0) {
        if (phobos_admin_stats_tlc("legacy", NULL, NULL, &output))
            exit(EXIT_FAILURE);
        printf("output=%s\n", output);
        free(output);
    }

    phobos_admin_fini(&adm);

    return EXIT_SUCCESS;
}
