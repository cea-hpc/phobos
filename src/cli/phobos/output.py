#!/usr/bin/python

# Copyright CEA/DAM 2016
# This file is part of the Phobos project

"""
Output and formatting utilities.
"""

import logging
import json
import yaml
import csv
import StringIO
import xml.etree.ElementTree
import xml.dom.minidom

import phobos.capi.dss as cdss


def csv_dump(data):
    """Convert a list of dictionaries to a csv string"""

    outbuf = StringIO.StringIO()
    writer = csv.DictWriter(outbuf, data[0].keys())
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
        for key in item:
            vals.append(" |{0:<20}|{1:<27}|".format(key, item[key]))
        out = out+ "\n".join(vals) + "\n {0:_^50}\n".format("")
    return out

def dump_object_list(objs, fmt="human", numeric=False):
    """
    Helper for user friendly object display.
    """
    if not objs:
        return

    display = {
        cdss.dev_info:('serial', ['adm_status', 'changer_idx', 'family',
                                  'host', 'model', 'path', 'serial']),
        cdss.media_info:('model', ['adm_status', 'fs_status', 'fs_type'])
    }

    formats = {
        'json' : json.dumps,
        'yaml' : yaml.dump,
        'xml'  : xml_dump,
        'csv'  : csv_dump,
        'human': human_dump,
    }

    formatter = formats.get(fmt)
    if formatter is None:
        logger = logging.getLogger(__name__)
        logger.error("Unknown output format: %s", fmt)
        return

    #Is an instance of a known class ?
    dclass = None
    for key in display:
        if isinstance(objs[0], key):
            dclass = key

    if not dclass:
        logger = logging.getLogger(__name__)
        logger.error("No model found to display this class: %s",
                     objs[0].__class__.__name__)
        return

    objlist = []
    #Build a dict with attributs to export/output
    for obj in objs:
        objext = {}
        for key in display[dclass][1]:
            if not numeric and hasattr(cdss, '%s2str' % key):
                method = getattr(cdss, '%s2str' % key)
                objext[key] = method(getattr(obj, key))
            else:
                objext[key] = str(getattr(obj, key))
        objlist.append(objext)

    #Print formatted objects
    print formatter(objlist)
