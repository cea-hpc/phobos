#!/usr/bin/env bash
# -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=4:tabstop=4:

#
#  All rights reserved (c) 2014-2026 CEA/DAM.
#
#  This file is part of Phobos.
#
#  Phobos is free software: you can redistribute it and/or modify it under
#  the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation, either version 2.1 of the License, or
#  (at your option) any later version.
#
#  Phobos is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with Phobos. If not, see <http://www.gnu.org/licenses/>.
#

# Mimic an LTFS command printing a VTL cartridge's non-UTF-8 tape attribute
# (dirty cartridge memory bytes), followed by the capacity line parsed by
# ltfs_format_filter(). Arguments (device, label or mount point) are ignored.

printf 'LTFS17227I Tape attribute: Medium Label = P000s\215\037\352\177.\n' >&2
printf 'LTFS14140I Volume capacity is 3000 GB\n' >&2

exit 0
