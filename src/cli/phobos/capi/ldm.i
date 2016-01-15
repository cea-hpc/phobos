/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright 2014-2015 CEA/DAM. All Rights Reserved.
 */
/**
 * \brief  Phobos LDM interface file for SWIG.
 */

%module ldm
%include "typemaps.i"

%{
#define SWIG_FILE_WITH_INIT

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pho_ldm.h>
#include <pho_types.h>
%}

%include <pho_ldm.h>
