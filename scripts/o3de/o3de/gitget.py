
#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#
"""
This file contains all the code that has to do with getting a single file from any git repo
"""

import argparse
import logging
import json
import os
import pathlib
import sys
import urllib.parse
import urllib.request
import subprocess

from o3de import manifest, repo, utils, validation, compatibility, cmake

logging.basicConfig(format=utils.LOG_FORMAT)
logger = logging.getLogger('o3de.gitget')
logger.setLevel(logging.INFO)

def run_command(command, cwd=None):
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    if result.returncode != 0:
        print(f"Command '{' '.join(command)}' failed with exit code {result.returncode}")
        print(f"stdout: {result.stdout}")
        print(f"stderr: {result.stderr}")
        raise subprocess.CalledProcessError(result.returncode, command)
    return result.returncode

def sparse_checkout_root_files(repo_url, branch, local_dir):
    local_dir = pathlib.Path(local_dir)
    
    # Initialize a new Git repository
    return_code = run_command(["git", "init", str(local_dir)])
    if return_code != 0:
        return return_code
    
    # Add the remote repository
    return_code = run_command(["git", "-C", str(local_dir), "remote", "add", "origin", repo_url])
    if return_code != 0:
        return return_code
    
    # Enable sparse checkout
    return_code = run_command(["git", "-C", str(local_dir), "config", "core.sparseCheckout", "true"])
    if return_code != 0:
        return return_code
    
    # Specify the root files to checkout
    sparse_checkout_file = local_dir / ".git" / "info" / "sparse-checkout"
    with sparse_checkout_file.open("w") as f:
        f.write("/*\n")
        f.write("!/*/*\n")
    
    # Pull the specified files from the remote repository
    return_code = run_command(["git", "-C", str(local_dir), "pull", "origin", branch])
    if return_code != 0:
        return return_code
    
    return 0

def sparse_checkout(repo_url, branch, file_or_folder_path, local_dir):
    local_dir = pathlib.Path(local_dir)
    
    # Initialize a new Git repository
    return_code = run_command(["git", "init", str(local_dir)])
    if return_code != 0:
        return return_code
    
    # Add the remote repository
    return_code = run_command(["git", "-C", str(local_dir), "remote", "add", "origin", repo_url])
    if return_code != 0:
        return return_code
    
    # Enable sparse checkout
    return_code = run_command(["git", "-C", str(local_dir), "config", "core.sparseCheckout", "true"])
    if return_code != 0:
        return return_code
    
    # Specify the file or folder to checkout
    sparse_checkout_file = local_dir / ".git" / "info" / "sparse-checkout"
    with sparse_checkout_file.open("w") as f:
        f.write(str(file_or_folder_path) + "\n")
    
    # Pull the specified file or folder from the remote repository
    return_code = run_command(["git", "-C", str(local_dir), "pull", "origin", branch])
    if return_code != 0:
        return return_code
    
    return 0
 
def _run_gitget(args: argparse) -> int:
    branch = args.branch if args.branch else "main"
    if args.file is None:
        return sparse_checkout_root_files(args.git_repo, branch, args.to)
    else:
        return sparse_checkout(args.git_repo, branch, args.file, args.to)

def add_parser_args(parser):
    """
    add_parser_args is called to add arguments to each command such that it can be
    invoked locally or added by a central python file.
    Ex. Directly run from this file alone with: python gitget.py --git-repo "https://git.overlo3de.com/apmg/Marine.git" --file "gem.json" --to "f:/marine"
    :param parser: the caller passes an argparse parser like instance to this method
    """

    # Sub-commands should declare their own verbosity flag, if desired
    utils.add_verbosity_arg(parser)

    parser.description = "download a single file from any git repo"
                           
    parser.add_argument('--git-repo', 
                      dest='git_repo',
                      type=str,
                      required=True,
                      help='The git repo.')
    
    parser.add_argument('--file', 
                       dest='file',
                       type=pathlib.Path, 
                       required=False,
                       help='the file you want from the git repo. otherwise all root files' )

    parser.add_argument('--to', 
                      dest='to',
                      type=pathlib.Path,
                      required=True,
                      help='The path to the output folder.')
    
    parser.add_argument('--branch', 
                       dest='branch',
                       type=str, 
                       required=False,
                       help='the branch you want from the git repo.' )
       
    parser.set_defaults(func=_run_gitget)


def add_args(subparsers) -> None:
    """
    add_args is called to add subparsers arguments to each command such that it can be
    a central python file such as o3de.py.
    It can be run from the o3de.py script as follows
    call add_args and execute: python o3de.py gitget --git-repo "https://git.overlo3de.com/apmg/Marine.git" --file "gem.json" --to "f:/marine"
    :param subparsers: the caller instantiates subparsers and passes it in here
    """
    gitget = subparsers.add_parser('gitget')
    add_parser_args(gitget)


def main():
    """
    Runs gitget.py script as standalone script
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
