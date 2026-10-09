#!/usr/bin/env bash
# Builds the optional Linux FastScape worker in an isolated, pinned conda environment.
set -euo pipefail
repository=$(cd -- "$(dirname -- "$0")/.." && pwd)
prefix=${OFG_EROSION_PREFIX:-"$HOME/.local/share/ofg/fastscape"}
conda_executable=${CONDA_EXE:-"$HOME/miniconda3/bin/conda"}
source_directory="$prefix/src/fastscapelib-fortran"
revision=c21b5c038bde663a61b6226b0cc3d16ce17991ec
if [[ ! -x "$prefix/bin/python" ]]; then
    "$conda_executable" create -y --prefix "$prefix" --file "$repository/services/terrain/conda-linux-64.lock"
fi
# Conda's compiler activation scripts read optional, unset toolchain variables.
set +u
eval "$("$conda_executable" shell.bash hook)"
conda activate "$prefix"
set -u
if [[ ! -d "$source_directory/.git" ]]; then
    git clone --branch v2.8.4 --depth 1 https://github.com/fastscape-lem/fastscapelib-fortran.git "$source_directory"
fi
test "$(git -C "$source_directory" rev-parse HEAD)" = "$revision"
test -z "$(git -C "$source_directory" status --porcelain --untracked-files=no)"
python -m pip install --no-build-isolation --no-deps "$source_directory"
python -m pip install -r "$repository/services/terrain/requirements-erosion.lock"
python -m pip install --no-deps -e "$repository/services/terrain"
mkdir -p "$prefix/share"
OFG_SOLVER_REVISION="$revision" python - <<'PY'
import json, os, pathlib, subprocess, sys
compiler = subprocess.check_output([os.environ['FC'], '--version'], text=True).splitlines()[0]
(pathlib.Path(sys.prefix)/'share/ofg-fastscape-build.json').write_text(
    json.dumps(dict(revision=os.environ['OFG_SOLVER_REVISION'], compiler=compiler)))
PY
python -m pytest "$repository/services/terrain/tests" -q
