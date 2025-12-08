#
#  All rights reserved (c) 2014-2025 CEA/DAM.
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

"""
Media target for Phobos CLI
"""

import argparse
import sys

from ClusterShell.NodeSet import NodeSet

from phobos.cli.action import ActionOptHandler
from phobos.cli.action.add import AddOptHandler
from phobos.cli.action.format import FormatOptHandler
from phobos.cli.action.list import ListOptHandler
from phobos.cli.action.lock import LockOptHandler
from phobos.cli.action.resource_delete import ResourceDeleteOptHandler
from phobos.cli.action.unlock import UnlockOptHandler
from phobos.cli.common import (BaseResourceOptHandler, env_error_format,
                               XferOptHandler)
from phobos.cli.common.args import add_list_arguments
from phobos.cli.common.exec import exec_delete_medium_device
from phobos.cli.common.utils import (check_output_attributes,
                                     handle_sort_option,
                                     parse_set_access_flags, set_library)
from phobos.core.admin import Client as AdminClient
from phobos.core.const import (ADM_STATUS, DELETE_ACCESS, DSS_MEDIA, GET_ACCESS, # pylint: disable=no-name-in-module
                               PHO_RSC_ADM_ST_LOCKED, PHO_RSC_ADM_ST_UNLOCKED,
                               PUT_ACCESS, TAGS)
from phobos.core.ffi import (MediaInfo, ResourceFamily)
from phobos.output import dump_object_list


class MediaAddOptHandler(AddOptHandler):
    """Insert a new media into the system."""
    descr = 'insert new media to the system'

    @classmethod
    def add_options(cls, parser):
        super(MediaAddOptHandler, cls).add_options(parser)
        # The type argument allows to transform 'a,b,c' into ['a', 'b', 'c'] at
        # parse time rather than post processing it
        parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                            help='tags to associate with this media (comma-'
                                 'separated: foo,bar)')


class MediaImportOptHandler(XferOptHandler):
    """Import a media into the system"""
    label = 'import'
    descr = 'import existing media'

    @classmethod
    def add_options(cls, parser):
        super(MediaImportOptHandler, cls).add_options(parser)
        parser.add_argument('media', nargs='+',
                            help="name of the media to import")
        parser.add_argument('--check-hash', action='store_true',
                            help="recalculates hashes and compares them "
                                 "with the hashes of the extent")
        parser.add_argument('--unlock', action='store_true',
                            help="unlocks the tape after the import")
        parser.add_argument('--library', help="Library containing each medium")


class MediaListOptHandler(ListOptHandler):
    """
    Specific version of the 'list' command for media, with a couple
    extra-options.
    """
    descr = "list all media"

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(MediaListOptHandler, cls).add_options(parser)
        parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                            help='filter on tags (comma-separated: foo,bar)')

        attr = list(MediaInfo().get_display_dict().keys())
        attr.sort()
        add_list_arguments(parser, attr, "name", sort_option=True,
                           lib_option=True, status_option=True)
        parser.formatter_class = argparse.RawDescriptionHelpFormatter
        parser.epilog = """About file system status `fs.status`:
    blank: medium is not formatted
    empty: medium is formatted, no data written to it
    used: medium contains data
    full: medium is full, no more data can be written to it"""


class MediaLocateOptHandler(ActionOptHandler):
    """Locate a media into the system."""
    label = 'locate'
    descr = 'locate a media'

    @classmethod
    def add_options(cls, parser):
        super(MediaLocateOptHandler, cls).add_options(parser)
        parser.add_argument('res', help='media to locate')
        parser.add_argument('--library',
                            help="Library containing the media to locate")


class MediaRenameOptHandler(ActionOptHandler):
    """Rename an existing media"""
    label = 'rename'
    descr = ('for now, change only the library of an existing medium '
             '(only the DSS is modified)')

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(MediaRenameOptHandler, cls).add_options(parser)
        parser.add_argument('--library',
                            help="Library containing the medium to rename")
        parser.add_argument('--new-library',
                            help="New library for these medium(s)")
        parser.add_argument('res', nargs='+', help="Resource(s) to rename")


class MediaSetAccessOptHandler(ActionOptHandler):
    """Set media operation flags."""
    label = 'set-access'
    descr = 'set media operation flags'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(MediaSetAccessOptHandler, cls).add_options(parser)
        parser.add_argument(
            'flags',
            type=parse_set_access_flags,
            metavar='FLAGS',
            help='[+|-]LIST, where LIST is made of capital letters among PGD,'
                 ' P: put, G: get, D: delete',
        )
        parser.add_argument('res', nargs='+', metavar='RESOURCE',
                            help='Resource(s) to update access mode')
        parser.formatter_class = argparse.RawDescriptionHelpFormatter


