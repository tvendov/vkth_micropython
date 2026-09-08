"""Restore the production TX GEN state after the RAM-only smooth-GEN tests."""
import json
from pathlib import Path
import start_gen_after_soak_20260908 as previous

WORK = Path(__file__).resolve().parent
DEPLOY = Path((WORK / 'gen-smooth-deployment-path.txt').read_text().strip())


def main():
    # Reuse the verified production-start sequence, not the old saved state.
    source = (WORK / 'start_gen_after_soak_20260908.py').read_text(encoding='utf-8')
    source = source.replace("rx-tone-monitor-deployment-path.txt", "gen-smooth-deployment-path.txt")
    source = source.replace("gen-soak-production-", "gen-smooth-production-")
    source = source.replace("WORK / 'gen-cycles-20260908-234251/before.json'",
                            "DEPLOY / 'production-before.json'")
    assert "gen-cycles-20260908-234251" not in source
    assert "rx-tone-monitor-deployment-path.txt" not in source
    before = json.loads((DEPLOY / 'production-before.json').read_text())[0]
    assert before['f'] == 579900 and before['m'] == 'AM'
    # Preserve the exact host script that was executed for provenance.
    (DEPLOY / 'production-start-host.py').write_text(source, encoding='utf-8')
    namespace = {'__file__': str(WORK / 'start_gen_smooth_20260909.py'),
                 '__name__': 'gen_smooth_production'}
    exec(compile(source, '<GEN smooth production start>', 'exec'), namespace)
    namespace['main']()


if __name__ == '__main__':
    main()
