
#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#
"""
This file contains all the code that has to do with updating the SPDX schema
"""

import argparse
import logging
import json
import os
import pathlib
import sys
import urllib.parse
import urllib.request

from o3de import manifest, repo, utils, validation, compatibility, cmake
from json_converter.json_mapper import JsonMapper, json_object, json_array
from json_converter.post_process import default_to

logging.basicConfig(format=utils.LOG_FORMAT)
logger = logging.getLogger('o3de.license_to_schema')
logger.setLevel(logging.INFO)

def licenses_to_schema(licenses_json_data, current_patterns_schema_json_data, output_json_path: pathlib.Path = None) -> int:
    """
    Updates a SPDX licnse json into a schema used by the schema 2.0.0   

    :return: 0 for success or non 0 failure code
    """      
    result = 0

    # Create new dict with schema version first
    new_license_schema = {
        "type": "object",
        "properties": {
            "licenseId": {
                "type": "string",
                "description": "The SPDX identifier for the license. i.e. 'Apache-2.0'",
                "oneOf": [
                    {
                        "enum": [
                            # we read the licenses.json and read each of the SPDX identifiers into this enum
                            # then based on this selection we default the other fields for the user
                        ]
                    },
                    {
                        "pattern": "^[a-zA-Z][a-zA-Z0-9_.-]*$"
                    }
                ]
            },
            "name": {
                "type": "string",
                "description": "The name of the license. i.e. 'Apache License 2.0'"
            },                
            "reference": {
                "description": "If you didn't include a copy of the license in the repo, then you may link to a website of the license i.e. 'https://opensource.org/licenses/Apache-2.0'",
                "$ref": "#/definitions/httpsftpsUri"
            },
            "is_deprecated": {
                "type": "boolean",
                "description": "If this license is deprecated, then set this to true. i.e. 'true'"
            },
            "details_url": {
                "description": "If you didn't include a copy of the license in the repo, then you may link to a json file of the license i.e. 'https://spdx.org/licenses/Apache-2.0.json'",
                "$ref": "#/definitions/httpsftpsUri"
            },
            "is_osi_approved": {
                "type": "boolean",
                "description": "If this license is OSI approved, then set this to true. i.e. 'true'"
            },
            "see_also": {
                "type": "array",
                "items": {
                    "$ref": "#/definitions/httpsftpsUri"
                }
            },
             "relative_path": {
                "definition": "If you didn't include a copy of the license in the repo, then this is the relative path to the license file from this object's root. i.e. 'licenses/mylicense.txt'",
                "$ref": "#definitions/relativePath"
            },
            "scope": {
                "type": "string",
                "description": "If this license is limited to a specific portion of this object state that here. i.e. 'Applies to all code in the X folder.'"
            }
        },
        "oneOf": [
            {
                "required": [
                    "licenseId",
                    "reference"
                ]
            },
            {
                "required": [
                    "licenseId",
                    "relative_path"
                ]
            }
        ],
        "allOf": [
            # This we will populate with conditions for each license
        ]
    }

    if 'licenses' in licenses_json_data:
        # Extract license IDs for enum
        new_license_schema['properties']['licenseId']['enum'] = [
            licenses['licenseId'] for licenses in licenses_json_data['licenses']
        ]

        # Build conditions for each license
        for license in licenses_json_data['licenses']:
            condition = {
                "if": {
                    "properties": {
                        "licenseId": {"const": license['licenseId']}
                    }
                },
                "then": {
                    "properties": {
                        "name": {"const": license['name']},
                        "reference": {"const": license['reference']},
                        "is_deprecated": {"const": license['isDeprecatedLicenseId']},
                        "details_url": {"const": license['detailsUrl']},
                        "is_osi_approved": {"const": license['isOsiApproved']},
                        "see_also": {
                            "type": "array",
                            "items": {
                                "enum": license['seeAlso']
                            }
                        }
                    }
                }
            }
            new_license_schema['allOf'].append(condition)

    #replace the license schema in the current patterns schema json
    current_patterns_schema_json_data['definitions']['license'] = new_license_schema
   
    # write the output json file
    if isinstance(output_json_path, pathlib.PurePath):
        try:
            with open(output_json_path, 'w') as f:
                json.dump(current_patterns_schema_json_data, f, indent=4)
        except Exception as e:
            logger.error(f'Failed to write json file: {output_json_path} {e}')
            return 1
    else:
        try:
            with open(licenses_json_data, 'w') as f:
                json.dump(current_patterns_schema_json_data, f, indent=4)
        except Exception as e:
            logger.error(f'Failed to write json file: {licenses_json_data} {e}')
            return 1
    
    return 0

