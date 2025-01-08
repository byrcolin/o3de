#
# Copyright (c) Contributors to the Open 3D Engine Project.
# For complete copyright and license terms please see the LICENSE at the root of this distribution.
#
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
#

# Enable the remote Python Debugging
try:
    import os
    import debugpy
    current_dir = os.path.dirname(__file__)
    python_path = os.path.join(current_dir, "../../../python/python.cmd")
    debugpy.configure(python=python_path)
    debugpy.listen(("localhost", 5678))
except:
    pass