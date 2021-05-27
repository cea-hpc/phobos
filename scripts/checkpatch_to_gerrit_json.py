#!/usr/bin/env python3

"""
Convert checkpatch output into a gerrit-compatible JSON
"""

import re
import sys
import json

def main(checkpatch_output=sys.stdin):
    """Convert checkpatch output into a gerrit-compatible JSON"""
    comments = {}
    while True:
        line = checkpatch_output.readline()
        fileline = checkpatch_output.readline()
        if not fileline.strip():
            break
        while True:
            newline = checkpatch_output.readline()
            if not newline.strip():
                break
            line += newline

        match = re.search('FILE: (.*):([0-9]*):', fileline)
        if match is not None:
            filename = match.group(1)
            lineno = match.group(2)
            report = {
                'line': lineno,
                'message': line.strip(),
                'unresolved': 'true'
            }
        else:
            filename = '/COMMIT_MSG'
            report = {'message': line.strip(), 'unresolved': 'true'}

        file_comments = comments.setdefault(filename, [])
        file_comments.append(report)

    if comments:
        output = {
            'comments': comments,
            'message': 'Checkpatch %s' % line.strip(),
            'labels': {'Code-Review': -1}
        }
    else:
        output = {
            'message': 'Checkpatch OK',
            'notify': 'NONE',
            'labels': {'Code-Review': +1}
        }
    print(json.dumps(output))

if __name__ == '__main__':
    main()
