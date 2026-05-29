#!/usr/bin/env python3
"""
Generate type stubs during wheel build.
This runs after the extension is built but before the wheel is packaged.
"""

import re
import shutil
import subprocess
import sys
from pathlib import Path


def generate_init_pyi(fastmm_pyi_path, target_dir):
    """Generate __init__.pyi from fastmm.pyi to re-export all symbols."""
    print("[+] Generating __init__.pyi from fastmm.pyi")

    # Read the generated fastmm.pyi
    content = fastmm_pyi_path.read_text(encoding="utf-8")

    # Extract __all__ list
    all_match = re.search(r"__all__:\s*list\[str\]\s*=\s*\[(.*?)\]", content, re.DOTALL)
    if not all_match:
        print("[!] Warning: Could not find __all__ in fastmm.pyi")
        return

    # Parse the symbols from __all__
    all_content = all_match.group(1)
    symbols = [s.strip().strip("'\"") for s in all_content.split(",") if s.strip()]

    # Generate __init__.pyi content
    init_pyi = ['"""Type stubs for fastmm package"""', "", "from .fastmm import ("]

    # Add imports
    for symbol in symbols:
        init_pyi.append(f"    {symbol} as {symbol},")

    init_pyi.append(")")
    init_pyi.append("")
    init_pyi.append("__version__: str")
    init_pyi.append("")

    # Add __all__
    init_pyi.append("__all__ = [")
    for symbol in symbols:
        init_pyi.append(f'    "{symbol}",')
    init_pyi.append("]")
    init_pyi.append("")

    # Write __init__.pyi
    init_pyi_path = target_dir / "__init__.pyi"
    init_pyi_path.write_text("\n".join(init_pyi), encoding="utf-8")
    print(f"[+] Generated stub: __init__.pyi with {len(symbols)} symbols")


def main():
    if len(sys.argv) < 3:
        print("Usage: generate_stubs_for_wheel.py <install_prefix> <fastmm_source_dir>")
        return 1

    install_prefix = Path(sys.argv[1])
    fastmm_source_dir = Path(sys.argv[2])

    # The built extension should be in install_prefix
    # Add it to sys.path so pybind11-stubgen can import it
    sys.path.insert(0, str(install_prefix))

    print("=" * 70)
    print("Generating type stubs for wheel inclusion")
    print(f"Install prefix: {install_prefix}")
    print(f"Source dir: {fastmm_source_dir}")
    print("=" * 70)

    # Check if pybind11-stubgen is available
    try:
        import pybind11_stubgen
    except ImportError:
        print("[X] ERROR: pybind11-stubgen not found - cannot generate stubs")
        print("  Install with: pip install pybind11-stubgen")
        return 1  # Fail the build

    try:
        # Generate stubs to a temp location
        temp_stub_dir = install_prefix / "stub_temp"
        temp_stub_dir.mkdir(parents=True, exist_ok=True)

        result = subprocess.run(
            [
                sys.executable,
                "-m",
                "pybind11_stubgen",
                "fastmm",
                "-o",
                str(temp_stub_dir),
                "--ignore-invalid-expressions",
                ".*",
                "--ignore-invalid-identifiers",
                ".*",
            ],
            capture_output=True,
            text=True,
            env={**subprocess.os.environ, "PYTHONPATH": str(install_prefix)},
        )

        # Show output for debugging
        if result.stdout:
            print("pybind11-stubgen output:")
            print(result.stdout)
        if result.stderr:
            print("pybind11-stubgen errors:")
            print(result.stderr)

        if result.returncode == 0:
            # Copy generated stubs to source directory so they get installed
            # pybind11-stubgen may create either stub_temp/fastmm/*.pyi or stub_temp/fastmm.pyi
            stub_src_dir = temp_stub_dir / "fastmm"
            stub_src_file = temp_stub_dir / "fastmm.pyi"

            stubs_found = False

            # Check for directory with stubs
            if stub_src_dir.exists() and stub_src_dir.is_dir():
                for stub_file in stub_src_dir.glob("*.pyi"):
                    target = fastmm_source_dir / stub_file.name
                    shutil.copy2(stub_file, target)
                    print(f"[+] Generated stub: {stub_file.name}")
                    stubs_found = True

            # Check for single .pyi file
            if stub_src_file.exists():
                target = fastmm_source_dir / stub_src_file.name
                shutil.copy2(stub_src_file, target)
                print(f"[+] Generated stub: {stub_src_file.name}")
                stubs_found = True

                # Generate __init__.pyi to re-export everything from fastmm.pyi
                generate_init_pyi(stub_src_file, fastmm_source_dir)

            if stubs_found:
                # Clean up temp directory
                shutil.rmtree(temp_stub_dir)
                print("[+] Stubs generated successfully and copied to source")
                return 0
            else:
                print(f"[X] ERROR: No stubs generated")
                print(f"    Temp stub dir contents: {list(temp_stub_dir.iterdir())}")
                return 1
        else:
            print(f"[X] ERROR: Stub generation failed with return code {result.returncode}")
            return 1  # Fail the build

    except Exception as e:
        print(f"[X] ERROR: Could not generate stubs: {e}")
        return 1  # Fail the build


if __name__ == "__main__":
    sys.exit(main())
