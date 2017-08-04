#!/usr/bin/python

#
#  All rights reserved (c) 2014-2017 CEA/DAM.
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
Output and formatting utilities.
"""

import logging
import json
import csv
import StringIO
import xml.dom.minidom
import xml.etree.ElementTree

import yaml

def csv_dump(data):
    """Convert a list of dictionaries to a csv string"""

    outbuf = StringIO.StringIO()
    writer = csv.DictWriter(outbuf, sorted(data[0].keys()))
    if hasattr(writer, 'writeheader'):
        #pylint: disable=no-member
        writer.writeheader()
    else:
        writer.writerow(dict((item, item) for item in data[0]))
    writer.writerows(data)
    out = outbuf.getvalue()
    outbuf.close()
    return out

def xml_dump(data, item_type='item'):
    """Convert a list of dictionaries to xml"""

    top = xml.etree.ElementTree.Element('phobos')
    for item in data:
        children = xml.etree.ElementTree.Element(item_type, **item)
        top.append(children)
    rough_string = xml.etree.ElementTree.tostring(top)
    reparsed = xml.dom.minidom.parseString(rough_string)
    return reparsed.toprettyxml(indent="  ")

def human_dump(data, item_type='item'):
    """Convert a list of dictionaries to human readable text"""
    title = " %s " % (item_type)
    out = " {0:_^50}\n".format(str(title))
    for item in data:
        vals = []
        for key in sorted(item.keys()):
            vals.append(" |{0:<20}|{1:<27}|".format(key, item[key]))
        out = out+ "\n".join(vals) + "\n {0:_^50}\n".format("")
    return out

def dump_object_list(objs, fmt="human", numeric=False):
    """
    Helper for user friendly object display.
    """
    if not objs:
        return

    formats = {
        'json' : json.dumps,
        'yaml' : yaml.dump,
        'xml'  : xml_dump,
        'csv'  : csv_dump,
        'human': human_dump,
    }

    # Build a list of dict with attributs to export/output
    objlist = [x.get_display_dict(numeric) for x in objs]

    # Print formatted objects
    print formats[fmt](objlist)
