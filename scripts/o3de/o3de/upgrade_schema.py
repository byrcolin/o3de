
#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#
"""
This file contains all the code that has to do with upgrading o3de object json files
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
logger = logging.getLogger('o3de.upgrade_schema')
logger.setLevel(logging.INFO)

def is_reverse_domain_format(name: str) -> bool:
    if not name or name.count('.') < 2:
        return False
    
    # Common TLDs that might appear as first segment
    tlds = {'org', 'com', 'net', 'edu', 'gov', 'io'}
    
    # Check if first segment is a TLD and name is lowercase
    first_segment = name.split('.')[0]
    return first_segment in tlds and name.islower()

def looks_like_spdx(license_str: str) -> bool:
    # Basic SPDX format check: Contains version numbers or standard keywords
    spdx_patterns = [
        '-', 'OR', 'AND', '0.', '1.', '2.', '3.', '4.',
        'Apache', 'MIT', 'GPL', 'LGPL', 'BSD', 'MPL', 'EPL'
    ]
    return any(pattern in license_str for pattern in spdx_patterns)

def is_valid_canonical_tag(tag: str) -> bool:
    valid_tags = {
        "Engine",
        "Project", 
        "Gem",
        "Template",
        "Repo",
        "Extension"
    }
    return tag in valid_tags

def is_valid_canonical_tag(tag: str) -> bool:
    canonical_mapping = {
        "engine": "Engine",
        "project": "Project",
        "gem": "Gem",
        "template": "Template",
        "repo": "Repo",
        "extension": "Extension"
    }
    return tag.lower() in canonical_mapping

def get_canonical_tag(tag: str) -> str:
    canonical_mapping = {
        "engine": "Engine",
        "project": "Project",
        "gem": "Gem",
        "template": "Template",
        "repo": "Repo",
        "extension": "Extension"
    }
    return canonical_mapping.get(tag.lower())

def upgrade_1_0_0_to_2_0_0(input_data) -> tuple[int, dict]:
    """
    Updates a o3de object json from 1.0.0 -> 2.0.0   

    :return: 0 for success or non 0 failure code
    """
    result = 0

    # Find first non-repo URL in any field
    reversed_domain = 'com.domain'  # default fallback
    for value in input_data.values():
        if isinstance(value, str) and ('http://' in value or 'https://' in value or 'ftp://' in value or "ftps://" in value):
            url = value.replace('https://', '').replace('http://', '').replace('ftps://', '').replace('ftp://', '')
            domain = url.split('/')[0]
            if not any(repo in domain for repo in [
                'github.com',
                'githubusercontent.com',
                'gitlab.com', 
                'bitbucket.org',
                'dev.azure.com',
                'sourceforge.net',
                'codeberg.org',
                'launchpad.net'
            ]):
                domain_parts = domain.split('.')
                reversed_domain = '.'.join(reversed(domain_parts))
                break

    # Create new dict with schema version first
    output_data = {}
    if 'engine_name' in input_data:
        output_data = {
            "$schema": "https://overlo3de.com/o3de-engine-2.0.0.json",
            "$schemaVersion": "2.0.0"
        }
    elif 'project_name' in input_data:
        output_data = {
            "$schema": "https://overlo3de.com/o3de-project-2.0.0.json",
            "$schemaVersion": "2.0.0"
        }
    elif 'gem_name' in input_data:
        output_data = {
            "$schema": "https://overlo3de.com/o3de-gem-2.0.0.json",
            "$schemaVersion": "2.0.0"
        }
    elif 'template_name' in input_data:
        output_data = {
            "$schema": "https://overlo3de.com/o3de-template-2.0.0.json",
            "$schemaVersion": "2.0.0"
        }
    elif 'repo_name' in input_data:
        output_data = {
            "$schema": "https://overlo3de.com/o3de-repo-2.0.0.json",
            "$schemaVersion": "2.0.0"
        }
    elif 'restricted_name' in input_data:
        output_data = {
            "$schema": "https://overlo3de.com/o3de-extension-2.0.0.json",
            "$schemaVersion": "2.0.0"
        }
    elif 'extension_name' in input_data:
        output_data = {
            "$schema": "https://overlo3de.com/o3de-extension-2.0.0.json",
            "$schemaVersion": "2.0.0"
        }

    json_mapper = JsonMapper(input_data)
    mapped_data = json_mapper.map(
        {
            'engine_name': ['engine_name', lambda x: (
                x if x and is_reverse_domain_format(x)
                else f"{reversed_domain}.{x}".lower() if x 
                else None
            )],
            'project_name': ['project_name', lambda x: (
                x if x and is_reverse_domain_format(x)
                else f"{reversed_domain}.{x}".lower() if x 
                else None
            )],
            'gem_name': ['gem_name', lambda x: (
                x if x and is_reverse_domain_format(x)
                else f"{reversed_domain}.{x}".lower() if x 
                else None
            )],
            'template_name': ['template_name', lambda x: (
                x if x and is_reverse_domain_format(x)
                else f"{reversed_domain}.{x}".lower() if x 
                else None
            )],
            'repo_name': ['repo_name', lambda x: (
                x if x and is_reverse_domain_format(x)
                else f"{reversed_domain}.{x}".lower() if x 
                else None
            )],
            'restricted_name': ['extension_name', lambda x: (
                x if x and is_reverse_domain_format(x)
                else f"{reversed_domain}.{x}".lower() if x 
                else None
            )],

            'engine': ['engine', lambda x: 'org.o3de.o3de' if x == 'o3de' else 'org.o3de.o3de-sdk' if x == 'o3de-sdk' else x],

            'product_name': ['product_name'],
            'executable_name': ['executable_name'],
            'modules': ['modules'],
            'project_id': ['project_id'],

            #'repo_uri': ['repo_uri'],

            'additonal_info': ['additonal_info'],

            'last_updated': ['last_updated'],

            'sha256': ['sha256'],

            'display_name': ['display_name'],
            'summary': ['summary'],
            
            'origin': ['origin', lambda x: (
                {
                    **({'name': x} if x else {}),
                    **({'uri': input_data.get('origin_url')} if input_data.get('origin_url') else {})
                } if (x or input_data.get('origin_url')) else None
            )],
            
            'version': ['version', default_to, '0.0.0'],
            
            'api_version': ['api_version'],

            'licenses': ['license', lambda x: [{
                "licenseId" if looks_like_spdx(x) else "custom_licenseId": x,
                **({"reference": input_data.get('license_url')} if input_data.get('license_url') else {}),
                **({"relative_path": input_data.get('license_path')} if input_data.get('license_path') else {}),
                **({"scope": input_data.get('license_scope')} if input_data.get('license_scope') else {}),
            }] if x else None],

            'copyright': ['copyright'],

            'gem_type': ['type', lambda x: x.lower() if x else None],
            
            'canonical_tags': ['canonical_tags', lambda x: (
                [get_canonical_tag(tag) for tag in x if is_valid_canonical_tag(tag)] if isinstance(x, list) 
                else None
            )],
            
            'user_tags': ['user_tags'],
            
            'icon': ['icon_path', lambda x: (
                {
                    **({'relative_path': x} if x else {}),
                    **({'uri': input_data.get('icon_url')} if input_data.get('icon_url') else {})
                } if (x or input_data.get('icon_url')) else None
            )],

            'requirements': ['requirements'],

            'documentation': ['documentation_path', lambda x: (
                {
                    **({'relative_path': x} if x else {}),
                    **({'uri': input_data.get('documentation_url')} if input_data.get('documentation_url') else {})
                } if (x or input_data.get('documentation_url')) else None
            )],
         
            'O3DEVersion': ['O3DEVersion'],
            'O3DEBuildNumber': ['O3DEBuildNumber'],

            'api_versions': ['api_versions'],

            'file_version': ['file_version'],

            'build': ['build'],

            'copy_files': ['copy_files'],
            'create_directories': ['create_directories'],

            'restricted_platform_relative_path': ['extension_platform_relative_path'],
            'template_restricted_platform_relative_path': ['template_extension_platform_relative_path'],

            'compatibilities': ['compatible_engines', lambda x: (
                {
                    "engines": [f"org.o3de.{engine}".lower() for engine in x] if x else None
                }
            )],

            'platforms': ['platforms'],

            'download': ['download_source_uri', lambda x: (
                {
                    "uris": {
                        **({"source_zip_uri": f"{input_data.get('download_source_uri')}.zip" if input_data.get('download_source_uri') and not input_data.get('download_source_uri').endswith('.zip') else input_data.get('download_source_uri')} if input_data.get('download_source_uri') else {}),
                        **({"lfs_zip_uri": f"{input_data.get('download_lfs_uri')}.zip" if input_data.get('download_lfs_uri') and not input_data.get('download_lfs_uri').endswith('.zip') else input_data.get('download_lfs_uri')} if input_data.get('download_lfs_uri') else {})
                    },
                    **({"source_zip_sha256": input_data.get('sha256')} if input_data.get('sha256') else {}),
                    **({"lfs_zip_sha256": input_data.get('lfs_sha256')} if input_data.get('lfs_sha256') else {}),
                } if (x or input_data.get('download_source_uri') or input_data.get('download_lfs_uri') or input_data.get('sha256') or input_data.get('lfs_sha256')) else None
            )],

            'source_control': ['source_control_uri', lambda x: (
                {
                    **({'uri': f"{x}.git" if x and not x.endswith('.git') else x} if x else {}),
                    **({'relative_path': input_data.get('source_control_path')} if input_data.get('source_control_path') else {}),
                    **({'branch': input_data.get('source_control_branch')} if input_data.get('source_control_branch') else {}),
                    **({'tag': input_data.get('source_control_tag')} if input_data.get('source_control_tag') else {}),
                } if (x or input_data.get('source_control_path') or input_data.get('source_control_branch') or input_data.get('source_control_tag')) else None
            )],

            'releases': ['versions_data', lambda x: [{
                "version": version.get('version', '0.0.0'),
                **({"download": {
                    "uris": {
                        **({"source_zip_uri": f"{version.get('download_source_uri')}.zip" if version.get('download_source_uri') and not version.get('download_source_uri').endswith('.zip') else version.get('download_source_uri')} if version.get('download_source_uri') else {}),
                        **({"lfs_zip_uri": f"{version.get('download_lfs_uri')}.zip" if version.get('download_lfs_uri') and not version.get('download_lfs_uri').endswith('.zip') else version.get('download_lfs_uri')} if version.get('download_lfs_uri') else {})
                    },
                    **({"source_zip_sha256": version.get('sha256')} if version.get('sha256') else {}),
                    **({"lfs_zip_sha256": version.get('lfs_sha256')} if version.get('lfs_sha256') else {})
                }} if any(version.get(k) for k in ['download_source_uri', 'download_lfs_uri', 'sha256', 'lfs_sha256']) else {}),
                **({"source_control": {
                    **({"uri": f"{version.get('source_control_uri')}.git" if version.get('source_control_uri') and not version.get('source_control_uri').endswith('.git') else version.get('source_control_uri')} if version.get('source_control_uri') else {}),
                    **({"relative_path": version.get('source_control_path')} if version.get('source_control_path') else {}),
                    **({"branch": version.get('source_control_branch')} if version.get('source_control_branch') else {}),
                    **({"tag": version.get('source_control_tag')} if version.get('source_control_tag') else {})
                }} if any(version.get(k) for k in ['source_control_uri', 'source_control_path', 'source_control_branch', 'source_control_tag']) else {})
            } for version in (x if isinstance(x, list) else [])] if x else None],
        }
    )

    # Only add non-empty lists to children
    children = {}
    if input_data.get('engines'):
        children['engines'] = input_data['engines']
    if input_data.get('templates'):
        children['templates'] = input_data['templates']
    if input_data.get('projects'):
        children['projects'] = input_data['projects']
    if input_data.get('repos'):
        children['repos'] = input_data['repos']
    if input_data.get('external_subdirectories'):
        children['gems'] = input_data['external_subdirectories']
    # if children is not empty add it to mapped_data
    if children:
        mapped_data['children'] = children

    # Only add non-empty lists to children
    dependencies = {}
    if input_data.get('gem_names'):
        transformed_gems = [
            x if x and is_reverse_domain_format(x)
            else f"org.o3de.{x}>=0.0.0".lower() if x 
            else None
            for x in input_data['gem_names']
        ]
        # Filter out None values
        if transformed_gems:
            dependencies['gems'] = [g for g in transformed_gems if g is not None]
    if input_data.get('dependencies'):
        transformed_gems = [
            x if x and is_reverse_domain_format(x)
            else f"org.o3de.{x}>=0.0.0".lower() if x 
            else None
            for x in input_data['dependencies']
        ]
        # Filter out None values
        if transformed_gems:
            valid_gems = [g for g in transformed_gems if g is not None]
            if 'gems' in dependencies:
                dependencies['gems'].extend(valid_gems)
            else:
                dependencies['gems'] = valid_gems
    #if dependencies is not empty add it to mapped_data
    if dependencies:
        mapped_data['dependencies'] = dependencies
    
    # Remove any duplicate schema version from mapped data
    if '$schemaVersion' in mapped_data:
        del mapped_data['$schemaVersion']
    
    # Update output_data with mapped fields while preserving order
    output_data.update(mapped_data)

    return result, output_data


def upgrade_0_0_0_to_1_0_0(input_data) -> tuple[int, dict]:
    """
    Updates a o3de object json from 0.0.0 -> 1.0.0   

    :return: 0 for success or non 0 failure code
    """
    result = 0

    # Create new dict with schema version first
    output_data = {'$schemaVersion': '1.0.0'}

    json_mapper = JsonMapper(input_data)
    mapped_data = json_mapper.map(
        {
            'engine_name': ['engine_name'],
            'project_name': ['project_name'],
            'gem_name': ['gem_name'],
            'template_name': ['template_name'],
            'repo_name': ['repo_name'],
            'restricted_name': ['restricted_name'],

            'engine': ['engine'],
            'product_name': ['product_name'],
            'executable_name': ['executable_name'],
            'modules': ['modules'],
            'project_id': ['project_id'],

            'repo_uri': ['repo_uri'],

            'additonal_info': ['additonal_info'],

            'last_updated': ['last_updated'],

            'sha256': ['sha256'],

            'display_name': ['display_name'],
            'summary': ['summary'],
            
            'origin': ['origin'],
            'origin_url': ['origin_url'],
            
            'version': ['version', default_to, '0.0.0'],
            
            'api_version': ['api_version'],

            'license': ['license'],
            'license_url': ['license_url'],
            
            'copyright': ['copyright'],

            'type': ['type'],
            
            'canonical_tags': ['canonical_tags'],
            
            'user_tags': ['user_tags'],
            
            'icon_path': ['icon_path'],
            'icon_url': ['icon_url'],

            'requirements': ['requirements'],

            'documentation_path': ['documentation_path'],
            'documentation_url': ['documentation_url'],

            'dependencies': ['dependencies'],

            'O3DEVersion': ['O3DEVersion'],
            'O3DEBuildNumber': ['O3DEBuildNumber'],

            'api_versions': ['api_versions'],

            'file_version': ['file_version'],

            'build': ['build'],

            'gem_names': ['gem_names'],

            'external_subdirectories': ['external_subdirectories'],
            
            'projects': ['projects'],

            'templates': ['templates'],

            'repos': ['repos'],
            
            'restricted': ['restricted'],

            'copy_files': ['copy_files'],
            'create_directories': ['create_directories'],

            'restricted_platform_relative_path': ['restricted_platform_relative_path'],
            'template_restricted_platform_relative_path': ['template_restricted_platform_relative_path'],

            'source_control_uri': ['source_control_uri'],
            'source_control_path': ['source_control_path'],
            'source_control_branch': ['source_control_branch'],
            'source_control_tag': ['source_control_tag'],

            'versions_data': ['versions_data'],

            'platforms': ['platforms'],

            'compatible_engines': ['compatible_engines'],

            'download_source_uri': ['download_source_uri'],
        }
    )

    # Remove any duplicate schema version from mapped data
    if '$schemaVersion' in mapped_data:
        del mapped_data['$schemaVersion']
    
    # Update output_data with mapped fields while preserving order
    output_data.update(mapped_data)

    return result, output_data

def upgrade_to_1_0_0(input_json_path: pathlib.Path = None, output_json_path: pathlib.Path = None) -> int:
    """
    Updates a o3de object json to 1.0.0   

    :return: 0 for success or non 0 failure code
    """
    input_json_data = None
    output_json_data = None

    # load the json file  
    if isinstance(input_json_path, pathlib.PurePath):
        try:
            with open(input_json_path, 'r') as f:
                input_json_data = json.load(f)
        except Exception as e:
            logger.error(f'Failed to load json file: {input_json_path}')
            return 1

    # check what $schemaVersion the input file is
    schema_version = input_json_data.get('$schemaVersion', '0.0.0')

    #upgrade as needed
    result = 0
    if not result and schema_version == '0.0.0':
        result, output_json_data = upgrade_0_0_0_to_1_0_0(input_json_data)
        schema_version = '1.0.0'
    else:
        output_json_data = input_json_data

    if result:
        logger.error(f'Failed to upgrade to schema version: {schema_version}')
        return 1
    
     # write the output json file
    if isinstance(output_json_path, pathlib.PurePath):
        try:
            with open(output_json_path, 'w') as f:
                json.dump(output_json_data, f, indent=4)
        except Exception as e:
            logger.error(f'Failed to write json file: {output_json_path}')
            return 1
    else:
        try:
            with open(input_json_path, 'w') as f:
                json.dump(output_json_data, f, indent=4)
        except Exception as e:
            logger.error(f'Failed to write json file: {input_json_path}')
            return 1
    
    return 0

def upgrade_to_2_0_0(input_json_path: pathlib.Path = None, output_json_path: pathlib.Path = None) -> int:
    """
    Updates a o3de object json to 2.0.0   

    :return: 0 for success or non 0 failure code
    """    
    input_json_data = None
    output_json_data = None

    # load the json file
    if isinstance(input_json_path, pathlib.PurePath):
        try:
            with open(input_json_path, 'r') as f:
                input_json_data = json.load(f)
        except Exception as e:
            logger.error(f'Failed to load json file: {input_json_path}')
            return 1

    # check what $schemaVersion the input file is
    schema_version = input_json_data.get('$schemaVersion', '0.0.0')
    
    #upgrade as needed
    result = 0
    if not result and schema_version == '0.0.0':
        result, output_json_data = upgrade_0_0_0_to_1_0_0(input_json_data)
        schema_version = '1.0.0'
    else:
        output_json_data = input_json_data
        
    if not result and schema_version == '1.0.0':
        result, output_json_data = upgrade_1_0_0_to_2_0_0(output_json_data)
        schema_version = '2.0.0'

    if result:
        logger.error(f'Failed to upgrade to schema version: {schema_version}')
        return 1
    
     # write the output json file
    if isinstance(output_json_path, pathlib.PurePath):
        try:
            with open(output_json_path, 'w') as f:
                json.dump(output_json_data, f, indent=4)
        except Exception as e:
            logger.error(f'Failed to write json file: {output_json_path}')
            return 1
    else:
        try:
            with open(input_json_path, 'w') as f:
                json.dump(output_json_data, f, indent=4)
        except Exception as e:
            logger.error(f'Failed to write json file: {input_json_path}')
            return 1
    
    return 0

def _run_upgrade(args: argparse) -> int:
   
    if hasattr(args, 'to_1_0_0') and args.to_1_0_0:
        return upgrade_to_1_0_0(input_json_path=args.object_json, output_json_path=args.to_1_0_0)

    elif hasattr(args, 'to_2_0_0') and args.to_2_0_0:
        return upgrade_to_2_0_0(input_json_path=args.object_json, output_json_path=args.to_2_0_0)
    
    # if no version is specified, upgrade fully inplace
    else:
        return upgrade_to_2_0_0(input_json_path=args.object_json, output_json_path=args.object_json)
        

def add_parser_args(parser):
    """
    add_parser_args is called to add arguments to each command such that it can be
    invoked locally or added by a central python file.
    Ex. Directly run from this file alone with: python o3de.py upgrade-schema --object-json "C:/Gems/MyGem/gem.json" --to_2.0.0 "C:/Gems/MyGem/gem-2.0.0.json"
    :param parser: the caller passes an argparse parser like instance to this method
    """

    # Sub-commands should declare their own verbosity flag, if desired
    utils.add_verbosity_arg(parser)

    parser.description = "Upgrades O3DE JSON schema files to newer versions"
    parser.add_argument('--object-json', 
                       dest='object_json',
                       type=pathlib.Path, 
                       required=True,
                       help='Input o3de JSON file to upgrade')
                       
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--to-1.0.0',
                      dest='to_1_0_0',
                      type=pathlib.Path,
                      help='Upgrade to schema 1.0.0')
    group.add_argument('--to-2.0.0', 
                      dest='to_2_0_0',
                      type=pathlib.Path,
                      help='Upgrade to schema 2.0.0')
       
    parser.set_defaults(func=_run_upgrade)


def add_args(subparsers) -> None:
    """
    add_args is called to add subparsers arguments to each command such that it can be
    a central python file such as o3de.py.
    It can be run from the o3de.py script as follows
    call add_args and execute: python o3de.py upgrade-schema --object-json "C:/Gems/MyGem/gem.json" --to_2.0.0 "C:/Gems/MyGem/gem-2.0.0.json"
    :param subparsers: the caller instantiates subparsers and passes it in here
    """
    upgrade_schema = subparsers.add_parser('upgrade-schema')
    add_parser_args(upgrade_schema)


def main():
    """
    Runs upgrade_schema.py script as standalone script
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