def _run_license_to_schema(args: argparse) -> int:
    
    #load the current patterns schema json file
    current_patterns_schema_json_data = {}
    try:
        with open(args.current_patterns_schema_json, 'r') as f:
            current_patterns_schema_json_data = json.load(f)
    except Exception as e:
        logger.error(f'Failed to load json file: {args.current_patterns_schema_json} {e}')
        return 1

    #load licenses.json
    licenses_json_data = {}
    if hasattr(args, 'licenses_json') and args.licenses_json:
        # Load the licenses.json file
        try:
            with open(args.licenses_json, 'r') as f:
                licenses_json_data = json.load(f)
        except Exception as e:
            logger.error(f'Failed to load json file: {args.license_json} {e}')
            return 1
    else:
        # Download the licenses.json file
        licenses_url = 'https://raw.githubusercontent.com/spdx/license-list-data/refs/heads/main/json/licenses.json'
        try:
            with urllib.request.urlopen(licenses_url) as url:
                licenses_json_data = json.loads(url.read().decode())
        except urllib.error.HTTPError as e:
            logger.error(f'HTTP Error {e.code} opening {licenses_url.geturl()}')
            return 1
        except urllib.error.URLError as e:
            logger.error(f'URL Error {e.reason} opening {licenses_url.geturl()}')
            return 1
        except Exception as e:
            logger.error(f'Error: {e}')
            return 1
    
    if hasattr(args, 'output_patterns_schema_json') and args.output_patterns_schema_json:
        return licenses_to_schema(licenses_json_data, current_patterns_schema_json_data, output_json_path=args.output_patterns_schema_json)

    # if no ouptut file is specified, transform fully inplace
    else:
        return licenses_to_schema(licenses_json_data, current_patterns_schema_json_data, output_json_path=args.current_patterns_schema_json)
        

def add_parser_args(parser):
    """
    add_parser_args is called to add arguments to each command such that it can be
    invoked locally or added by a central python file.
    Ex. Directly run from this file alone with: python license_to_schema.py --in "C:/o3de/engine.json" --to-1_0_0 "C:/o3de/engine-1.0.0.json"
    :param parser: the caller passes an argparse parser like instance to this method
    """

    # Sub-commands should declare their own verbosity flag, if desired
    utils.add_verbosity_arg(parser)

    parser.description = "Upgrades O3DE JSON schema files to newer versions"
                           
    parser.add_argument('--current-patterns-schema-json', 
                      dest='current_patterns_schema_json',
                      type=pathlib.Path,
                      required=True,
                      help='The path to the current patterns schema json file.')
    
    parser.add_argument('--licenses-json', 
                       dest='licenses_json',
                       type=pathlib.Path, 
                       required=False,
                       help='You can manually download the licenses.json file from https://github.com/spdx/license-list-data/blob/main/json/licenses.json and pass it in here. If not it will automatically download it.' )

    parser.add_argument('--output-patterns-schema-json', 
                      dest='output_patterns_schema_json',
                      type=pathlib.Path,
                      required=False,
                      help='The path to the output patterns schema json file. If not supplied the current-patterns-schema-json will be updated in place.')
       
    parser.set_defaults(func=_run_license_to_schema)


def add_args(subparsers) -> None:
    """
    add_args is called to add subparsers arguments to each command such that it can be
    a central python file such as o3de.py.
    It can be run from the o3de.py script as follows
    call add_args and execute: python o3de.py license-to-schema --license-json "C:/license.json" --to "C:/schemas/license.json"
    :param subparsers: the caller instantiates subparsers and passes it in here
    """
    license_to_schema = subparsers.add_parser('license-to-schema')
    add_parser_args(license_to_schema)


def main():
    """
    Runs license_to_schema.py script as standalone script
    """
    # parse the command line args
    the_parser = argparse.ArgumentParser()

    # add args to the parser
    add_parser_args(the_parser)

    # parse args
    the_args = the_parser.parse_args()

    # run
    ret = the_args.func(the_args) if hasattr(the_args, 'func') else 1
    logger.info('Success!' if ret == 0 else 'Completed with issues: result {}'.format(ret))

    # return
    sys.exit(ret)


if __name__ == "__main__":
    main()