class MediaUpdateOptHandler(ActionOptHandler):
    """Update an existing media"""
    label = 'update'
    descr = 'update existing media properties'

    @classmethod
    def add_options(cls, parser):
        """Add resource-specific options."""
        super(MediaUpdateOptHandler, cls).add_options(parser)
        parser.add_argument('-T', '--tags', type=lambda t: t.split(','),
                            help='New tags for this media (comma-separated, '
                                 'e.g. "-T foo,bar"), empty string to clear '
                                 'tags, new tags list overwrite current tags')
        parser.add_argument('res', nargs='+', help='Resource(s) to update')


class MediaOptHandler(BaseResourceOptHandler):
    """Shared interface for medium."""
    verbs = [
        FormatOptHandler,
        LockOptHandler,
        MediaAddOptHandler,
        MediaListOptHandler,
        MediaLocateOptHandler,
        MediaRenameOptHandler,
        MediaSetAccessOptHandler,
        MediaUpdateOptHandler,
        ResourceDeleteOptHandler,
        UnlockOptHandler,
    ]
    library = None

    def add_medium(self, adm, medium, tags):
        """Add media method"""

    def add_medium_and_device(self):
        """Add a new medium and a new device at once"""
        resources = self.params.get('res')
        keep_locked = not self.params.get('unlock')
        tags = self.params.get('tags', [])
        set_library(self)
        valid_count = 0
        rc = 0

        try:
            with AdminClient(lrs_required=False) as adm:
                for path in resources:
                    medium_is_added = False

                    try:
                        medium = MediaInfo(family=self.family, name=path,
                                           model=None,
                                           is_adm_locked=keep_locked,
                                           library=self.library)

                        self.add_medium(adm, [medium], tags)
                        medium_is_added = True
                        adm.device_add(self.family, [path], False,
                                       self.library)
                        valid_count += 1
                    except EnvironmentError as err:
                        self.logger.error(env_error_format(err))
                        rc = (err.errno if not rc else rc)
                        if medium_is_added:
                            self.client.media.remove(self.family, path,
                                                     self.library)
                        continue

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        return (rc, len(resources), valid_count)

    def exec_add(self):
        """Add new media."""
        names = NodeSet.fromlist(self.params.get('res'))
        fstype = self.params.get('fs').upper()
        techno = self.params.get('type', '').upper()
        tags = self.params.get('tags', [])
        keep_locked = not self.params.get('unlock')
        set_library(self)

        media = [MediaInfo(family=self.family, name=name,
                           model=techno, is_adm_locked=keep_locked,
                           library=self.library)
                 for name in names]
        try:
            with AdminClient(lrs_required=False) as adm:
                adm.medium_add(media, fstype, tags=tags)
            self.logger.info("Added %d media successfully", len(names))
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def del_medium(self, adm, family, #pylint: disable=too-many-arguments
                   resources, library, lost):
        """Delete medium method"""

    def delete_medium_and_device(self):
        """Delete a medium and device at once"""
        resources = self.params.get('res')
        lost = self.params.get('lost')
        set_library(self)
        valid_count = 0
        rc = 0

        try:
            with AdminClient(lrs_required=False) as adm:
                for path in resources:
                    device_is_del = False
                    try:
                        rc = adm.device_delete(self.family, [path],
                                               self.library)
                        device_is_del = True
                        self.del_medium(adm, self.family, [path], self.library,
                                        lost)
                        valid_count += 1
                    except EnvironmentError as err:
                        self.logger.error(env_error_format(err))
                        rc = (err.errno if not rc else rc)
                        if device_is_del:
                            adm.device_add(self.family, [path], False,
                                           self.library)
                        continue
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

        return (rc, len(resources), valid_count)

    def _media_update(self, media, fields):
        """Calling client.media.update"""
        try:
            self.client.media.update(media, fields)
            with AdminClient(lrs_required=False) as adm:
                adm.notify_media_update(media)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_update(self):
        """Update tags of an existing media"""
        set_library(self)
        tags = self.params.get('tags')
        if tags is None:
            self.logger.info("No update to be performed")
            return

        # Empty string clears tag and ''.split(',') == ['']
        if tags == ['']:
            tags = []

        uids = NodeSet.fromlist(self.params.get('res'))

        media = [MediaInfo(family=self.family, name=uid, tags=tags,
                           library=self.library)
                 for uid in uids]
        self._media_update(media, TAGS)

    def exec_format(self):
        """Format media however requested."""
        media_list = NodeSet.fromlist(self.params.get('res'))
        nb_streams = self.params.get('nb_streams')
        fs_type = self.params.get('fs')
        unlock = self.params.get('unlock')
        set_library(self)
        if self.family == ResourceFamily.RSC_TAPE:
            force = self.params.get('force')
        else:
            force = False

        try:
            with AdminClient(lrs_required=True) as adm:
                if unlock:
                    self.logger.debug("Post-unlock enabled")

                self.logger.info("Formatting media '%s'", media_list)
                if force:
                    self.logger.warning(
                        "This format may imply some orphan extents or lost "
                        "objects. Hoping you know what you are doing!")

                adm.fs_format(media_list, self.family, self.library,
                              nb_streams, fs_type, unlock=unlock, force=force)

            self.logger.info("Media '%s' have been formatted", media_list)

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_list(self):
        """List media and display results."""
        attrs = list(MediaInfo().get_display_dict().keys())
        check_output_attributes(attrs, self.params.get('output'), self.logger)

        kwargs = {}
        if self.params.get('tags'):
            kwargs["tags"] = self.params.get('tags')

        if self.params.get('library'):
            kwargs["library"] = self.params.get('library')

        if self.params.get('status'):
            kwargs["adm_status"] = self.params.get('status')

        kwargs = handle_sort_option(self.params, MediaInfo(), self.logger,
                                    **kwargs)

        objs = []
        if self.params.get('res'):
            uids = NodeSet.fromlist(self.params.get('res'))
            for uid in uids:
                curr = self.client.media.get(family=self.family, id=uid,
                                             **kwargs)
                if not curr:
                    continue
                assert len(curr) == 1
                objs.append(curr[0])
        else:
            objs = list(self.client.media.get(family=self.family, **kwargs))

        if len(objs) > 0:
            dump_object_list(objs, attr=self.params.get('output'),
                             fmt=self.params.get('format'))

    def _set_adm_status(self, adm_status):
        """Update media.adm_status"""
        uids = NodeSet.fromlist(self.params.get('res'))
        media = [MediaInfo(family=self.family, name=uid, adm_status=adm_status,
                           library=self.library)
                 for uid in uids]
        self._media_update(media, ADM_STATUS)

    def exec_lock(self):
        """Lock media"""
        set_library(self)
        self._set_adm_status(PHO_RSC_ADM_ST_LOCKED)

    def exec_unlock(self):
        """Unlock media"""
        set_library(self)
        self._set_adm_status(PHO_RSC_ADM_ST_UNLOCKED)

    def exec_set_access(self):
        """Update media operations flags"""
        set_library(self)
        fields = 0
        flags = self.params.get('flags')
        if 'put' in flags:
            put_access = flags['put']
            fields += PUT_ACCESS
        else:
            put_access = True   # unused value

        if 'get' in flags:
            get_access = flags['get']
            fields += GET_ACCESS
        else:
            get_access = True   # unused value

        if 'delete' in flags:
            delete_access = flags['delete']
            fields += DELETE_ACCESS
        else:
            delete_access = True   # unused value

        uids = NodeSet.fromlist(self.params.get('res'))
        media = [MediaInfo(family=self.family, name=uid,
                           put_access=put_access, get_access=get_access,
                           delete_access=delete_access, library=self.library)
                 for uid in uids]
        self._media_update(media, fields)

    def exec_locate(self):
        """Locate a medium"""
        set_library(self)
        try:
            with AdminClient(lrs_required=False) as adm:
                print(adm.medium_locate(self.family, self.params.get('res'),
                                        self.library))

        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_import(self):
        """Import a medium"""
        media_names = NodeSet.fromlist(self.params.get('media'))
        check_hash = self.params.get('check_hash')
        adm_locked = not self.params.get('unlock')
        techno = self.params.get('type', '').upper()
        fstype = self.params.get('fs').upper()
        set_library(self)

        media = (MediaInfo * len(media_names))()
        for index, medium_name in enumerate(media_names):
            media[index] = MediaInfo(family=self.family, name=medium_name,
                                     model=techno, is_adm_locked=adm_locked,
                                     library=self.library)
        try:
            with AdminClient(lrs_required=True) as adm:
                adm.medium_import(fstype, media, check_hash)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_repack(self):
        """Repack a medium"""
        set_library(self)
        try:
            with AdminClient(lrs_required=True) as adm:
                tags = self.params.get('tags', [])
                adm.repack(self.family, self.params.get('res'), self.library,
                           tags)
        except EnvironmentError as err:
            self.logger.error(env_error_format(err))
            sys.exit(abs(err.errno))

    def exec_delete(self):
        """Delete a medium"""
        exec_delete_medium_device(self, DSS_MEDIA)

    def exec_rename(self):
        """Rename a medium"""
        resources = self.params.get('res')
        new_lib = self.params.get('new_library')
        set_library(self)

        if new_lib is None:
            self.logger.info("No rename to be performed")
            return

        try:
            with AdminClient(lrs_required=False) as adm:
                count = adm.medium_rename(self.family, resources, self.library,
                                          new_lib)
        except EnvironmentError as err:
            self.logger.error("%s", env_error_format(err))
            sys.exit(abs(err.errno))

        if count > 0:
            self.logger.info("Rename %d media(s) successfully", count)
