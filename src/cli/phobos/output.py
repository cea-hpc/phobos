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
from dss import *

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

    formatter = formats.get(fmt)
    if formatter is None:
        logger = logging.getLogger(__name__)
        logger.error("Unknown output format: %s", fmt)
        return

    #Build a list of dict with attributs to export/output
    objlist = [x.todict(numeric) for x in objs]

    #Print formatted objects
    print formatter(objlist)
