"""Run the local HTTP service or generate one diagnostic revision from a JSON recipe."""

import argparse
import json
from pathlib import Path

from .app import create_app
from .content import adopt_revision, publish_revision
from .island import IslandParameters, generate_island


def main() -> None:
    """Parse explicit local paths and start a single-process HTTP host or an offline generation."""
    parser = argparse.ArgumentParser(description='OFG terrain laboratory service')
    parser.add_argument('command', choices=['serve', 'generate'])
    parser.add_argument('--data-dir', type=Path, default=Path('artifacts/terrain-service/data'))
    parser.add_argument('--read-only', type=Path, help='Saved content root (contains island/revisions directories)')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--output-limit-mib', type=int, default=4096)
    parser.add_argument('--island', default='demo')
    parser.add_argument('--recipe', type=Path)
    args = parser.parse_args()
    if args.command == 'generate':
        params = IslandParameters.model_validate_json(args.recipe.read_text()) if args.recipe else IslandParameters()
        revision = publish_revision(args.data_dir, args.island, generate_island(params))
        adopt_revision(args.data_dir, args.island, revision)
        print(json.dumps({'revision': revision, 'directory': str(args.data_dir / args.island / 'revisions' / revision)}))
    else:
        import uvicorn
        uvicorn.run(create_app(args.read_only or args.data_dir, args.read_only is not None,
                               args.output_limit_mib << 20), host=args.host, port=args.port)


if __name__ == '__main__':
    main()
