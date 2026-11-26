# SPDX-License-Identifier: MIT
"""Allow ``python3 -m luac`` to invoke the compiler CLI."""

from .cli import main

raise SystemExit(main())
